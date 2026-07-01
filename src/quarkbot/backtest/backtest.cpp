#include "backtest.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "basic_coro/sync_await.hpp"
#include <quarkbot/context.hpp>
#include <quarkbot/execution_worker.hpp>
#include <quarkbot/tradable_instrument.hpp>
#include "backtest_executor.hpp"
#include "quarkbot/utils/init_with.hpp"
#include "simexchange.hpp"
#include "siminstrument.hpp"
#include <memory>
#include <variant>

namespace quarkbot {

    Backtest::Backtest(std::shared_ptr<IBacktestDataSource> data_source,
            std::string account_name, 
            std::span<std::pair<std::string, Decimal> > wallet)
        :_executor(BacktestExecutor::create())
        ,_exchange(std::make_shared<SimExchange>())
        ,_data(std::move(data_source))
        ,_account(_exchange->create_account(std::move(account_name),wallet))
        {
            _context.exec_worker = ExecutionWorker(_executor);
            _context.mode = StrategyMode::backtest;            
            _context.stop_signal  = stop_src.get_token();
        }

    void Backtest::add_instrument(IMarketInstrument::Info instrument_def) {
        auto instr = _exchange->add_instrument(instrument_def);        
        auto tinstr = instr->create_tradable_instrument(_account);
        _context.instruments.push_back(std::static_pointer_cast<ITradableInstrument>(std::move(tinstr)));
    }
    
    void Backtest::run(StrategyFragment fragment) {        
        auto event = _data->next_event();
        if (!event) return;
        _executor->set_time(event->time);
        auto pending = coro::awaitable<void>(std::move(fragment)).launch();
        
        while (event.has_value()) {
            _executor->set_time(event->time);
            if (std::holds_alternative<Quote>(event->payload)) {
                _exchange->on_event(event->instrument, std::get<Quote>(event->payload));
            } else if (std::holds_alternative<Trade>(event->payload)) {
                _exchange->on_event(event->instrument, std::get<Trade>(event->payload));
            }
            //copy elision
            std::destroy_at(&event);
            std::construct_at(&event, InitWith([&]{return _data->next_event();}));
        }        

        _stopped = true;
        stop_src.request_stop();
        
        while (!pending.await_ready()) {
            _executor->flush_queue();
        }
        _executor->flush_queue();
        coro::sync_await(pending);
    }
}

