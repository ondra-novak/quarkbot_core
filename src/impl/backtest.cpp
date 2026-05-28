#include "backtest.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "basic_coro/sync_await.hpp"
#include "ifc/context.hpp"
#include "ifc/execution_worker.hpp"
#include "ifc/tradable_instrument.hpp"
#include "impl/backtest_executor.hpp"
#include "impl/simexchange.hpp"
#include "impl/siminstrument.hpp"
#include "utils/init_with.hpp"
#include <memory>
#include <variant>

namespace quarkbot {

    Backtest::Backtest(std::shared_ptr<IBacktestDataSource> data_source,
            std::string account_name, 
            std::span<std::pair<std::string, Decimal> > wallet)
        :_exchange(std::make_shared<SimExchange>())
        ,_data(std::move(data_source))
        ,_account(_exchange->create_account(std::move(account_name),wallet))
        {
            _context.exec_worker = ExecutionWorker(_executor);
            _context.mode = StrategyMode::backtest;            
            _context.stop_signal = [this] -> awaitable<coro::void_type> {
                if (_stopped) return {};
                return [this](auto promise)->coro::prepared_coro{
                    if (!_stopped) {
                        _stop_awaiting.push_back(std::move(promise));
                        return {};
                    } else {
                        return promise();
                    }
                };
            };
        }

    void Backtest::add_instrument(IMarketInstrument::Info instrument_def) {
        auto instr = _exchange->create_instrument(instrument_def);        
        auto tinstr = instr->create_tradable_instrument(_account);
        _context.instruments.push_back(std::static_pointer_cast<ITradableInstrument>(std::move(tinstr)));
    }
    
    void Backtest::run(StrategyFragment fragment) {        
        _executor = BacktestExecutor::create();
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
        for (auto &x: _stop_awaiting) {x();}
        _stop_awaiting.clear();
        
        while (!pending.await_ready()) {
            _executor->flush_queue();
        }
        _executor->flush_queue();
        coro::sync_await(pending);
    }
}

