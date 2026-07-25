#pragma once

#include "basic_coro/awaitable.hpp"
#include "basic_coro/cancel_signal.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "publisher_base.hpp"
#include "subscriber.hpp"
#include "quarkbot/utils/lockfree_queue.hpp"
#include <mutex>
#include <type_traits>
namespace quarkbot {


    template<typename ViewType, std::size_t queue_len>
    class LockFreePublisher: public PublisherBase {
    public:


        template<typename CBWriter>
        requires(std::is_nothrow_invocable_r_v<bool, CBWriter, ViewType &>)
        void write(CBWriter &&writer) {
            if (_queue.write(std::forward<CBWriter>(writer))) {
                flush_consumers(true);
            }
        }

        void publish(const ViewType &data) {
            write([&](ViewType &x) noexcept {
                x = data;
                return true;
            });
        }

        //retrieve reference to top value, useful only for publisher to apply incrmeents
        const ViewType &get_top_value_ref() const {
            return _queue.get_top_value_ref();
        }

        template<typename X>
        requires(std::is_nothrow_assignable_v<X, ViewType>)
        bool read(X &target, Seq &seq) {
            return _queue.read(target, seq);
        }

        coro::prepared_coro next(Seq seq, awaitable<bool>::result promise, coro::cancel_signal *sig) {
            std::lock_guard _(_mx);
            if (sig && sig->is_canceled()) return promise(false);
            if (seq<_queue.get_top_seq()) return promise(true);
            if (_closed) return promise(false);
            _awaiters.push_back({std::move(promise), sig});
            return coro::prepared_coro{};
        }

        coro::awaitable<bool> next(Seq seq, coro::cancel_signal *sig) {
            if (sig && sig->is_canceled()) return false;
            if (seq < _queue.get_top_seq()) return true;
            return [seq, this, sig](auto promise) -> coro::prepared_coro {
                return next(seq, std::move(promise), sig);
            };
        }

        LockFreePublisher():PublisherBase([](std::shared_ptr<PublisherBase> this_shared) -> std::shared_ptr<IEventStreamBase> {
            auto me = std::static_pointer_cast<LockFreePublisher>(this_shared);
            return std::make_shared<StreamSubscriber<ViewType, LockFreePublisher> >(me);
        }) {}
        LockFreePublisher(Factory factory):PublisherBase(factory) {}

        Seq get_top_seq() const {
            return _queue.get_top_seq();
        }


    protected:
        LockFreeQueue<ViewType, queue_len> _queue;        

    };

}