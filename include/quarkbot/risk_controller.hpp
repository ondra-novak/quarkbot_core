#pragma once

#include "abstract/iriskcontroller.hpp"
#include "quarkbot/order_defs.hpp"
#include "utils/wrapper.hpp"
#include <type_traits>
namespace quarkbot {
    
    class RiskController: public Wrapper<IRiskController>{
    public:

        using Wrapper<IRiskController>::Wrapper;
        using CheckResult = IRiskControl::CheckResult;

        template<typename T, typename ... Args>
        requires(std::is_base_of_v<IRiskController, T> && std::is_constructible_v<T, Args...>)
        static RiskController create(Args && ... args) {
            return RiskController(std::make_shared<T>(std::forward<Args>(args)...));
        }

        CheckResult pre_trade_check(const Order &order) {
                        return _ptr->pre_trade_check(order);
                    }

        void on_order_event(const Order &order, const Fill &fill) {
            _ptr->on_order_event(order, fill);
        }

        void on_order_event(const Order &order, OrderStatus new_status) {
            _ptr->on_order_event(order, new_status);
        }

    };

}