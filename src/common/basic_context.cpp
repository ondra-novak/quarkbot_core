#include "basic_context.h"

#include <quarkbot/basic_order.h>

#include <list>
#include <future>
#include <vector>

namespace quarkbot {



BasicContext::~BasicContext() {
    _mq.unsubscribe_all(this);
    _scheduler(Timestamp{}, [](auto){}, this);
    for (const auto &[e,_]: _exchanges) {
        BasicExchangeContext::from_exchange(e).disconnect(this);
    }

}

Positions BasicContext::load_positions(std::string_view filter) const {
    return _storage->load_positions(filter);
}

Trades BasicContext::load_closed(Timestamp limit, std::string_view filter) const {
    return _storage->load_closed(limit, filter);
}

std::string_view BasicContext::get_strategy_name() const {
    return _name;
}

Log BasicContext::get_logger() const {
    return _logger;
}

std::vector<std::pair<Order, Order::Report> > BasicContext::restore_orders() {
    std::mutex mx;
    std::condition_variable cond;
    unsigned int counter = 1;

    std::vector<std::pair<Order, Order::Report> > result;
    std::exception_ptr e = {};
    for (const auto &a: _accounts) {
        auto &ctx = BasicExchangeContext::from_exchange(a.get_exchange());
        auto orders = _storage->load_open_orders(a);
        if (!orders.empty()) {
            ctx.restore_orders(a, orders, [&](AsyncResult<IExchange::RestoredOrders> arg){

                //runs async
                std::lock_guard _(mx);
                try {
                    auto arr = arg.get();
                    result.insert(result.end(), arr.begin(), arr.end());
                } catch (...) {
                    e = std::current_exception();
                }
                if (--counter == 0) cond.notify_all(); //notify last operation

            });
        }
    }
    std::unique_lock lk(mx);
    --counter;

    //wait for completion
    cond.wait(lk,[&]{return counter == 0;});
    if (e) std::rethrow_exception(e);
    return result;

}

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
    auto restored = restore_orders();
    EvRestoreOrders ev_restored{this,{}};
    ev_restored.orders.reserve(restored.size());
    for (auto &x: restored) {ev_restored.orders.push_back(x.first);}
    if (!ev_restored.orders.empty()) {
        _queue.post(std::move(ev_restored));
    }
    _queue.post(EvStart{this});
    for (auto &x: restored) {
        if (!IOrder::is_done(*x.second.new_state)) {
            auto &ex = BasicExchangeContext::from_exchange(x.first.get_account().get_exchange());
            ex.subscribe_order(this, x.first);
        }
        _queue.post(EvOrderReport{this, std::move(x.first), std::move(x.second)});
    }
    notify_queue();
}

void BasicContext::post(QueueItem &&q) {
    if (_queue.post(std::move(q))) notify_queue();
}
void BasicContext::post_collapse(QueueItem &&q) {
    if (_queue.post_collapse(std::move(q))) notify_queue();
}


bool BasicContext::on_subscription_event(Instrument i, MarketEventType type, MarketEvent event) {
    std::lock_guard _(_queue_mx);
    if (can_collapse(type)) {
        post_collapse(EvMarketEventItem{this,std::move(i), type, std::move(event)});
    } else {
        post(EvMarketEventItem{this,std::move(i), type,std::move(event)});
    }
    return true;
}

void BasicContext::on_order_report(Order order,Order::Report report) {
    std::lock_guard _(_queue_mx);
    post(EvOrderReport{this, std::move(order), std::move(report)});

}


void BasicContext::on_update(Instrument i, AsyncResult<void> st) {
    std::lock_guard _(_queue_mx);
    post(EvUpdateInstrument{this, std::move(i), std::move(st)});

}



void BasicContext::on_update(Account a, AsyncResult<void> st ) {
    std::lock_guard _(_queue_mx);
    post(EvUpdateAccount{this, std::move(a), std::move(st)});
}

