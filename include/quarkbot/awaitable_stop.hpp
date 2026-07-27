#pragma once


#include <coroutine>
#include <stop_token>

namespace quarkbot {


    class stop_awaiter {
    public:
        stop_awaiter(std::stop_token tkn):tkn(std::move(tkn)) {}
        stop_awaiter(stop_awaiter &&other) = delete;
        stop_awaiter &operator=(stop_awaiter &&other) = delete;
       
        bool await_ready() const {return tkn.stop_requested();}
        void await_suspend(std::coroutine_handle<> h) {
            callback.emplace(tkn, CB{h});            
        }
        void await_resume() {
            callback.reset(); //remove callback, it is no longer needed
        }

    protected:

        struct CB {
            std::coroutine_handle<> h;
            void operator()() {
                h.resume();
            }
        };


    public:
        std::stop_token tkn;
        std::optional<std::stop_callback<CB> >callback;
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