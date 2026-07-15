#pragma once

#include "../order_defs.hpp"
#include <cstddef>
namespace quarkbot {

    class Order;

    class IRiskController {
    public:

        virtual ~IRiskController() = default;
        ///Called when order is placed
        /**
            @param instrument on which instrument the order is placed
            @param parameters order parameters
            @param replaced reference to order being replaced (or nullptr)
            @retval nullopt order will be placed
            @retval OrderRejectionWithText order will be rejected with a reason

            @note implementation must be MT safe
        */
        virtual std::optional<OrderRejectionWithText> pre_trade_check(const Order &order) = 0;
           

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

    class IRiskController::Null final: public IRiskController {
    public:
        virtual std::optional<OrderRejectionWithText> pre_trade_check(const Order &) override {return std::nullopt;}
        virtual void on_order_event(const Order &, const Fill &) override {}
        virtual void on_order_event(const Order &, OrderStatus ) override {};
    };



};