
#include "quarkbot/backtest/config_backtest.hpp"
#include "quarkbot/backtest/config_datasource.hpp"
#include "quarkbot/backtest/config_instrument.hpp"
#include "quarkbot/backtest/ibacktest_debugger.hpp"
#include "quarkbot/common/strategy_config.hpp"
#include "quarkbot/config.hpp"
#include "quarkbot/utils/simple_ini.hpp"
#include "quarkbot/common/mem_storage.hpp"
#include "quarkbot/strategy_main.hpp"
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <ostream>
#include <print>
#include <thread>

namespace quarkbot {

    static std::atomic<bool> interrupted = {true};  //debugger starts interrupted



    static void simple_debugger(std::stop_token tkn, std::shared_ptr<IBacktestDebugger> dbg) {
        signal(SIGINT,[](int){
            interrupted.store(true);
        });
        std::println(std::cout, "Debugger active, type 'help' for help");
        while (!tkn.stop_requested()) {
            if (interrupted.exchange(false)) {
                dbg->set_running(false);
            }
            auto st = dbg->get_status();
            std::print(std::cout, "Running: {:%Y%m%d %H%M%S} - press Ctrl+C to interrupt\r", st.time);
            if (st.run_status == IBacktestDebugger::RunStatus::done) break;
            if (st.run_status == IBacktestDebugger::RunStatus::paused) {




            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }


    int entry_point(std::string_view program_name, std::span<const char *const > args, std::function<StrategyFragment(StrategyContext &&, const StrategyContext::Config &)> start_fn){
        //TODO - argv[1] = strategy config, argv[2] = backtest config + python config
    
        std::filesystem::path strategy_config_path;
        std::filesystem::path backtest_config_path;
        int pos = 0;
        bool dbg = false;

        for (std::string_view a: args ){
            if (a[0] == '-') {
                a.remove_prefix(1);
                if (a == "d") dbg = true;
                else {
                    std::println(std::cerr,"Unexpected switch -{}", a);
                    return 1;
                }
            } 
            else {
                switch (pos){
                    case 0: strategy_config_path = a;++pos;break;
                    case 1: backtest_config_path = a;++pos;break;
                    default: std::println(std::cerr, "Unexpected parameter: {}", a);return 1;
                }
            }
        }
        if (pos != 2) {
            std::println(std::cerr, "Usage: {} [-d] <strategy_config.ini> <backtest_config.ini>", program_name);
            std::println(std::cerr, "");
            std::println(std::cerr, "-d     Start with debugger");
            return 1;
        }

        BacktestConfig cfg;

        try {
            cfg = BacktestConfig::load(backtest_config_path);
            cfg.configure_log();
        } catch (const std::exception &e) {
            std::print(std::cerr, "Failed to initialize backtest config {}, exception {}\n", backtest_config_path.string(), e.what());
            return 2;
        }

        try {

            BacktestEnv bt("backtest-account", cfg.configure_wallet(), configure_instruments(backtest_config_path), cfg.configure_simulation());
            auto data_source = configure_datasources(backtest_config_path);
            std::jthread dbgthr;

            StrategyContext ctx;
            ctx.storage = MemStorage::create(MemStorage::no_history);        
            ctx.config = load_strategy_config(strategy_config_path);
            bt.add_strategy([start_fn = std::move(start_fn), config = cfg.as_config()](StrategyContext &&context){
                return start_fn(std::move(context), config);
            },  std::move(ctx));

            if (dbg) dbgthr = std::jthread(simple_debugger, bt.enable_debugger());
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