#pragma once

#include "basic_coro/cancel_signal.hpp"
#include "execution_worker.hpp"
#include "ifc/stream_defs.hpp"
#include "stream.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace quarkbot {

    ///generic stream server pro events
    /**
        There is no queue, just last value.
        Readers are polling value
        Readers can wait for next event
        Revision is used to detect change        
    */
    template<StreamType T>
    class EventStreamPublisher {
    public:

        using Revision = StreamEventRevision;
        
        ///Consumer definition - registration of awaiting coroutine
        struct Consumer {
            ResultAndExecWorker<bool> _result; //result of suspended coroutine associated with its execution worker
            coro::cancel_signal *_cancel; // if not null, awaiting can be canceled
        };

        using Event = StreamEvent<T>;

        ///Poll data from the server, copy them to Event structure
        /**
        @param ev refernece to event structure where copy is stored
        @retval true update
        @retval false no change detected

        @note the function updates ev.revision and ev.missed field to report count of
        missed events. If the steam is closed, the method ev.eof() starts to return true

        Read is MT safe
        */
        bool read(Event &ev) const {            
            Revision rev;
            Revision cmp_rev;
            cmp_rev = _rev.load(std::memory_order_acquire);            
            if (cmp_rev == ev.revision) return false;    //no data change detected
            do {
                rev = cmp_rev;
                ev.data =  _value;
                ev.received = _received;
                cmp_rev = _rev.load(std::memory_order_acquire);
            } while (rev != cmp_rev || cmp_rev & 1);
            ev.missed = ((rev - ev.revision)>>1) - 1;
            ev.revision = rev;
            return true;
        }

        ///Write new event
        /**
            @param cb callback which receives reference to data, where new event data should be written. The callback
                should return std::chrono::system_clock::time_point of timestamp when the data has been received            

            @note Write is MT safe unless there are multiple writers
        */
        template<typename CB>
        requires(std::is_nothrow_invocable_r_v<std::chrono::system_clock::time_point, CB, volatile T &>)
        void write(CB &&cb) {
            _rev.fetch_add(1, std::memory_order_relaxed);
            _received = cb(_value);
            _rev.fetch_add(1,std::memory_order_release);
            std::lock_guard _(_mx);
            for (auto &x: _awaiters) {
                x(true);
            }
            _awaiters.clear();
        }

        ///begin await on next event
        /**
            @param rev last seen revision (Event.revision)
            @param cancel optional pointer to cancel signal structure. if supplied, the structure must be in no-cancel
            state, otherwise the function immediately reports closed_stream / eof
            @return avaitable which returns true, if new revision is ready, or false if stream has been closed or 
                this operation has been canceled. The co_await operation blocks until new event is available

            @note MT safe
        */
        awaitable<bool> next_event(Revision rev, coro::cancel_signal *cancel = nullptr) {
            //already canceled - return immediately
            if (cancel && cancel->is_canceled()) return closed_stream;      
            //poll current revision      
            auto x = _rev.load(std::memory_order_relaxed);
            //if revision is different, immediately return whether stream is closed (false), or not (true)
            if (x != rev) return x != closed_stream;
            //now we must wait
            return [this, rev, cancel](auto promise) {
                //exception can be thrown from this when caller is not in ExecWorker thread
                auto result = ResultAndExecWorker(std::move(promise));
                std::lock_guard _(_mx);
                //all tests must be performed again under lock
                //if already canceled
                if (cancel && cancel->is_canceled()) {
                    result(false);
                    return;
                }
                //poll revision (we are under lock)
                auto x = _rev.load(std::memory_order_relaxed);
                //if revision is different
                if ( rev != x) {
                    //resolve with status true = new event, false = closed stream
                    result(x != closed_stream);
                } else {
                    //register 
                    _awaiters.push_back({std::move(result), cancel});
                }
            };
        }

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
            if (!sig || sig->is_canceled()) return false; //ignore if already canceled
            std::lock_guard _(_mx);
            //try to find awaiter
            auto found =  std::find_if(_awaiters.begin(), _awaiters.end(), [&](const Consumer &c){
                return c._cancel == sig;
            });
            //request cancel on signal
            sig->request_cancel();
            //if found, remove it and report true
            if (found != _awaiters.end()) {
                found->_result(false);  //close the stream for this consumer only
                _awaiters.erase(found); 
                return true;
            }
            //otherwise report false
            return false;
        }

        ///tests, whether server is closed
        /**
        @note MT safe
        */
        bool is_closed() const {
            return _rev.load(std::memory_order_relaxed) == closed_stream;
        }

        ///close the server, all awaiters are resolved with false, and revision is set to closed_stream. Writer should not write any more data        
        /**
        @note MT safe unless there are multiple writers
        */
        void close() {
            std::lock_guard _(_mx);
            _rev.store(closed_stream, std::memory_order_release);
             for (auto &x: _awaiters) {
                x(false);
            }                        
            _awaiters.clear();
        }

    protected:
        volatile T _value;
        volatile std::chrono::system_clock::time_point _received;
        mutable std::mutex _mx;
        std::vector<Consumer> _awaiters;
        std::atomic<Revision> _rev;
    };



}