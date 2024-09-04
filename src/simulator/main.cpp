#include <iostream>
#include <common/structured_ini.h>
#include <common/basic_exchange.h>
#include <common/basic_config.h>
#include <common/basic_log.h>
#include "sim_exchange.h"
#include "sim_scheduler.h"


using namespace quarkbot;

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
        Config acc_query = accounts[acc];
        ctx.query_accounts(ExchangeCredentials(),  acc_query, acc, [&](std::span<Account> accs){
            auto &lst = aimap._accounts[acc];
            for (Account x: accs) {
                lst.push_back(x);
            }
        });
    }
    for (auto instr: instruments.list_sections()) {
        Config instr_query = instruments[instr];
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

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <config.conf>" << std::endl;
        return 1;
    }
    try {
        std::string cfgfname = argv[1];
        auto inicfg = load_config(cfgfname);
        auto cfg = Config(std::make_shared<BasicConfig>(inicfg));

        bool replay_done = false;

        SimScheduler scheduler;
        auto logservice = std::make_shared<BasicLog>(std::cerr, ILog::Serverity::debug);
        auto context = std::make_shared<BasicExchangeContext>("simulator",
                scheduler.get_instance(), Network(), Log(logservice));
        context->init(std::make_unique<SimExchange>([&]{
                        replay_done = true;
                    }), cfg["simulator"]);

        AccountInstrumentMap aimap;
        configure_context(*context, cfg, aimap);


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
