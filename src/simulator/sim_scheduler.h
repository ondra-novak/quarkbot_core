#pragma once

#include "../quarkbot/function.h"
#include "../common/dispatcher.h"
#include "../common/common.h"

namespace quarkbot {


class SimScheduler {
public:



    auto get_instance() {
        return [this](Timestamp tp, Function<void(Timestamp)> fn, const void *ident){
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
        tp = std::max(tp,_dispatcher.get_nearest_schedule());
        _dispatcher.process_message([&](auto &&fn){
            fn(tp);
            return true;
        });
        return true;
    }

protected:

    Timestamp tp = {};

    DispatcherCore<Function<void(Timestamp)> > _dispatcher;




};


}
