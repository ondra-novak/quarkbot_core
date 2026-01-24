#include "base_order.hpp"
#include "ifc/execution_worker.hpp"

namespace quarkbot {

BaseOrder::BaseOrder(const OrderParameters &params,
             PTradableInstrument instrument,
             POrder replaced,
             std::string_view name)
                :_params(params)
                ,_instrument(std::move(instrument) )
                ,_replaced(replaced)
                ,_name(name) {}





coro::prepared_coro BaseOrder::post_update(OrderStatus status) {
    std::lock_guard _(_mx);
    if (_event_waiter) {
        _status = status;
        return _event_waiter(BaseOrder::is_done());
    } else {
        _events.push_back(status);            
        return {};
    }
}

coro::prepared_coro BaseOrder::post_update(OrderFill fill) {
    std::lock_guard _(_mx);
    _events.push_back(fill);
    return _event_waiter(true);

}
coro::awaitable<bool> BaseOrder::wait_event()  {
    return [&](coro::awaitable<bool>::result res) {
        std::lock_guard lock(_mx);
        if (!_events.empty()) {     //not empty
            flush_statuses();       //flush any status change before fills
            return res(BaseOrder::is_done());   //report false, if order is done
        } else if (BaseOrder::is_done()) {  //empty and done
            return res(false);          //report it is done
        } else { //empty and not done, register result with dispatcher
            _event_waiter = IExecutionWorker::proxy_result(std::move(res));                    
            return coro::prepared_coro{};   //nothing to resume
        }
    };
}

double BaseOrder::get_remaining_amount() const  {
    return _params.amount.value - _filled_amount;
}

bool BaseOrder::any_fill() const  {
    std::lock_guard lock(_mx);
    auto iter = std::find_if(_events.begin(), _events.end(), [](const Event &ev){
        return std::holds_alternative<OrderFill>(ev);
    });
    return iter != _events.end();
}
std::optional<OrderFill> BaseOrder::read_fill()  {
    std::lock_guard lock(_mx);
    flush_statuses();
    std::optional<OrderFill> res;
    if (_events.empty()) return res;
    res.emplace(std::move(std::get<OrderFill>(_events.front())));
    _filled_amount += res->amount;
    return res;
}

OrderParameters BaseOrder::get_parameters() const {
    return _params;
}
OrderStatus BaseOrder::get_status() const {
    return _status;
}
bool BaseOrder::is_done() const {
    return is_done_status(_status);
}
PTradableInstrument BaseOrder::get_instrument() const  {
    return _instrument;
}

std::string_view BaseOrder::get_name() const  {
    return _name ;
}
POrder BaseOrder::get_replaced_order() const  {
    return _replaced.lock();
}

void BaseOrder::flush_statuses() {
        while (!_events.empty() && std::holds_alternative<OrderStatus>(_events.front())) {
            auto &st = std::get<OrderStatus>(_events.front());
            _status = st;
            _events.pop_front();
        }
    }


}