void BasicContext::on_update(Instrument i, MarketEventType type, AsyncResult<MarketEvent> event) {
    std::lock_guard _(_queue_mx);
    post(EvUpdateMarket{this, i, type, std::move(event)});
}



void BasicContext::notify_queue() {
    Timestamp tp = _queue.get_nearest_schedule();
    _scheduler(tp,[this](auto tp){on_scheduler(tp);}, this);
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
    if (_queue.post_timed(id, at, std::move(fnptr))) {
        notify_queue();
    }
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
    return _queue.cancel(reinterpret_cast<const void *>(id));
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
    _storage->erase_var(_event_time, var_name);
}


void BasicContext::set_var(std::string_view var_name, std::string_view value) {
    _storage->put_var(_event_time, var_name, value);
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

void BasicContext::on_scheduler(Timestamp tp) noexcept {
    _event_time = tp;
    auto next_ev = tp;
    std::unique_lock lk(_queue_mx);

    auto executor = [&](auto &&ev){
        lk.unlock();
        std::visit([&](auto &&fn) {
            call_strategy(std::move(fn));
        }, ev);
        lk.lock();
        return true;
    };

    if (!_queue.process_message(tp, executor)) {
        bool r = true;
        lk.unlock();
        call_strategy([this, &r]{
            r = _strategy->on_context_idle();
        });
        lk.lock();
        if (r) {
            next_ev = _queue.get_nearest_schedule();
        }
    }
    _scheduler(next_ev, [this](auto tp){on_scheduler(tp);}, this);
}


void BasicContext::EvMQ::operator()() {
    me->_strategy->on_mq_message(std::move(msg));
}
void BasicContext::on_message(MQClient::Message message) {
    std::lock_guard _(_queue_mx);
    post(EvMQ{this, std::move(message)});
}

bool BasicContext::get_service(const std::type_info &, std::shared_ptr<void> &) {
    return false;
}

void BasicContext::mq_subscribe_channel(std::string_view channel) {
    _mq.subscribe(this, channel);
}
void BasicContext::mq_unsubscribe_channel(std::string_view channel) {
    _mq.unsubscribe(this, channel);
}
void BasicContext::mq_send_message(std::string_view channel, std::string_view msg, IMQBroker::ConversationID cid) {
    _mq.send_message(this, channel, msg, cid);

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
    auto &ex = BasicExchangeContext::from_exchange(order.get_account().get_exchange());
    ex.order_apply_report(order, report);
    me->_storage->put_order(me->_event_time, order);
    { //remove duplicate fills, store unique fills to the DB
        auto iter = std::remove_if(report.fills.begin(), report.fills.end(), [&](const Fill &f){
            bool dup = me->_storage->is_duplicate_fill(f);
            if (!dup) me->_storage->put_fill(me->_event_time, f);
            return dup;
        });
        report.fills.erase(iter, report.fills.end());
    }
    me->_strategy->on_order_report(std::move(order), std::move(report.fills));
}

std::size_t BasicContext::QueueItemHasher::operator ()(const QueueItem &x) const {
    if (std::holds_alternative<EvMarketEventItem>(x)) {
        auto ev = std::get<EvMarketEventItem>(x);
        Instrument::Hasher hasher;
        return hasher(ev.i) + static_cast<std::size_t>(ev.type);
    } else {
        throw std::runtime_error("Internal: Attempt to collapse noncollapsable event");
    }
}

bool BasicContext::QueueItemCompare::operator ()(const QueueItem &a, const QueueItem &b) const {
    if (std::holds_alternative<EvMarketEventItem>(a) && std::holds_alternative<EvMarketEventItem>(b)) {
        auto ev_a = std::get<EvMarketEventItem>(a);
        auto ev_b = std::get<EvMarketEventItem>(b);
        return ev_a.i == ev_b.i && ev_a.type == ev_b.type;
    } else {
        throw std::runtime_error("Internal: Attempt to collapse noncollapsable event");
    }

}

}
