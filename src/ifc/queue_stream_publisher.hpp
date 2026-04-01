#pragma once

#include "basic_coro/cancel_signal.hpp"
#include "execution_worker.hpp"
#include "ifc/publisher_base.hpp"
#include "ifc/stream_defs.hpp"
#include "stream.hpp"
#include <chrono>
#include <deque>
#include <iterator>
#include <mutex>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace quarkbot {

    ///generic stream server pro important events
    /**
        Not such fast as EventStreamPublisher
        Manages queue which length is defined by time span (few seconds)
    */
    template<StreamType T>
    class QueueStreamPublisher : public PublisherBase{
    public:

        ///Constructor with obsolence interval definition
        /**
        @param obsolence_interval defines how long events are stored in the queue. 
            It is used to prevent queue from growing infinitely, but it can cause events to be lost if consumer is too slow.
        */
        explicit QueueStreamPublisher(std::chrono::system_clock::duration obsolence_interval)
            :_obsolence_interval(obsolence_interval) {}


        using Event = StreamEvent<T>;

        ///Poll data from the server, copy them to Event structure
        /**
        @param ev refernece to event structure where copy is stored
        @retval true update. Note EOS is reported as update with ev.eof() returning true
        @retval false no change detected
        */
        bool read(Event &ev) {
            std::lock_guard lk(_mx);
            if (ev.revision >= _front_revision) {
                if (_closed) {
                    ev.revision = closed_stream;
                    return true;
                }
                return false;
            }
            ev.missed = 0;
            auto ofs = _front_revision - ev.revision;
            if (ofs >= _queue.size()) {
                ofs = _queue.size();
                auto new_rev = _front_revision - ev.revision;
                ev.missed = new_rev - ev.revision;
                ev.revision = new_rev;
            }
            ev.revision++;
            ofs--;
            auto iter = _queue.begin();
            std::advance(iter, ofs);
            ev.data = iter->data;
            ev.received = iter->received;
            
            return true;            
        }

        ///Write new event
        /**
            @param cb callback which receives reference to data, where new event data should be written. The callback
                should return std::chrono::system_clock::time_point of timestamp when the data has been received
            @note The callback must be declared noexecpt
        */            
        template<typename CB>
        requires(std::is_nothrow_invocable_r_v<std::chrono::system_clock::time_point, CB, volatile T &>)
        void write(CB &&cb) {
            std::lock_guard lk(_mx);
            _queue.push_front(Item{});
            Item &nw = _queue.front();
            nw.received = cb(nw.data);
            auto obsolence_tp = nw.received - _obsolence_interval;
            while (_queue.back().received < obsolence_tp) _queue.pop_back();
            ++_front_revision;
            for (auto &c: _awaiters) c(true);
            _awaiters.clear();
        }

        ///begin await on next event
        /**
        @param rev revision of event which consumer has. If there is already newer event, the
                consumer receives it immediately without suspension. 
                Otherwise, the consumer is suspended until new event is available. 
                If stream is closed, the consumer receives eof indication immediately without suspension
        @param cancel optional pointer to cancel signal structure. if supplied, the structure must be in no-cancel
               state, otherwise the function immediately reports closed_stream / eof
        @return awaitable which returns true, if new event is ready, or false if stream has been closed or 
                this operation has been canceled. The co_await operation blocks until new event is available
        */
        awaitable<bool> next_event(Revision rev, coro::cancel_signal *cancel = nullptr) {
            std::lock_guard _(_mx);
            if (rev != _front_revision) return !_closed;
            return [cancel, this, rev](auto promise){
                //exception can be thrown from this when caller is not in ExecWorker thread
                auto result = ResultAndExecWorker(std::move(promise));
                std::lock_guard _(_mx);
                if (cancel && cancel->is_canceled()) {
                    result(false);
                    return;
                }
                if (_front_revision != rev) {
                    result(true);
                    return;
                }
                if (_closed) {
                    result(false);
                    return;
                }
                _awaiters.push_back(std::move(result));
            };            
        }

        ///cancel awaiting operation
        /**
        @param sig pointer to associated cancel signal, which serves as identification and also signal distributor. Must not be
        null, other function fails with false
        @return true if awaiter was found and coroutine was scheduled with eof indication as result, 
                false if awaiter was not found, it is possible that the coroutine is running and 
                processing some data. The function returns false in this case to avoid race condition
        
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

        ///check if stream is closed
        bool is_closed() const {
            std::lock_guard _(_mx);
            return _closed;
        }

        ///close the stream, all awaiting consumers will receive eof indication        
        void close() {
            std::lock_guard _(_mx);
            _closed = true;
             for (auto &x: _awaiters) x(false);
            _awaiters.clear();
        }


    protected:
         ///Consumer definition - registration of awaiting coroutine
        struct Item {
            T data;
            std::chrono::system_clock::time_point received;
        };
        
        std::deque<Item> _queue;
        Revision _front_revision;
        std::chrono::system_clock::duration _obsolence_interval;
        bool _closed = false;
 
    };

}