#pragma once

#include "../quarkbot/function.h"
#include "../common/dispatcher.h"
#include "../common/common.h"
#include <chrono>

namespace quarkbot {


class SimScheduler {
public:



    auto get_instance_for_exchange() {
        return [this](Timestamp tp, Function<void(Timestamp)> fn, const void *ident){
            _dispatcher.post_timed(ident, tp,std::move(fn));
        };
    }

    auto get_instance_for_strategy() {
        return [this](Timestamp tp, Function<void(Timestamp)> fn, const void *ident){
            if (ident == _dispatcher.get_executing_ident()) {
                auto exec_time = std::chrono::system_clock::now() - _exec_start;
                tp = std::max(tp, _tp + exec_time);
            }
            _dispatcher.post_timed(ident, tp,std::move(fn));
        };
    }

    bool is_next() const {
        return !_dispatcher.empty();
    }

    auto get_next_time() const {
        return _dispatcher.get_nearest_schedule();
    }

    bool go_next() {
        if (_dispatcher.empty()) return false;
        _tp = std::max(_tp,_dispatcher.get_nearest_schedule());
        _dispatcher.process_message(_tp, [&](auto &&fn){
            _exec_start = std::chrono::system_clock::now();
            fn(_tp);
            return true;
        });
        return true;
    }

protected:

    Timestamp _tp = {};
    Timestamp _exec_start ={};
    DispatcherCore<Function<void(Timestamp)> > _dispatcher;




};


}
