#pragma once

#include "../order_defs.hpp"
#include <cstddef>
namespace quarkbot {

    class Order;

    ///The object controlling risk, just interface, can be anything
    class IRiskControl {
    public:

        struct CheckResult {
            //set true to allow this order, false to reject order
            bool ok;
            //if false is set, specify reject reason
            OrderRejectionReason rej_reason = OrderRejectionReason::too_risky;
            //you can also add some message 
            std::string rej_message = {};
        };

        virtual ~IRiskControl() = default;
        ///Called when order is placed
        /**
            @param instrument on which instrument the order is placed
            @param parameters order parameters
            @param replaced reference to order being replaced (or nullptr)
            @retval nullopt order will be placed
            @retval OrderRejectionWithText order will be rejected with a reason

            @note implementation must be MT safe
        */
        virtual CheckResult pre_trade_check(const Order &order) = 0;
           

        ///Called when event - fill
        /**
            @param order order - access to internal data, you can receive parameters, order id, etc
            @param fill fill 
            
            @note implementation must be MT safe
         */
        virtual void on_order_event(const Order &order, const Fill &fill) = 0;
        ///Called when event - order status
        /**
            @param order order - access to internal data, you can receive parameters, order id, etc
            @param new_status new status

            @note implementation must be MT safe
         */
        virtual void on_order_event(const Order &order, OrderStatus new_status) = 0;

        class Null;
    };

    class IRiskControl::Null final: public IRiskControl {
    public:
        virtual CheckResult pre_trade_check(const Order &) override {return {false, OrderRejectionReason::not_tradable};}
        virtual void on_order_event(const Order &, const Fill &) override {}
        virtual void on_order_event(const Order &, OrderStatus ) override {};
    };

    ///The standalone object to control and define risk rules
    class IRiskController : public IRiskControl {
    public:
        class Null;
        //empty, just alias

    };

    class IRiskController::Null: public IRiskController {
    public:
        virtual CheckResult pre_trade_check(const Order &) override {return {false, OrderRejectionReason::not_tradable};}
        virtual void on_order_event(const Order &, const Fill &) override {}
        virtual void on_order_event(const Order &, OrderStatus ) override {};
    };

    


};