#include <iostream>
#include <common/structured_ini.h>
#include <common/basic_exchange.h>
#include <common/basic_config.h>
#include <common/basic_context.h>
#include <common/basic_log.h>
#include <common/memory_storage.h>
#include <common/report_ldjson.h>
#include <common/loader.h>
#include <common/basic_mq.h>
#include "sim_exchange.h"
#include "sim_scheduler.h"


using namespace quarkbot;

static ModuleRepository modules;

std::shared_ptr<StructuredIni> load_config(const std::string &fname) {
    auto ini = std::make_shared<StructuredIni>();
    ini->parse_file(std::filesystem::path(fname));
    return ini;
}


struct AccountInstrumentMap {
    std::unordered_map<std::string_view, std::vector<Account> > _accounts;
    std::unordered_map<std::string_view, std::vector<Instrument> > _instruments;
};

void configure_context(BasicExchangeContext &ctx, Config cfg, AccountInstrumentMap &aimap) {
    Config accounts = cfg["accounts"];
    Config instruments = cfg["instruments"];


    for (auto acc: accounts.list_sections()) {
        Config acc_query = accounts[acc]["query"];
        ctx.query_accounts(ExchangeCredentials(),  acc_query, acc, [&](std::span<Account> accs){
            auto &lst = aimap._accounts[acc];
            for (Account x: accs) {
                lst.push_back(x);
            }
        });
    }
    for (auto instr: instruments.list_sections()) {
        Config instr_query = instruments[instr]["query"];
        ctx.query_instruments( instr_query, instr, [&](std::span<Instrument> instrs){
            auto &lst = aimap._instruments[instr];
            for (Instrument x: instrs) {
                lst.push_back(x);
            }
        });
    }
}

// prints the explanatory string of an exception. If the exception is nested,
// recurses to print the explanatory of the exception it holds
void print_exception(const std::exception& e, int level =  0)
{
    std::cerr << std::string(level, '\t') << (level?"...because: ":"exception: ") << e.what() << '\n';
    try
    {
        std::rethrow_if_nested(e);
    }
    catch (const std::exception& nestedException)
    {
        print_exception(nestedException, level + 1);
    }
    catch (...) {}
}



void load_strategies(StructuredIni::Section strategy_cfg,
        std::shared_ptr<StructuredIni> whole_cfg,
        const AccountInstrumentMap &aimap,
        SimScheduler &scheduler,
        Log logger,
        MQBroker mq_broker,
        std::vector<std::shared_ptr<IContext> > &out) {
    std::vector<Account> accounts;
    std::vector<Instrument> instruments;
    for (const auto &[label, s]: strategy_cfg.sections()) {
        StructuredIni::Section scfg = strategy_cfg[label];
        std::string_view name = scfg("name",label);
        auto alist = scfg("accounts","");
        for (const std::string_view &item: alist) {
            if (!item.empty()) {
                auto iter = aimap._accounts.find(item);
                if (iter == aimap._accounts.end()) {
                    throw std::runtime_error("Account '"+std::string(item)+"' is not defined");
                }
                std::copy(iter->second.begin(), iter->second.end(), std::back_inserter(accounts));
            }
        }
        auto ilist = scfg("instruments","");
        for (const std::string_view &item: ilist) {
            if (!item.empty()) {
                auto iter = aimap._instruments.find(item);
                if (iter == aimap._instruments.end()) {
                    throw std::runtime_error("Instrument '"+std::string(item)+"' is not defined");
                }
                std::copy(iter->second.begin(), iter->second.end(), std::back_inserter(instruments));
            }
        }
        auto storage = std::make_unique<MemoryStorage>();
        auto report = std::make_unique<ReportLDJsonToStream<> >(std::move(storage),
                std::string(name), std::cout);

        auto context = std::make_shared<BasicContext>(std::move(report),
                                    scheduler.get_instance_for_strategy(),
                                    logger,
                                    mq_broker,
                                    name);
        auto strategy = modules.create_strategy(name);
        if (!strategy) {
            throw std::runtime_error("Strategy '"+std::string(name) + "' wasn't found (missing module at command line?)");
        }
        context->init(std::move(strategy),
                      std::move(accounts),
                      std::move(instruments),
                      Config(std::make_shared<BasicConfig>(whole_cfg, scfg["configuration"])));
        out.push_back(std::move(context));
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " config_path modules..." << std::endl;
        std::cerr << std::endl;
        std::cerr << "config_path         path to configuration"  << std::endl;
        std::cerr << "modules...          list of modules with strategies to load"  << std::endl;
        return 1;
    }

    for (int i = 2; i < argc; ++i) {
        modules.add_module(argv[i]);
    }


    try {
        std::string cfgfname = argv[1];
        auto inicfg = load_config(cfgfname);
        auto cfg = Config(std::make_shared<BasicConfig>(inicfg));

        bool replay_done = false;

        SimScheduler scheduler;
        auto logservice = std::make_shared<BasicLog>(std::cerr, ILog::Serverity::debug);
        auto context = std::make_shared<BasicExchangeContext>("simulator",
                scheduler.get_instance_for_exchange(), Network(), Log(logservice));
        context->init(std::make_unique<SimExchange>([&]{
                        replay_done = true;
                    }), cfg["simulator"]);

        AccountInstrumentMap aimap;
        auto mq = std::make_shared<BasicMQ>();
        configure_context(*context, cfg, aimap);

        std::vector<std::shared_ptr<IContext> > strategies;
        load_strategies((*inicfg)["strategies"],
                inicfg, aimap, scheduler, Log(logservice),
                MQBroker(mq), strategies);



        while (scheduler.is_next()) {
            std::cout << "Timestamp: " << scheduler.get_next_time() << std::endl;
            scheduler.go_next();
            if (replay_done) break;
        }



    } catch (const std::exception &e) {
        std::cerr << "FATAL: ";
        print_exception(e);



    }


}
