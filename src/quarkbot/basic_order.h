#pragma once

#include "instrument.h"
#include "account.h"
#include "order.h"

namespace quarkbot {

class AssociatedOrder: public IOrder::Null {
public:

    AssociatedOrder(Instrument instrument, Account account, std::string_view label)
        :_instrument(std::move(instrument)),_account(std::move(account)),label(label) {}

    virtual Instrument get_instrument() const override {return _instrument;}
    virtual Account get_account() const override {return _account;}
    virtual State get_state() const override {return State::associated;}
    virtual SerializedOrder to_binary() const override {return {};}
    virtual Origin get_origin() const override {return Origin::strategy;}
    virtual std::string get_label() const override {return label;}

protected:
    Instrument _instrument;
    Account _account;
    std::string label;
};

class ErrorOrder: public AssociatedOrder {
public:
    ErrorOrder(Instrument instrument, Account account, Setup setup, std::string_view label, Reason r)
        :AssociatedOrder(std::move(instrument),std::move(account), label)
        ,_r(std::move(r)),_setup(std::move(setup)) {}
    virtual State get_state() const override {return State::discarded;}
    virtual Reason get_reason() const override {return _r;}
    virtual Origin get_origin() const override {return Origin::strategy;}
    virtual const Setup &get_setup() const override {return _setup;}
protected:
    Reason _r;
    Setup _setup;
};

///Create error order (create_order cannot throw exception)
inline Order order_error(Instrument instrument, Account account, Order::Setup setup, std::string_view label, Order::Reason r) {
    return Order(std::make_shared<ErrorOrder>(
            std::move(instrument),
            std::move(account),
            std::move(setup),
            label,
            r));
}

class BasicOrder: public IOrder {
public:

    struct Status {
        std::string id = {};
        Decimal filled = 0;
        Decimal avg_price = 0;
        State state = State::sent;
        Reason reason = {};

        void apply_report(const Order::Report &report) {
            if (report.new_state) state = *report.new_state;
            if (report.reason) reason = *report.reason;
            if (report.filled_amount) filled = *report.filled_amount;
            if (report.avg_price) avg_price = *report.avg_price;
        }
    };


    BasicOrder(Instrument instrument, Account account, Setup setup, std::string_view label, Origin origin)
        :_instrument(std::move(instrument))
        ,_account(std::move(account))
        ,_setup(std::move(setup))
        ,_origin(std::move(origin))
        ,_label(label){}
    BasicOrder(Order replaced, Setup setup, std::string_view label, Origin origin)
        :_instrument(replaced.get_instrument())
        ,_account(replaced.get_account())
        ,_setup(std::move(setup))
        ,_origin(std::move(origin))
        ,_replaced(replaced.get_handle())
        ,_label(label) {}
    virtual State get_state() const override {
        return _status.state;
    }
    virtual Decimal get_avg_price() const override {
        return _status.avg_price;
    }
    virtual Decimal get_filled() const override {
        return _status.filled;
    }
    virtual const Setup &get_setup() const override {
        return _setup;
    }
    virtual Reason get_reason() const override {
        return _status.reason;
    }
    virtual Instrument get_instrument() const override {
        return _instrument;
    }
    ///Default serialization stores just order ID and order's label as exchange is responsible to recreate order from its records
    virtual SerializedOrder to_binary() const override {return {_status.id,_label};}
    virtual Origin get_origin() const override  {return _origin;}
    virtual Account get_account() const override {return _account;}
    virtual std::string get_id() const override {return _status.id;}

    Status &get_status() const {
        return _status;
    }

    virtual std::shared_ptr<const IOrder> get_replaced_order() const override {
        auto lk = _replaced.lock();
        if (lk) return lk;
        else return Order::null_instance_ptr;
    }

    static const BasicOrder &from_order(const Order &ord) {
        return dynamic_cast<const BasicOrder &>(*ord.get_handle());
    }

    virtual std::string get_label() const  override {return _label;}

    static void apply_report(const Order &ord, const Order::Report &rep) {
        from_order(ord).get_status().apply_report(rep);
    }

protected:

    Instrument _instrument;
    Account _account;
    Setup _setup;
    Origin _origin;
    std::weak_ptr<const IOrder> _replaced;
    std::string _label;

    mutable Status _status;


};



}
