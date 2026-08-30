
#include "entry_point.hpp"
#include "basic_coro/exceptions.hpp"
#include "config_backtest.hpp"
#include "config_datasource.hpp"
#include "config_instrument.hpp"
#include "ibacktest_debugger.hpp"
#include "quarkbot/backtest/json_report.hpp"
#include "quarkbot/json/json.hpp"
#include "quarkbot/log.hpp"
#include "quarkbot/utils/cli_options.hpp"
#include "simple_stdio_debugger.hpp"
#include "../common/strategy_config.hpp"
#include "../common/mem_storage.hpp"
#include "quarkbot/utils/simple_ini.hpp"
#include "quarkbot/strategy_main.hpp"
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ostream>
#include <print>
#include <thread>

namespace quarkbot {

    struct DefaultCLI {
        std::filesystem::path strategy_config_path = {};
        std::filesystem::path backtest_config_path = {};
        std::optional<std::filesystem::path> jsonl_output_path = {};
        unsigned int json_interval = 5;
        bool jsn = false;
        bool dbg = false;        
        bool help = false;
    };


    constexpr cli::OptionDef<DefaultCLI> cli_options[] = {
        {&DefaultCLI::strategy_config_path,{},{},"strategy_config.ini","Specifies configuration (INI) of the strategy",true},
        {&DefaultCLI::backtest_config_path,{},{},"backtest_config.ini","Specifies configuration (INI) of the backtest runner",true},
        {&DefaultCLI::dbg, 'd', "debugger", {}, "Enable console debugger"},
        {&DefaultCLI::jsn,'j',"jsonl", {}, "Generate JSONL report"},
        {&DefaultCLI::jsonl_output_path,'f',"file", {}, "Specify output file (default is stdout)"},
        {&DefaultCLI::json_interval,'i',"interval", {}, "Specify JSONL chart price interval in minutes (default: 5)"},
        {&DefaultCLI::help, 'h',"help", {}, "Display help"}
    };



    int entry_point(std::filesystem::path program_name, std::span<const char *const > args, std::function<StrategyFragment(StrategyContext &&)> start_fn){
        std::vector<std::string_view> argsv(args.begin(), args.end());
        DefaultCLI pargs;
        cli::CLIParser<DefaultCLI> parser({std::begin(cli_options), std::end(cli_options)}, std::filesystem::current_path());
        auto r = parser.parse_options<char>(argsv,pargs);
        if (pargs.help) {
            std::string h = parser.generate_help();
            std::println("Quarkbot Backtester");
            std::println("");
            std::println("Usage:\n\t{}{}", program_name.stem().string(),h);
            std::println("\nCopyright (c) 2026 Ondřej Novak. MIT Licence.");
            return 0;
        }
        if (r) {
            std::println(stderr,"Invalid arguments `{}` : {}",parser.nth_option_to_string(r->nth_option),to_string(r->errtype));
            std::println(stderr, "Use -h for help");
            return 1;
        }


        std::optional<BacktestJsonReportSetup> json_setup;
        std::optional<std::ofstream> json_out;
        if (pargs.jsn) {
            if (pargs.jsonl_output_path) {
                json_out.emplace(*pargs.jsonl_output_path, std::ios::trunc|std::ios::out);
                json_setup.emplace(BacktestJsonReportSetup{
                    pargs.json_interval, *json_out
                });
            } else {
                json_setup.emplace(BacktestJsonReportSetup{
                    pargs.json_interval, std::cout
                });
            }
        }

        return backtest_entry_point({
            std::move(pargs.strategy_config_path),
            std::move(pargs.backtest_config_path),
            std::move(start_fn),
            pargs.dbg?get_simple_stdio_debugger():nullptr,
            {},
            json_setup
        });
    }

    int backtest_entry_point(BacktestStartParams params) {

        BacktestConfig cfg;

        try {
            cfg = BacktestConfig::load(params.backtest_config);
            cfg.configure_log();
        } catch (const std::exception &e) {
            std::print(std::cerr, "Failed to initialize backtest config {}, exception {}\n", params.backtest_config.string(), e.what());
            return 2;
        }

        try {
            auto simcfg = params.json_report?cfg.configure_simulation_no_report():cfg.configure_simulation();
            BacktestEnv bt("backtest-account", cfg.configure_wallet(), configure_instruments(params.backtest_config), simcfg);
            auto data_source = configure_datasources(params.backtest_config);            

            std::optional<JsonReport> jsnrpt;


            StrategyContext ctx;
            ctx.storage = MemStorage::create(MemStorage::no_history);        
            ctx.config = load_strategy_config(params.strategy_config);

            if (params.json_report) {
                jsnrpt.emplace(params.json_report->output);
                jsnrpt->attach_exchange(bt.get_exchange(), bt.get_stop_token(), params.json_report->interval);
                jsnrpt->attach_storage(ctx.storage);
            }

            if (params.init_env) params.init_env(cfg.as_config());
            if (params.debugger) params.debugger(bt.enable_debugger(), ctx.storage);
            bt.add_strategy([start_fn = std::move(params.start_fn)](StrategyContext &&context){
                return start_fn(std::move(context));
            },  std::move(ctx));


            logInfo("Backtest started");
            bt.run(std::move(data_source));
            logInfo("Backtest ended");        

        } catch (const std::exception &e) {
            logFatal("Exception: {}",e.what());
            return 3;
        }

        return 0;
        

    }

}