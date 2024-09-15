#include "basic_context.h"

#include <quarkbot/basic_order.h>
#include <quarkbot/shared_state.h>

#include <list>
#include <future>
#include <vector>

namespace quarkbot {


BasicContext::BasicContext(std::unique_ptr<IStorage> storage,
        IControl &control,
        Log logger,
        MQBroker mq,
        std::string_view strategy_name)
    :_control(control)
    ,_storage(std::move(storage))
    ,_logger(std::move(logger), "{}", strategy_name)
    ,_mq(mq)
    ,_name(strategy_name)
{
    _control.attach(this);
}


BasicContext::~BasicContext() {
    stop_internal();
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

void BasicContext::series_erase_points(std::string_view series_name, uint64_t index_and_less) {
    _storage->series_erase_points(series_name, index_and_less);
}

uint64_t BasicContext::series_add_point(std::string_view series_name, std::string_view point_data) {
    return _storage->series_add_point(series_name, point_data);
}

ValueStream<std::string_view> BasicContext::load_series(std::string_view name) const {
    return _storage->load_series(name);
}

void BasicContext::stop() {
    _stop_called = true;
}

std::vector<std::pair<Order, Order::Report> > BasicContext::restore_orders() {

    std::vector<std::pair<Order, Order::Report> > result;
    SharedState<std::vector<std::pair<Order, Order::Report> > > state({},
                    [&](auto &v){result = std::move(v);});

    std::exception_ptr e = {};
    for (const auto &a: _accounts) {
        auto &ctx = BasicExchangeContext::from_exchange(a.get_exchange());
        auto orders = _storage->load_open_orders(a);
        if (!orders.empty()) {
            ctx.restore_orders(a, orders, [state, &e](AsyncResult<IExchange::RestoredOrders> arg){
                std::lock_guard _(state);
                try {
                    auto arr = arg.get();
                    state->insert(state->end(), arr.begin(), arr.end());
                } catch (...) {
                    e = std::current_exception();
                }

            });
        }
    }
    state.wait();
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

    _queue.post(EvStart{this});
    _queue.post(std::move(ev_restored));
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


bool BasicContext::on_subscription_event(const MarketEvent &event) {
    std::lock_guard _(_queue_mx);
    if (can_collapse(event.type)) {
        post_collapse(EvMarketEventItem{this,event});
    } else {
        post(EvMarketEventItem{this,event});
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

void BasicContext::on_update(Instrument i, MarketEventType type, AsyncResult<MarketEventData> event) {
    std::lock_guard _(_queue_mx);
    post(EvUpdateMarket{this, i, type, std::move(event)});
}



void BasicContext::notify_queue() {
    if (_in_scheduler) return;
    Timestamp tp = _queue.get_nearest_schedule();
    _control.schedule(tp);
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


void BasicContext::update_account(const Account &a, Function<void(AsyncResult<void>)> &&cb) {
    auto &cbs = _update_account_cbs[a];
    if (cbs.register_callback(std::move(cb))) {
        BasicExchangeContext &e = BasicExchangeContext::from_exchange(a.get_exchange());
        e.update_account(this, a);
    }
}


void BasicContext::allocate(const Account &, double ) {
    //todo
}


bool BasicContext::clear_timer(TimerID id) {
    std::lock_guard _(_queue_mx);
    return _queue.cancel(reinterpret_cast<const void *>(id));
}



void BasicContext::update_instrument(const Instrument &i, Function<void(AsyncResult<void>)> &&cb) {
    auto &cbs = _update_instrument_cbs[i];
    if (cbs.register_callback(std::move(cb))) {
        BasicExchangeContext &e = BasicExchangeContext::from_exchange(i.get_exchange());
        e.update_instrument(this, i);
    }
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
    auto iter = me->_update_instrument_cbs.find(i);
    if (iter != me->_update_instrument_cbs.end()) {
        iter->second.send(st);
        me->_update_instrument_cbs.erase(iter);
    }
}

void BasicContext::EvUpdateAccount::operator ()() {
    auto iter = me->_update_account_cbs.find(a);
    if (iter != me->_update_account_cbs.end()) {
        iter->second.send(st);
        me->_update_account_cbs.erase(iter);
    }
}

void BasicContext::EvUpdateMarket::operator ()() {
    auto iter = me->_update_market_cbs.find({i,type});
    if (iter != me->_update_market_cbs.end()) {
        iter->second.send(ev);
        me->_update_market_cbs.erase(iter);
    }
}

void BasicContext::EvMarketEventItem::operator ()() {
    me->_on_market_event_cbs.send(event);
    me->_strategy->on_market_event(event);
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
            _control.notify_fail();
            return;
        }
    }
    commit();
}

bool BasicContext::is_stopped() const noexcept  {
    return _stop_called;
}

void BasicContext::request_stop()  noexcept {
    std::lock_guard _(_queue_mx);
    if (_queue.post(EvStopRequest{this})) {
        notify_queue();
    }
}

void BasicContext::stop_internal() {
    _queue.clear();
    _mq.unsubscribe_all(this);
    for (const auto &[e,_]: _exchanges) {
        BasicExchangeContext::from_exchange(e).disconnect(this);
    }
    _update_account_cbs.clear();
    _update_instrument_cbs.clear();
    _update_market_cbs.clear();
    _order_report.clear();
    _on_idle_cbs.clear();
    _on_market_event_cbs.clear();
    _on_restored_orders_cbs.clear();
    _on_mq_message.clear();
    _on_stop_cb = {};
    _control.notify_exit();

}

void BasicContext::on_scheduled(Timestamp tp) noexcept {
    _event_time = tp;
    auto next_ev = tp;
    std::unique_lock lk(_queue_mx);

    if (_stop_called) {
        stop_internal();
        return;
    }

    _in_scheduler = true;

    auto executor = [&](auto &&ev){
        lk.unlock();
        std::visit([&](auto &&fn) {
            call_strategy(std::move(fn));
        }, ev);
        lk.lock();
        return true;
    };

    if (!_queue.process_message(tp, executor)) {
        bool r = _on_idle_cbs.empty();
        if (!r) {
            auto &fn = _on_idle_cbs.front();
            lk.unlock();
            call_strategy([&]{
                fn(AsyncResult<void>());
            });
            lk.lock();
            r = _on_idle_cbs.empty();
        }
        if (r) {
            next_ev = _queue.get_nearest_schedule();
        }
    }
    _in_scheduler = false;
    _control.schedule(next_ev);
}


void BasicContext::EvMQ::operator()() {
    me->_on_mq_message.send(Message{
        msg.get_sender(), msg.get_channel(), msg.get_content(), msg.get_conversation(), pm
    });
}
void BasicContext::on_message(const MQClient::Message &message, bool pm) noexcept {
    std::lock_guard _(_queue_mx);
    post(EvMQ{this, std::move(message), pm});
}

bool BasicContext::get_service(const std::type_info &, std::shared_ptr<void> &) {
    return false;
}

void BasicContext::subscribe_channel(std::string_view channel) {
    _mq.subscribe(this, channel);
}
void BasicContext::unsubscribe_channel(std::string_view channel) {
    _mq.unsubscribe(this, channel);
}
void BasicContext::send_message(std::string_view channel, std::string_view msg, IMQBroker::ConversationID cid) {
    _mq.send_message(this, channel, msg, cid);

}

void BasicContext::update_market(const Instrument &i, MarketEventType type, Function<void(AsyncResult<MarketEventData>)> &&cb)
{
   auto &cbs = _update_market_cbs[{i,type}];
   if (cbs.register_callback(std::move(cb))) {
       BasicExchangeContext::from_exchange(i.get_exchange())
                       .update_market(this, i, type);
   }
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
    while (!me->_on_restored_orders_cbs.send(std::span(orders.begin(), orders.end())));
    me->_orders_restored = true;

}
void BasicContext::EvOrderReport::operator ()() {
    auto &ex = BasicExchangeContext::from_exchange(order.get_account().get_exchange());
    ex.order_apply_report(order, report);
    me->_storage->put_order(me->_event_time, order);
     //remove duplicate fills, store unique fills to the DB
    report.fills.erase(std::remove_if(report.fills.begin(), report.fills.end(), [&](const Fill &f){
        bool dup = me->_storage->is_duplicate_fill(f);
        if (!dup) me->_storage->put_fill(me->_event_time, f);
        return dup;
    }), report.fills.end());
    auto fillspan = std::span(report.fills.data(), report.fills.size());
    auto iter = me->_order_report.find(order);
    if (iter != me->_order_report.end()) {
        auto &cbs = iter->second;
        cbs.send(fillspan);
        if (order.done()) me->_order_report.erase(iter);
    }
    me->_strategy->on_order_report(std::move(order), fillspan);
}

std::size_t BasicContext::QueueItemHasher::operator ()(const QueueItem &x) const {
    if (std::holds_alternative<EvMarketEventItem>(x)) {
        auto ev = std::get<EvMarketEventItem>(x);
        Instrument::Hasher hasher;
        return hasher(ev.event.instrument) + static_cast<std::size_t>(ev.event.type);
    } else {
        throw std::runtime_error("Internal: Attempt to collapse non-collapsable event");
    }
}

bool BasicContext::QueueItemCompare::operator ()(const QueueItem &a, const QueueItem &b) const {
    if (std::holds_alternative<EvMarketEventItem>(a) && std::holds_alternative<EvMarketEventItem>(b)) {
        auto ev_a = std::get<EvMarketEventItem>(a);
        auto ev_b = std::get<EvMarketEventItem>(b);
        return ev_a.event.instrument == ev_b.event.instrument
                && ev_a.event.type == ev_b.event.type;
    } else {
        throw std::runtime_error("Internal: Attempt to collapse non-collapsable event");
    }
}

void BasicContext::EvStopRequest::operator ()() {
    me->_stop_requested = true;
    if (me->_on_stop_cb) {
        me->_on_stop_cb(AsyncResult<void>());
    } else {
        me->stop();
    }
}

void BasicContext::on_idle(Function<void(AsyncResult<void> )> &&fn) {
    std::lock_guard _(_queue_mx);
    _on_idle_cbs.push_back(std::move(fn));
    if (_in_scheduler) return;
    _control.schedule(Timestamp::min());
}

void BasicContext::receive_order_report(const Order &order, Function<void(AsyncResult<std::span<Fill>  >)> &&cb)  {
    if (order.done()) return;
    auto &cbs = _order_report[order];
    cbs.register_callback(std::move(cb));
}

void BasicContext::on_stop_requested(Function<void(AsyncResult<void>)> &&fn) {
    _on_stop_cb = std::move(fn);
}

void BasicContext::on_market_event(Function<void(AsyncResult<MarketEvent>)> &&callback) {
    _on_market_event_cbs.register_callback(std::move(callback));
}
void BasicContext::on_orders_restored(Function<void(AsyncResult<std::span<Order> >)> &&callback) {
    if (_orders_restored) {
        callback(std::in_place);
    } else {
        _on_restored_orders_cbs.register_callback(std::move(callback));
    }
}
void BasicContext::on_mq_message(Function<void(AsyncResult<Message>)> &&callback) {
    _on_mq_message.register_callback(std::move(callback));
}

}
