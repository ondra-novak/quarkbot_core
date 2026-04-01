#pragma once

#include "basic_coro/cancel_signal.hpp"
#include "ifc/execution_worker.hpp"
#include "stream_defs.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unistd.h>
#include <vector>

namespace quarkbot {



    template<StreamType T>
    class EventStreamServer {
    public:

        using Revision = std::size_t;

        static constexpr auto closed_stream = ~Revision(1);

        struct Consumer {
            ProxyResult<Revision> _result;
            coro::cancel_signal *_cancel;
        };

        T read() const {
            T out;
            Revision rev;
            Revision cmp_rev;
            cmp_rev = _rev.load(std::memory_order_relaxed);
            do {
                rev = cmp_rev;
                out =  _value;
                cmp_rev = _rev.load(std::memory_order_relaxed);
            } while (rev != cmp_rev || cmp_rev & 1);
            return out;
        }

        template<std::invocable<T &> CB>
        auto write(CB &&cb) {
            _rev.fetch_add(1, std::memory_order_relaxed);
            cb(_value);
            auto new_rev = _rev.fetch_add(1,std::memory_order_release)+1;
            std::lock_guard _(_mx);
            for (auto &x: _awaiters) {
                x(new_rev);
            }
        }

        awaitable<Revision> next_event(Revision rev, coro::cancel_signal *cancel) {
            if (cancel && cancel->is_canceled()) return closed_stream;
            auto x = _rev.load(std::memory_order_relaxed);
            if (x != rev) return x;
            return [this, rev, cancel](auto promise) {
                auto result = ProxyResult(std::move(promise));
                std::lock_guard _(_mx);
                if (cancel && cancel->is_canceled()) {
                    result(closed_stream);
                    return;
                }
                auto x = _rev.load(std::memory_order_relaxed);
                if ( rev != x) {
                    result(x);
                } else {
                    _awaiters.push_back({std::move(result), cancel});
                }
            };
        }

        void cancel(coro::cancel_signal *sig) {
            if (!sig || sig->is_canceled()) return;
        }

    protected:
        T _value;
        mutable std::mutex _mx;
        std::vector<Consumer> _awaiters;
        std::atomic<Revision> _rev;
    };



}