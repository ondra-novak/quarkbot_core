#include "backtest.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "basic_coro/sync_await.hpp"
#include <filesystem>
#include <quarkbot/context.hpp>
#include <quarkbot/execution_worker.hpp>
#include <quarkbot/tradable_instrument.hpp>
#include "backtest_executor.hpp"
#include "quarkbot/selector.hpp"
#include "quarkbot/types.hpp"
#include "quarkbot/utils/init_with.hpp"
#include "simaccount.hpp"
#include "simexchange.hpp"
#include "simexec_report_csv.hpp"
#include "simexecutor.hpp"
#include "siminstrument.hpp"
#include <memory>
#include <variant>

namespace quarkbot {

    bool run_backtest(std::shared_ptr<BacktestExecutor> executor, 
                  std::shared_ptr<SimExchange> exchange,
                  std::shared_ptr<SimAccount> account,
                  BacktestDataSource source,std::stop_source stop_source) {

        auto token = stop_source.get_token();

            
        BacktestEvent event;
        if (!source(event)) return false;
        do {
            executor->set_time(event.time);
            selector(event.data, [&](const CustomBacktestEvent &ev){
                ev(event.symbol);
            }, [&](const CustomBacktestEventOnExchange &ev){
                ev(event.symbol, exchange.get());
            }, [&](const CustomBacktestEventOnAccount &ev){
                ev(event.symbol, account.get());                
            }, [&](const auto &ev){
                exchange->on_event(event.symbol, ev);
            });
            executor->flush_queue();
        } while (source(event) && !token.stop_requested());

        stop_source.request_stop();    
        executor->flush_queue();    
        return true;
    }


BacktestEnv::BacktestEnv(std::string_view account_name,
            std::span<const WalletInitItem> wallet,
            std::span<const InstrumentDescription> instruments,
            const SimulationParams &sim_params)
        :_worker(nullptr), _exchange(nullptr) ,_account(nullptr)
        {
    
        auto wrk = BacktestExecutor::create();
        auto ex = std::make_shared<SimExchange>();
        auto acc = ex->create_account(std::string(account_name), wallet);
        for (auto &info: instruments) {
            ex->add_instrument(info);
        }
        if (std::holds_alternative<ReportSink>(sim_params.reporter)) {
            ex->set_reporter(std::get<ReportSink>(sim_params.reporter));
        } else if (std::holds_alternative<std::filesystem::path>(sim_params.reporter)) {
            auto &path = std::get<std::filesystem::path>(sim_params.reporter);
            ex->set_reporter( open_report(path, ExecutionWorker(wrk)));
        }
        ex->set_slippage(sim_params.slippage);
        ex->set_latency(sim_params.latency);
        _exchange = Exchange(ex);
        _account = Account(acc);
        _worker = ExecutionWorker(wrk);
    }

    void BacktestEnv::init_context_basic(std::span<const std::string_view> instruments, StrategyContext &ctx) {
        for (auto &n: instruments) {
            auto instr = _exchange.create_instrument(n, InstrumentType::contract);
            ctx.instruments.push_back(instr.create_tradable_instrument(_account));
        }
        ctx.exec_worker = _worker;
        ctx.stop_signal = stop_src.get_token();        
    }

    void BacktestEnv::init_context_basic(StrategyContext &ctx) {
        auto all = _exchange.get_market_instruments();
        for (auto &x: all) {
            ctx.instruments.push_back(x.create_tradable_instrument(_account));
        }
    
        ctx.exec_worker = _worker;
        ctx.stop_signal = stop_src.get_token();        
        ctx.mode = StrategyMode::backtest;        
    }

    bool BacktestEnv::run(BacktestDataSource data_source) {
        return run_backtest(std::static_pointer_cast<BacktestExecutor>(_worker.get_handle()),
                    std::static_pointer_cast<SimExchange>(_exchange.get_handle()),
                    std::static_pointer_cast<SimAccount>(_account.get_handle()),
                    std::move(data_source),stop_src);
    }

}