#pragma once

#include "execution_worker.hpp"
#include <coroutine>
#include <stop_token>

namespace quarkbot {


    class stop_awaiter {
    public:
        stop_awaiter(std::stop_token tkn):tkn(std::move(tkn)) {}
        //awaiter is not movable, it is intended to be created temporarily during co_await and destroyed immediately after resumption, so no need to support move semantics
        stop_awaiter(stop_awaiter &&other) = delete;
        stop_awaiter &operator=(stop_awaiter &&other) = delete;
        ~stop_awaiter() {/* nothing to delete here, but union requires a destructor*/};

        bool await_ready() const {return tkn.stop_requested();}
        void await_suspend(std::coroutine_handle<> h) {
            std::construct_at(&callback,tkn, CB{h, ExecutionWorker::current()});            
        }
        void await_resume() {
            std::destroy_at(&callback);
        }

    protected:

        struct CB {
            std::coroutine_handle<> h;
            ExecutionWorker worker;
            void operator()() {
                if (worker) worker.resume(h);
                else h.resume();
            }
        };


    public:
        std::stop_token tkn;
        union {
            //valid only during co_await
            std::stop_callback<CB> callback;
        };
    };

    ///Awaitable stop token that can be co_awaited, resuming the awaiting coroutine when stop is requested
    class AwaitableStopToken: public std::stop_token {
    public:
        using std::stop_token::stop_token;
        AwaitableStopToken(std::stop_token tkn):std::stop_token(std::move(tkn)) {}
        
        ///co_await operator that returns an awaiter which will resume the awaiting coroutine when stop is requested
        stop_awaiter operator co_await() const {
            return stop_awaiter(*this); 
        }
        ///create awaiter without co_await, useful for manual awaiting in custom await_suspend implementations
        stop_awaiter operator ()() const {
            return stop_awaiter(*this); 
        }
    };


}