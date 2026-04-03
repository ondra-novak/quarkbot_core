#include "base_order.hpp"
#include "ifc/execution_worker.hpp"
#include "ifc/order.hpp"
#include <variant>

namespace quarkbot {

BaseOrder::BaseOrder(const OrderParameters &params,
             PTradableInstrument instrument,
             POrder replaced,
             std::string_view name)
                :_params(params)
                ,_instrument(std::move(instrument) )
                ,_replaced(replaced)
                ,_name(name) {}





void BaseOrder::post_update(OrderStatus status) {
    std::lock_guard _(_mx);
    if (_event_waiter) {
        _status = status;
        _event_waiter(BaseOrder::is_done());
    } else {
        _events.push_back(status);            
    }
}

void BaseOrder::post_update(Fill fill) {
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
            _event_waiter = std::move(res);                    
            return coro::prepared_coro{};   //nothing to resume
        }
    };
}


bool BaseOrder::any_fill() const  {
    std::lock_guard lock(_mx);
    auto iter = std::find_if(_events.begin(), _events.end(), [](const Event &ev){
        return std::holds_alternative<Fill>(ev);
    });
    return iter != _events.end();
}
std::optional<Fill> BaseOrder::read_fill()  {
    std::lock_guard lock(_mx);
    flush_statuses();
    std::optional<Fill> res;
    if (_events.empty()) return res;
    res.emplace(std::move(std::get<Fill>(_events.front())));
    _filled_amount += res->amount;
    return res;
}

const OrderParameters &BaseOrder::get_parameters() const {
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
        while (!_events.empty()) {
            auto &ev = _events.front();
            if (std::holds_alternative<OrderStatus>(ev)) {
                auto &st = std::get<OrderStatus>(ev);
                _status = st;
            } else if (std::holds_alternative<Rejection>(ev)) {
                auto &rj = std::get<Rejection>(ev);
                _status = OrderStatus::rejected;
                _rejection_reason = rj.reason;
                _rejection_message = rj.text;
            } else {
                break;
            }
        }
    }


}