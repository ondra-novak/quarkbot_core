#pragma once


#include <basic_coro/awaitable_transform.hpp>
#include <basic_coro/cancel_signal.hpp>
#include <memory>
#include <mutex>
#include "basic_coro/prepared_coro.hpp"
#include "quarkbot/abstract/ieventstream.hpp"
#include "quarkbot/execution_worker.hpp"
namespace quarkbot {

class PublisherBase {
public:
        using Factory = std::shared_ptr<IEventStreamBase> (*)(std::shared_ptr<PublisherBase>);

        PublisherBase(Factory factory):_factory(factory) {}

        using Seq = std::size_t;
        
        ///Consumer definition - registration of awaiting coroutine
        struct Consumer {
            awaitable<bool>::result _result; //result of suspended coroutine associated with its execution worker
            coro::cancel_signal *_cancel; // if not null, awaiting can be canceled
        };

                ///cancel awaiting operation
        /**
        @param sig pointer to associated cancel signal, which serves as identification and also signal distributor. Must not be
        null, other function fails with false
        @retval true awaiter was removed and coroutine was scheduled with eof indication as result
        @retval false awaiter was not found, it is possible that the coroutine is running and processing some data. The
        canel_signal prevents to next co_await on this object, but caller must use a proper synchronization to 
        join coroutine's execution with its own thread.

        @note MT safe

        */
        bool cancel(coro::cancel_signal *sig) {       
            coro::prepared_coro pc;     
            if (!sig || sig->is_canceled()) return false; //ignore if already canceled
            std::lock_guard _(_mx);

            //request to cancel
            sig->request_cancel();
            //find all awaiters in the single cancel group and cancel them
            auto end = std::remove_if(_awaiters.begin(), _awaiters.end(), [&](Consumer &c)->bool{
                if (c._cancel == sig) {
                    pc = c._result(false);
                    return true;
                }
                return false;
            });
            //cleanup array if anything canceled
            if (end != _awaiters.end()) {
                _awaiters.erase(end, _awaiters.end());
                //report success
                return true;
            }
            //report failue,
            return false;
        }

        
        auto create_subscriber(std::shared_ptr<PublisherBase> this_shared)const {
            return _factory(this_shared);
        }

        void flush_consumers(bool st) {
            std::lock_guard _(_mx);
            flush_consumers_lk(st);
        }
        
        bool is_closed() const {
            std::lock_guard _(_mx);
            return _closed;
        }

        void close() {
            std::vector<Consumer> tmp;
            {
                std::lock_guard _(_mx);
                _closed = true;
                tmp = std::move(_awaiters);
            }
            for (auto &x: tmp) x._result(false);
        }

protected:

        mutable std::recursive_mutex _mx;
        std::vector<Consumer> _awaiters;

        void flush_consumers_lk(bool st) {
            for (auto &x: _awaiters) x._result(st);
            _awaiters.clear();
        }
        Factory _factory;
        bool _closed = false;

};


}