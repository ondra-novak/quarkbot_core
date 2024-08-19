#include "basic_exchange.h"

namespace trading_api {

BasicExchangeContext::BasicExchangeContext(std::string label, Network ntw, Log log)
            :_label(std::move(label))
            ,_ntw(std::move(ntw))
            ,_log(std::move(log),"ex/{}",_label) {}

void BasicExchangeContext::init(std::unique_ptr<IExchangeService> svc, Config configuration) {
    this->_ptr = std::move(svc);
    _ptr->init(this, configuration);
}

void BasicExchangeContext::subscribe(IEventTarget *target, MarketEventType sbstype, const Instrument &instrument) {
    std::lock_guard _(_mx);
    Subscription s{sbstype, instrument};
    auto v = _subscriptions[s];
    if (v.empty()) {
        _ptr->subscribe(s.type, s.i);
    }
    v.push_back(target);
}

void BasicExchangeContext::unsubscribe(IEventTarget *target, MarketEventType sbstype, const Instrument &instrument) {
    std::lock_guard _(_mx);
    Subscription s{sbstype, instrument};
    auto v = _subscriptions[s];
    v.erase(std::remove(v.begin(), v.end(), target), v.end());
    if (v.empty()) {
        _ptr->unsubscribe(s.type, s.i);
        _subscriptions.erase(s);
    }
}

void BasicExchangeContext::income_data(const Instrument &i, const MarketEvent &ev) {
    std::lock_guard _(_mx);
    auto iter = _subscriptions.find({ev.get_type(),i});
    if (iter != _subscriptions.end()) {
        for (auto t: iter->second) {
            t->on_event(i, ev);
        }
    }
}

Order BasicExchangeContext::create_order(const Instrument &instrument,
        const Account &account, const Order::Setup &setup) {
    _ptr->create_order(instrument, account, setup);
}

Order BasicExchangeContext::create_order_replace(const Order &replace,
        const Order::Setup &setup, bool amend) {
    _ptr->create_order_replace(replace, setup, amend);
}

std::optional<IExchange::Icon> BasicExchangeContext::get_icon() const {
    return _ptr->get_icon();
}

std::string BasicExchangeContext::get_name() const {
    return _ptr->get_name();
}

std::string BasicExchangeContext::get_id() const {
    return _ptr->get_id();
}

void BasicExchangeContext::order_apply_report(const Order &order,
        const Order::Report &report) {
    _ptr->order_apply_report(order, report);
}

void BasicExchangeContext::order_apply_fill(const Order &order,
        const Fill &fill) {
    _ptr->order_apply_fill(order, fill);
}



std::string BasicExchangeContext::get_label() const {
    return _label;
}

void BasicExchangeContext::query_instruments(std::string_view query,
        std::string_view label, Function<void(Instrument)> cb) {
    _ptr->query_instruments(query, label, std::move(cb));

}

void BasicExchangeContext::query_accounts(std::string_view identity,
        std::string_view query,
        std::string_view label, Function<void(Account)> cb) {
    _ptr->query_accounts(identity, query, label, std::move(cb));
}



void BasicExchangeContext::update_ticker(IEventTarget *target, const Instrument &instrument) {
    std::lock_guard _(_mx);
    //todo
}

void BasicExchangeContext::update_account(IEventTarget *target, const Account &account) {
    std::lock_guard _(_mx);
    auto &lst = _account_update_waiting[account];
    if (lst.empty()) {
        _ptr->update_account(account);
    }
    lst.push_back(target);
}

void BasicExchangeContext::update_instrument(IEventTarget *target, const Instrument &instrument) {
    std::lock_guard _(_mx);
    auto &lst = _instrument_update_waiting[instrument];
    if (lst.empty()) {
        _ptr->update_instrument(instrument);
    }
    lst.push_back(target);
}

void BasicExchangeContext::object_updated(const Account &account, AsyncStatus st) {
    std::lock_guard _(_mx);
    auto &lst = _account_update_waiting[account];
    for (auto x: lst) {
        x->on_event(account, st);
    }
    lst.clear();
}

void BasicExchangeContext::object_updated(const Instrument &instrument, AsyncStatus st) {
    std::lock_guard _(_mx);
    auto &lst = _instrument_update_waiting[instrument];
    for (auto x: lst) {
        x->on_event(instrument, st);
    }
    lst.clear();
}

void BasicExchangeContext::disconnect(const IEventTarget *target) {
    std::lock_guard _(_mx);
    for (auto iter = _subscriptions.begin(); iter != _subscriptions.end();) {
        auto &lst = iter->second;
        lst.erase(std::remove(lst.begin(), lst.end(), target), lst.end());
    }
    for (auto &[k,lst]: _account_update_waiting) {
        lst.erase(std::remove(lst.begin(), lst.end(), target), lst.end());
    }
    for (auto &[k,lst]: _instrument_update_waiting) {
        lst.erase(std::remove(lst.begin(), lst.end(), target), lst.end());
    }
}

void BasicExchangeContext::batch_place(IEventTarget *target, std::span<Order> orders) {
    std::lock_guard _(_mx);
    for (const auto &ord: orders) {
        _orders.emplace(ord, target);
    }
    _ptr->batch_place(orders);
}

void BasicExchangeContext::batch_cancel(std::span<Order> orders) {
    _ptr->batch_cancel(orders);
}
void BasicExchangeContext::restore_orders(IEventTarget *target, std::span<SerializedOrder> orders) {
    _ptr->restore_orders(target, orders);
}

void BasicExchangeContext::order_state_changed(const Order &order, const Order::Report &report) {
    std::lock_guard _(_mx);
    auto iter= _orders.find(order);
    if (iter != _orders.end()) {
        iter->second->on_event(iter->first, report);
        if (IOrder::is_done(report.new_state)) {
            _orders.erase(iter);
        }
    }
}

void BasicExchangeContext::order_fill(const Order &order, const Fill &fill) {
    std::lock_guard _(_mx);
    auto iter= _orders.find(order);
    if (iter != _orders.end()) {
        iter->second->on_event(iter->first, fill);
    }

}

void BasicExchangeContext::order_restore(void *target, const Order &order) {
    _orders.emplace(order, reinterpret_cast<IEventTarget *>(target));

}

BasicExchangeContext &BasicExchangeContext::from_exchange(Exchange ex) {
    const IExchange *e = ex.get_handle().get();
    const BasicExchangeContext *be = dynamic_cast<const BasicExchangeContext *>(e);
    return const_cast<BasicExchangeContext &>(*be);
}

Exchange BasicExchangeContext::get_exchange() const {
    return Exchange(shared_from_this());
}

Log BasicExchangeContext::get_log() const {
    return _log;
}
Network BasicExchangeContext::get_network() const {
    return _ntw;
}

void BasicExchangeContext::update_market(IEventTarget *target, const Instrument &i, MarketEventType type)
{
    std::lock_guard _(_mx);
    auto &lst = _market_updates[Subscription{type,i}];
    if (lst.empty()) _ptr->update_market(i, type);
    lst.push_back(target);
}


void BasicExchangeContext::set_api_key(std::string_view name, const Config &api_key_config) {
    _ptr->set_api_key(name, api_key_config);
}

void BasicExchangeContext::unset_api_key(std::string_view name) {
    _ptr->unset_api_key(name);
}

void BasicExchangeContext::object_updated(const Instrument &i, AsyncStatus st, MarketEvent ev) {
    std::lock_guard _(_mx);
    Subscription sub{ev.get_type(),i};
    auto iter = _market_updates.find(sub);
    if (iter != _market_updates.end()) {
        auto lst = std::move(iter->second);
        _market_updates.erase(iter);
        for (auto x: lst) x->on_event(i, st, ev);
    }
}

}
