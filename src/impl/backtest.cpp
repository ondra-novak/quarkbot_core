#include "backtest.hpp"
#include "ifc/context.hpp"
#include "ifc/market_events.hpp"
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
            std::span<std::pair<UnderlyingCurrency, Decimal> > wallet)
        :_exchange(std::make_shared<SimExchange>())
        ,_data(std::move(data_source))
        ,_executor(std::make_shared<BacktestExecutor>())
        ,_account(_exchange->create_account(std::move(account_name),wallet))
        {
            _context.exec_worker = _executor;
            _context.scheduler = _executor;
            _context.mode = StrategyMode::backtest;
        }

    void Backtest::add_instrument(IMarketInstrument::Info instrument_def) {
        auto instr = std::make_shared<SimInstrument>(std::move(instrument_def),_exchange);
        auto tinstr = instr->create_tradable_instrument(_account).get();
        _context.instruments.push_back(std::static_pointer_cast<ITradableInstrument>(std::move(tinstr)));
    }
    
    void Backtest::run(StrategyFragment fragment) {
        auto event = _data->next_event();
        if (!event) return;
        _executor->set_time(event->time);
        _executor->run(std::move(fragment));
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
    }
}

