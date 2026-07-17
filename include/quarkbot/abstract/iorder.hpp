#pragma once

#include "../defs.hpp"
#include "../order_defs.hpp"
#include "../types.hpp"
namespace quarkbot {

    
///abstract order
class IOrder {
public:

    enum class RcvStatus {
        ///registered for await
        awaiting,
        ///order is done
        done,
        ///there are updates
        updates,
        ///already pending await (error)
        already_pending,
        ///no data, need await 
        need_await

    };

    ///Receive report without registering awaitable callback
    virtual RcvStatus receive_report(OrderReport &rpt) = 0;
    ///Receive report or register awaitable callback, if not avaiable yet
    virtual RcvStatus receive_report(OrderReport &rpt, awaitable<bool>::result  &promise) = 0;
    ///get replaced order (if still exists)
    virtual std::weak_ptr<IOrder> get_replaced_order() const = 0;
    ///get order parameters
    virtual const OrderParameters &get_parameters() const =0;
    ///cancel order
    virtual void cancel() = 0;
    ///get creation time
    virtual std::chrono::system_clock::time_point get_creation_time() const = 0;
    ///get instrument
    virtual const PTradableInstrument &get_instrument() const = 0;

    virtual ~IOrder() = default;

    class Null;
};

class IOrder::Null final: public IOrder {
public:
    static constexpr auto params = OrderParameters{};
    virtual RcvStatus receive_report(OrderReport &) override {
        return RcvStatus::done;
    }
    virtual RcvStatus receive_report(OrderReport &, awaitable<bool>::result & ) override {
        return RcvStatus::done;
    }
    virtual std::weak_ptr<IOrder> get_replaced_order() const override {
        return {};
    }
    virtual void cancel() override {}
    virtual const OrderParameters &get_parameters() const  override {
        return params;
    }
    virtual std::chrono::system_clock::time_point get_creation_time() const override {
        return {};
    }
    virtual const PTradableInstrument &get_instrument() const override {
        throw UninitializedException();
    }
    


};

}