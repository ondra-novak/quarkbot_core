#pragma once

#include "ifc/account.hpp"
#include "ifc/backtest_data_source.hpp"
#include "ifc/context.hpp"
#include "ifc/defs.hpp"
#include "impl/backtest_executor.hpp"
#include "simexchange.hpp"
#include <memory>

namespace quarkbot {

    class Backtest {
    public:
        Backtest(std::shared_ptr<IBacktestDataSource> data_source,
            std::string account_name, 
            std::span<std::pair<UnderlyingCurrency, Decimal> > wallet
        );

        SimExchange &get_exchange() {return *_exchange.get();}

        StrategyContext &get_context() {return _context;}

        void add_instrument(IMarketInstrument::Info instrument_def);

        void run(StrategyFragment fragment);
        
    protected:

        class Context: public StrategyContext {
        public:
            using StrategyContext::on_stop_awaitable;
        };

        Context _context;
        std::shared_ptr<SimExchange> _exchange;
        std::shared_ptr<IBacktestDataSource> _data;
        std::shared_ptr<BacktestExecutor> _executor;
        PAccount _account;

    };


    std::shared_ptr<SimExchange> create_backtest_exchange();

    void run_backtest(std::shared_ptr<SimExchange> exchange, std::shared_ptr<IBacktestDataSource> data, StrategyFragment strategy_entrypoint);

    //todo reporting

}