#pragma once

#include "quarkbot/abstract/iaccount.hpp"
#include "quarkbot/abstract/iriskcontroller.hpp"
#include "quarkbot/risk_controller.hpp"
#include <shared_mutex>
namespace quarkbot {
 
class AccountBase: public IAccount {
public:
    virtual RiskController set_risk_controller(RiskController) override;
    
    virtual CheckResult pre_trade_check(const Order &order) override {
        std::shared_lock _(_mx);
        return _risk.pre_trade_check(order);
    }
    virtual void on_order_event(const Order &order, const Fill &fill) override {
        std::shared_lock _(_mx);
        return _risk.on_order_event(order, fill);
    }
    virtual void on_order_event(const Order &order, OrderStatus new_status) override {
        std::shared_lock _(_mx);
        return _risk.on_order_event(order, new_status);
    }
protected:
    std::shared_mutex _mx;
    RiskController _risk;
};    

}