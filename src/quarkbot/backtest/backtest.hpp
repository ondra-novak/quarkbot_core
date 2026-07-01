#pragma once

#include "basic_coro/concepts.hpp"
#include "quarkbot/account.hpp"
#include "quarkbot/backtest_data_source.hpp"
#include "quarkbot/context.hpp"
#include "quarkbot/defs.hpp"
#include "backtest_executor.hpp"
#include "simexchange.hpp"
#include <memory>

namespace quarkbot {

    class Backtest {
    public:
        Backtest(std::shared_ptr<IBacktestDataSource> data_source,
            std::string account_name, 
            std::span<std::pair<std::string, Decimal> > wallet
        );

        SimExchange &get_exchange() {return *_exchange.get();}

        StrategyContext get_context() {return _context;}

        void add_instrument(IMarketInstrument::Info instrument_def);

        void run(StrategyFragment fragment);
        
    protected:


        StrategyContext _context;
        std::shared_ptr<BacktestExecutor> _executor;
        std::shared_ptr<SimExchange> _exchange;
        std::shared_ptr<IBacktestDataSource> _data;
        PAccount _account;
        bool _stopped = false;
        std::stop_source stop_src;

    };


    std::shared_ptr<SimExchange> create_backtest_exchange();

    void run_backtest(std::shared_ptr<SimExchange> exchange, std::shared_ptr<IBacktestDataSource> data, StrategyFragment strategy_entrypoint);

    //todo reporting

}