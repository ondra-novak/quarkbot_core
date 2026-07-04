#pragma once
#include "basic_coro/concepts.hpp"
#include "basic_coro/coro_frame.hpp"
#include "basic_coro/coroutine.hpp"
#include "memory.hpp"
#include <coroutine>
#include <source_location>
#include "execution_worker.hpp"
namespace quarkbot {

    template<coro::is_awaiter _Awt>
    class TransformedAwaiterExecWorker   {
    public:
        TransformedAwaiterExecWorker(_Awt && awt):_awt(std::forward<_Awt>(awt)) {}

        template<typename T>        
        TransformedAwaiterExecWorker(T &x):_awt(coro::extract_awaiter(x)) {}

        TransformedAwaiterExecWorker(const TransformedAwaiterExecWorker &&)  = delete;
        TransformedAwaiterExecWorker &operator=(const TransformedAwaiterExecWorker &&)  = delete;
        
        bool await_ready() const {
            return _awt.await_ready();
        }
        
        auto await_suspend(std::coroutine_handle<> h)  {
            
            using RetT = decltype(_awt.await_suspend(h));
            _frame._worker = ExecutionWorker::current().required();;
            auto target = _frame.create_handle();
            _frame._awaiting = h;
            try {
                if constexpr(std::is_convertible_v<RetT,std::coroutine_handle<> >) {
                    auto r = _awt.await_suspend(target);
                    if (r != target) { 
                        return r;                
                    }
                    _frame._awaiting = {};
                    //no suspend happened destroy handle
                    target.destroy();
                    //return original handle
                    return h; 
                } else if constexpr(std::is_convertible_v<RetT, bool>) {                
                    bool b = _awt.await_suspend(target);
                    if (b) {
                        return true;
                    }
                    _frame._awaiting = {};
                    //no suspend happened - destroy target
                    target.destroy();
                    //return false;
                    return false;
                } else {
                    //always suspend
                    _awt.await_suspend(target);
                    return;
                }
            } catch (...) {
                target.destroy();
                throw;
            }
        }
        
        decltype(auto) await_resume() {
            return _awt.await_resume();
        }


    protected:
        struct Frame: coro::coro_frame<Frame> {
            ExecutionWorker _worker{nullptr};
            std::coroutine_handle<> _awaiting = {};
            void do_resume() {
                _worker.resume(_awaiting);            
            }
            void do_destroy() {
                if (_awaiting) _awaiting.destroy();
            }
        };
        _Awt _awt;
        Frame _frame;




        
    };

    ///Asynchronous function which can be co_awaited, used for concurrent execution of strategy fragments and other asynchronous operations
    /**
        This is a coroutine which is managed by quarkbot interface. 
        The function can use co_await. It must use co_return to returns value

        @tparam T type of return value

        To call this function and retrieve the return value you need to use co_await fn(...). You can
        call such function from anothe Async or from StrategyFragment

    */

    class IAsyncDebugTrace {
    public:

        virtual void add(std::coroutine_handle<> h, const std::source_location &loc) = 0;
        virtual void remove(std::coroutine_handle<> h) = 0;
        virtual void resumed(std::coroutine_handle<> h) = 0;        
        virtual ~IAsyncDebugTrace() = default;
        static IAsyncDebugTrace *trace;
    };

    inline IAsyncDebugTrace * IAsyncDebugTrace::trace = nullptr;


    template<typename T>
    class Async : public coro::coroutine<T> {
    public:

    class promise_type: public coro::coroutine<T>::promise_type {
        public:            
            void *operator new(std::size_t sz) {return LockFreeFramePool::allocate(sz);}
            void operator delete(void *ptr, std::size_t sz) {return LockFreeFramePool::deallocate(ptr, sz);}
            
            
            void (*old_resume)(std::coroutine_handle<promise_type>) = nullptr;

            static void debug_traced_resume(std::coroutine_handle<promise_type> h) {
                IAsyncDebugTrace::trace->resumed(h);
                auto &me = h.promise();
                me.old_resume(h);
            }

            std::suspend_always initial_suspend(std::source_location loc = std::source_location::current())  noexcept {
                if (IAsyncDebugTrace::trace) {
                    auto h = std::coroutine_handle<promise_type>::from_promise(*this);
                    IAsyncDebugTrace::trace->add(h, loc);                
                    auto frame_ptr = reinterpret_cast<void (**)(std::coroutine_handle<promise_type>) >(h.address());
                    old_resume = *frame_ptr;
                    *frame_ptr = &debug_traced_resume;
                }
                return {};
            }        

            ~promise_type() {
                if (IAsyncDebugTrace::trace) IAsyncDebugTrace::trace->remove(std::coroutine_handle<promise_type>::from_promise(*this));
            }

            template<typename _Awt>
            auto await_transform(_Awt &&awt) {
                if constexpr(coro::is_awaiter<_Awt>) {
                    return TransformedAwaiterExecWorker<_Awt &&>(std::forward<_Awt>(awt));
                } else {
                    return TransformedAwaiterExecWorker<coro::extract_awaiter_t<_Awt> >(awt);
                }
            }
            auto await_transform(std::suspend_always &awt) {return awt;}
            auto await_transform(std::suspend_always &&awt) {return awt;}
            auto await_transform(std::suspend_never &awt) {return awt;}
            auto await_transform(std::suspend_never &&awt) {return awt;}
            auto await_transform(typename coro::coroutine<T>::promise_type::finisher &awt) {return awt;}
            auto await_transform(typename coro::coroutine<T>::promise_type::finisher &&awt) {return awt;}
            
        };

        Async() = default;
        Async(coro::coroutine<T> x):coro::coroutine<T>(std::move(x)) {}
    };


}