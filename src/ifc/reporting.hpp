#pragma once

#include "ifc/context.hpp"
#include "ifc/defs.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/tradable_instrument.hpp"
#include "ifc/order.hpp"
#include "utils/json.hpp"
#include <stop_token>
namespace quarkbot {

    class IReporter{
    public:
        virtual ~IReporter() = default;
        ///report order (done automatically via attach)
        virtual void report_order(const Order &ord, const Order::Update &update) = 0;
        ///report quote (done automatically via attach)
        virtual void report_quote(PMarketInstrument instrument, const Quote &qt)= 0;
        ///report trade (done automatically via attach)
        virtual void report_trade(PMarketInstrument instrument, const Trade &qt)= 0;
        ///report user value
        /**
        @param name name of variable
        @param value value (as json)
         */         
        virtual void report_value(std::string_view name, const Json value) = 0;

        ///attach reporter to tradable instrument;
        /**
        @param reporter reporter instance
        @param instrument tradable instrument
        @param stp stop token - can be used to stop reporting when no longer needed
        @param worker execution worker where reporting is running - if not definned, current worker is used

        @note you need #include "reporting_orders.hpp"
         */
        static void attach(PReporter reporter, PTradableInstrument instrument, std::stop_token stp = {}, PExecutionWorker worker = {});
    };




}