#include "basic_context.h"
#include "../trading_ifc/basic_order.h"
#include <list>
#include <future>
#include <vector>

namespace trading_api {



BasicContext::~BasicContext() {
    _mq.unsubscribe_all(this);
    _scheduler(Timestamp{}, [](auto){}, this);
    for (const auto &[e,_]: _exchanges) {
        BasicExchangeContext::from_exchange(e).disconnect(this);
    }

}

class OrderCollector: public IExchange::IRestoredOrderCollector {
public:
    virtual void order(Order ord, Order::Report report) {
        orders[ord].report = report;
    }
    virtual void fill(Order ord, Fills fill) {
        auto &f = orders[ord].fills;
        f.insert(f.end(), fill.begin(), fill.end());
    }
    virtual void ok() {
        promise.set_value();
    }
    virtual void error(){
        promise.set_exception(std::current_exception());
    }

    struct Status {
        Order::Report report;
        Fills fills;
    };
    std::unordered_map<Order, Status, Order::Hasher> orders;
    std::promise<void> promise;
};


void BasicContext::init(std::unique_ptr<IStrategy> strategy,
        std::vector<Account> accounts,
        std::vector<Instrument> instruments,
        Config config) {
    _strategy = std::move(strategy);
    _accounts = std::move(accounts);
    _instruments = std::move(instruments);
    _config = std::move(config);

    for (const auto &a: _accounts) {
        _exchanges.emplace(a.get_exchange(),Batches{});
    }
    for (const auto &i: _instruments) {
        _exchanges.emplace(i.get_exchange(),Batches{});
    }
    _strategy->on_init(this);
    std::list<OrderCollector> collected_orders;
    std::list<std::future<void> > collected_futures;
    for (const auto &a: _accounts) {
        auto ex = a.get_exchange();
        auto &ctx = BasicExchangeContext::from_exchange(ex);
        auto orders = _storage->load_open_orders(a);
        if (!orders.empty()) {
            auto &c = collected_orders.emplace_back();
            ctx.restore_orders(a, orders, c);
        }
    }
    std::transform(collected_orders.begin(), collected_orders.end(),
            std::back_inserter(collected_futures), [](OrderCollector &x) {
       return x.promise.get_future();
    });
    for (auto &x: collected_futures) x.wait();
    for (auto &x: collected_futures) x.get();

    EvRestoreOrders restored_orders{this,{}};
    for (auto &x: collected_orders) {
        for (auto &[k,v]: x.orders) restored_orders.orders.push_back(k);
    }
    if (!restored_orders.orders.empty()) {
        _queue.push(std::move(restored_orders));
    }
    _queue.push(EvStart{this});
    for (auto &x: collected_orders) {
        for (auto &[k,v]: x.orders) {
            _queue.push(EvOrderReport{this,k, std::move(v.report), std::move(v.fills)});
        }
    }
    notify_queue();
}


void BasicContext::on_event(const Instrument &i, const MarketEvent &event) {
    std::lock_guard _(_queue_mx);
    if (can_collapse(event.get_type())) {
        MarketEventItem itm{i, event};
        auto iter = std::find(_mequeue.begin(), _mequeue.end(), itm);
        if (iter == _mequeue.end()) {
            _mequeue.push_back(itm);
            notify_queue();
        } else {
            iter->event = std::move(itm.event);
        }
    } else {
        _queue.push(EvMarketEventItem{this,i,event});
        notify_queue();
    }
}

void BasicContext::on_event(const Order &order,Order::Report report, Fills fills) {
    std::lock_guard _(_queue_mx);
    _queue.push(EvOrderReport{this, order, std::move(report), std::move(fills)});
    notify_queue();
}




void BasicContext::on_event(const Instrument &i, AsyncResult<void> st) {
    std::lock_guard _(_queue_mx);
    _queue.push(EvUpdateInstrument{this, i, std::move(st)});
    notify_queue();

}



void BasicContext::on_event(const Account &a, AsyncResult<void> st ) {
    std::lock_guard _(_queue_mx);
    _queue.push(EvUpdateAccount{this, a, std::move(st)});
    notify_queue();
}

void BasicContext::on_event(const Instrument &i, MarketEventType type, AsyncResult<MarketEvent> event) {
    std::lock_guard _(_queue_mx);
    _queue.push(EvUpdateMarket{this, i, type, std::move(event)});
     notify_queue();
}



void BasicContext::notify_queue() {
    Timestamp tp = Timestamp::max();
    if (_queue.empty()) {
        if (_timed_queue.empty()) {
            return;
        }
        tp = _timed_queue.front().tp;
    } else {
        tp = Timestamp::min();
    }
    if (_scheduled_time > tp) {
        _scheduled_time = tp;
        _scheduler(tp,[this](auto tp){on_scheduler(tp);}, this);
    }

}


void BasicContext::subscribe(MarketEventType type, const Instrument &i) {
    BasicExchangeContext::from_exchange(i.get_exchange()).subscribe(this, type, i);
}


Fills BasicContext::get_fills(std::size_t limit, std::string_view filter) const {
    return _storage->load_fills(limit, filter);
}
Fills BasicContext::get_fills(Timestamp tp, std::string_view filter) const {
    return _storage->load_fills(tp, filter);

}


Order BasicContext::place(const Instrument &instrument, const Account &account, const Order::Setup &setup, std::string_view label) {

    ExchangeInfo ex = account.get_exchange();
    BasicExchangeContext &e = BasicExchangeContext::from_exchange(ex);
    auto ord = e.create_order(instrument, account, setup, label);
    if (!ord.discarded()) {
        _exchanges[ex]._batch_place.push_back(ord);
    }
    return ord;
}


Order BasicContext::replace(const Order &order, const Order::Setup &setup, std::string_view label) {
    ExchangeInfo ex = order.get_account().get_exchange();
    BasicExchangeContext &e = BasicExchangeContext::from_exchange(ex);
    auto ord = e.create_order_replace(order, setup, label);
    if (!ord.discarded()) {
        _exchanges[ex]._batch_place.push_back(ord);
    }
    return Order(ord);
}



void BasicContext::cancel(const Order &order) {
    ExchangeInfo e = order.get_account().get_exchange();
    _exchanges[e]._batch_cancel.push_back(order);
}


void BasicContext::set_timer(Timestamp at, TimerEventCB fnptr, TimerID id) {

    std::lock_guard _(_queue_mx);
    _timed_queue.push(TimerItem{at, id, std::move(fnptr)});
    notify_queue();
}


void BasicContext::unsubscribe(MarketEventType type, const Instrument &i) {
    BasicExchangeContext::from_exchange(i.get_exchange()).unsubscribe(this, type, i);
}


Timestamp BasicContext::get_event_time() const {
    return _event_time;

}


Order BasicContext::bind_order(const Instrument &instrument, const Account &account, std::string_view label) {
    return Order(std::make_shared<AssociatedOrder>(instrument, account, label));
}


void BasicContext::update_account(const Account &a) {
    BasicExchangeContext &e = BasicExchangeContext::from_exchange(a.get_exchange());
    e.update_account(this, a);
}


void BasicContext::allocate(const Account &, double ) {
    //todo
}


bool BasicContext::clear_timer(TimerID id) {
    std::lock_guard _(_queue_mx);
    auto iter = std::find_if(_timed_queue.begin(), _timed_queue.end(), [&](const TimerItem &item){
       return item.id == id;
    });
    if (iter != _timed_queue.end()) {
        _timed_queue.erase(iter);
        return true;
    }
    return false;
}



void BasicContext::update_instrument(const Instrument &i) {
    BasicExchangeContext &e = BasicExchangeContext::from_exchange(i.get_exchange());
    e.update_instrument(this, i);

}


void BasicContext::begin_transaction() {
    _storage->begin_transaction();
}

void BasicContext::commit() {
    _storage->commit();
    for (auto &[ex, batch]: _exchanges) {
        BasicExchangeContext &e = BasicExchangeContext::from_exchange(ex);
        if (!batch._batch_cancel.empty()) {
            e.batch_cancel({batch._batch_cancel.begin(), batch._batch_cancel.end()});
            batch._batch_cancel.clear();
        }
        if (!batch._batch_place.empty()) {
            e.batch_place(this, {batch._batch_place.begin(), batch._batch_place.end()});
            batch._batch_place.clear();
        }
    }
}

void BasicContext::rollback() {
    _storage->rollback();
    for (auto &[ex, batch]: _exchanges) {
        BasicExchangeContext &e = BasicExchangeContext::from_exchange(ex);
        batch._batch_cancel.clear();
        if (!batch._batch_place.empty()) {
            e.batch_cancel({batch._batch_place.begin(), batch._batch_place.end()});
            batch._batch_place.clear();
        }
    }
}

void BasicContext::unset_var(std::string_view var_name) {
    _storage->erase_var(var_name);
}


void BasicContext::set_var(std::string_view var_name, std::string_view value) {
    _storage->put_var(var_name, value);
}



void BasicContext::EvUpdateInstrument::operator ()() {
    me->_strategy->on_update_complete(i, st);
}

void BasicContext::EvUpdateAccount::operator ()() {
    me->_strategy->on_update_complete(a, st);
}

void BasicContext::EvUpdateMarket::operator ()() {
    me->_strategy->on_update_complete(i, type, ev);
}

void BasicContext::EvMarketEventItem::operator ()() {
    me->_strategy->on_market_event(i, event);
}


std::string BasicContext::get_var(std::string_view var_name) const {
    return _storage->get_var(var_name);
}

std::span<const Account> BasicContext::get_accounts() const {
    return {_accounts.data(), _accounts.size()};
}

std::span<const Instrument> BasicContext::get_instruments() const {
    return {_instruments.data(), _instruments.size()};
}


const Config &BasicContext::get_config() const {
    return _config;
}


template<std::invocable<> Fn>
void BasicContext::call_strategy(Fn &&strategy_fn) {
    begin_transaction();
    try {
        strategy_fn();
        //any exception thrown by coroutine is rethrown here
        CoroutineBase::rethrow_stored_exception();
    } catch (...) {
        try {
            _strategy->on_unhandled_exception();
        } catch (...) {
            _storage->rollback();
            _logger.fatal("Unhandled exception in strategy{}", std::current_exception());
            return;
        }
    }
    commit();


}

void BasicContext::reschedule(Timestamp tp) {
    _scheduled_time = tp;
    _scheduler(tp, [this](auto tp){on_scheduler(tp);}, this);
}


void BasicContext::on_scheduler(Timestamp tp) noexcept {
    _event_time = tp;
    auto next_ev = tp;
    std::unique_lock lk(_queue_mx);
    //scheduler processes one action at time, then it is rescheduled
    //in case that action has been processed, it reschedules to immediately execution
    //otherwise, it reschedules to next scheduled time
    //or to infinite time

    //queue is processed first (high priority)
    if (!_queue.empty()) {
        auto f = std::move(_queue.front());
        _queue.pop();
        lk.unlock();
        std::visit([&](auto &item){call_strategy(item);}, f);

    //timed events have middle priority
    } else if (!_timed_queue.empty() && _timed_queue.front().tp <= tp) {
        auto fn = std::move(_timed_queue.front().r);
        _timed_queue.pop();
        lk.unlock();
        call_strategy(fn);

     //market event queue has low priority
    } else if (!_mequeue.empty()) {
        auto item = std::move(_mequeue.front());
        _mequeue.pop_front();
        lk.unlock();
        call_strategy([&]{
               this->_strategy->on_market_event(item.i, item.event);
        });
     //idle events have very low priority
    } else {
        bool r = true;
        lk.unlock();
        call_strategy([this, &r]{
            r = _strategy->on_context_idle();
        });
        if (r) {
            if (_timed_queue.empty()) {
                next_ev = Timestamp::max();
            } else {
                next_ev = _timed_queue.front().tp;
            }
        }
    }
    lk.lock();
    reschedule(next_ev);
}


void BasicContext::EvMQ::operator()() {
    me->_strategy->on_mq_message(std::move(msg));
}
void BasicContext::on_message(MQClient::Message message) {
    std::lock_guard _(_queue_mx);
    _queue.push(EvMQ{this, std::move(message)});
}

bool BasicContext::get_service(const std::type_info &tinfo, std::shared_ptr<void> &ptr) {
    return false;
}

void BasicContext::mq_subscribe_channel(std::string_view channel) {
    _mq.subscribe(this, channel);
}
void BasicContext::mq_unsubscribe_channel(std::string_view channel) {
    _mq.unsubscribe(this, channel);
}
void BasicContext::mq_send_message(std::string_view channel, std::string_view msg) {
    _mq.send_message(this, channel, msg);

}

void BasicContext::update_market(const Instrument &i, MarketEventType type)
{
    BasicExchangeContext::from_exchange(i.get_exchange()).update_market(this, i, type);
}

VarSet<> BasicContext::get_vars(std::string_view prefix) const {
    return _storage->get_vars(prefix);
}

VarSet<> BasicContext::get_vars(std::string_view start, std::string_view end) const {
    return _storage->get_vars(start, end);
}

void BasicContext::EvStart::operator ()() {
    me->_strategy->on_start();
}

void BasicContext::EvRestoreOrders::operator ()() {
    me->_strategy->on_active_orders(std::move(orders));
}
void BasicContext::EvOrderReport::operator ()() {
    me->_strategy->on_order_report(std::move(order), std::move(fills));
}

}

