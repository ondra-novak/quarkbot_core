#include "basic_exchange.h"

namespace quarkbot {

BasicExchangeContext::BasicExchangeContext(std::string label, Network ntw, Log log)
            :_label(std::move(label))
            ,_ntw(std::move(ntw))
            ,_log(std::move(log),"ex/{}",_label) {}

void BasicExchangeContext::init(std::unique_ptr<IExchange> svc, Config configuration) {
    this->_ptr = std::move(svc);
    this->_cfg = std::move(configuration);
    _ptr->init(this);
}

void BasicExchangeContext::subscribe(IEventTarget *target, MarketEventType sbstype, const Instrument &instrument) {
    std::lock_guard _(_mx);
    Subscription s{sbstype, instrument};
    auto v = _subscriptions[s];
    if (v.empty()) {
        _ptr->subscribe(s.type, s.i);
    }
    v.set(target);
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
            t->on_subscription_event(i, ev);
        }
    }
}

Order BasicExchangeContext::create_order(const Instrument &instrument,
        const Account &account, const Order::Setup &setup, std::string_view label) {
    return _ptr->create_order(instrument, account, setup, label);
}

Order BasicExchangeContext::create_order_replace(const Order &replace,
        const Order::Setup &setup, std::string_view label) {
    return _ptr->create_order_replace(replace, setup, label);
}

std::optional<IExchangeInfo::Icon> BasicExchangeContext::get_icon() const {
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




std::string BasicExchangeContext::get_label() const {
    return _label;
}



void BasicExchangeContext::update_account(IEventTarget *target, const Account &account) {
    std::lock_guard _(_mx);
    auto &lst = _account_update_waiting[account];
    if (lst.empty()) _ptr->update_account(account);
    lst.set(target);
}

void BasicExchangeContext::update_instrument(IEventTarget *target, const Instrument &instrument) {
    std::lock_guard _(_mx);
    auto &lst = _instrument_update_waiting[instrument];
    if (lst.empty()) _ptr->update_instrument(instrument);
    lst.set(target);
}

void BasicExchangeContext::object_updated(const Account &account, AsyncResult<void> st) {
    std::lock_guard _(_mx);
    auto &lst = _account_update_waiting[account];
    for (auto x: lst) {
        x->on_update(account, st);
    }
    lst.clear();
}

void BasicExchangeContext::object_updated(const Instrument &instrument, AsyncResult<void> st) {
    std::lock_guard _(_mx);
    auto &lst = _instrument_update_waiting[instrument];
    for (auto x: lst) {
        x->on_update(instrument, st);
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

void BasicExchangeContext::subscribe_order(IEventTarget *target, const Order &order) {
    std::lock_guard _(_mx);
    _orders.emplace(order, target);
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
void BasicExchangeContext::restore_orders(const Account &acc, std::span<SerializedOrder> orders, IExchange::RestoreOrdersCallback collector) {
    _ptr->restore_orders(acc, orders, std::move(collector));
}

void BasicExchangeContext::order_report(const Order &order, Order::Report report) {
    std::lock_guard _(_mx);
    auto iter= _orders.find(order);
    if (iter != _orders.end()) {
        bool is_done = report.new_state && IOrder::is_done(*report.new_state);
        iter->second->on_order_report(iter->first, std::move(report));
        if (is_done) {
            _orders.erase(iter);
        }
    }
}


BasicExchangeContext &BasicExchangeContext::from_exchange(ExchangeInfo ex) {
    const IExchangeInfo *e = ex.get_handle().get();
    const BasicExchangeContext *be = dynamic_cast<const BasicExchangeContext *>(e);
    return const_cast<BasicExchangeContext &>(*be);
}

ExchangeInfo BasicExchangeContext::get_exchange_info() const {
    return ExchangeInfo(shared_from_this());
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
    lst.set(target);
}


void BasicExchangeContext::object_updated(const Instrument &i, MarketEventType type, AsyncResult<MarketEvent> ev) {
    std::lock_guard _(_mx);
    Subscription sub{type,i};
    auto iter = _market_updates.find(sub);
    if (iter != _market_updates.end()) {
        auto lst = std::move(iter->second);
        _market_updates.erase(iter);
        for (auto x: lst) x->on_update(i, type, ev);
    }
}

void BasicExchangeContext::load_credentials(const Config &credential_config,
        std::string_view label, Function<void(ExchangeCredentials)> result) {
    _ptr->load_credentials(credential_config, label, std::move(result));

}

void BasicExchangeContext::query_accounts(const ExchangeCredentials &creds,
        std::string_view label, const Query &query,
        Function<void(std::span<Account>)> result) {
    _ptr->query_accounts(creds, label, query, std::move(result));
}

void BasicExchangeContext::query_instruments(const ExchangeCredentials &creds,
        const Query &query, std::string_view label,
        Function<void(std::span<Instrument>)> result) {
    _ptr->query_instruments(creds, query, label, std::move(result));
}

void BasicExchangeContext::query_instruments(const Query &query,
        std::string_view label, Function<void(std::span<Instrument>)> result) {
    _ptr->query_instruments(query, label,std::move(result));
}

const Config &BasicExchangeContext::get_config() const {
    return _cfg;
}

}
