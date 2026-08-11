#pragma once
#include "basic_coro/concepts.hpp"
#include "basic_coro/coro_frame.hpp"
#include "basic_coro/coroutine.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "log.hpp"
#include <coroutine>
#include <source_location>
#include "execution_worker.hpp"
#include "quarkbot/utils/magazine_vmem_pool.hpp"
#include "quarkbot/utils/string_utils.hpp"
namespace quarkbot {

    template<typename T>
    concept AwaitTransformDisabled =  requires {
        typename T::no_transform_awaiter;
    };

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
            _frame._worker = ExecutionWorker::current();
            if (!_frame._worker) {
                throw std::runtime_error(
                    std::format("No Execution Worker is associated with the current coroutine or thread. "
                        "This operation requires an active Execution Worker context."));                    
            }
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
                _frame._awaiting = {};
                target.destroy();
                throw;
            }
        }
        
        decltype(auto) await_resume() {
            return _awt.await_resume();
        }


    protected:
        struct Frame: coro::coro_frame<Frame> {
            ExecutionWorker _worker;
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



    template<typename T>
    class Async : public coro::coroutine<T> {
    public:

    class promise_type: public coro::coroutine<T>::promise_type {
        public:            
            Logger::Location coro_location;            

            void *operator new(std::size_t sz) {return MagazineVMemAllocator::allocate(sz);}
            void operator delete(void *ptr, std::size_t sz) {return MagazineVMemAllocator::deallocate(ptr, sz);}
            
            
            void (*old_resume)(std::coroutine_handle<promise_type>) = nullptr;

            static std::string_view short_name(std::string_view n){
                auto sz = n.size();
                int bc = 0;
                while (true) {
                    if (n.starts_with("static ")) {
                       n = trim(n.substr(7));
                    } else if (n.starts_with("struct ")) {
                        n = trim(n.substr(7));
                    } else if (n.starts_with("class ")) {
                        n = trim(n.substr(6));
                    } else if (n.starts_with("enum ")) {
                        n = trim(n.substr(5));
                    } else break;                
                }
                for (auto i = sz-sz; i < sz; ++i) {
                    char c = n[i];
                    if (isspace(c) && bc == 0) {
                        n = n.substr(i);
                        break;
                    }
                    if (c == '<') bc++;
                    if (c == '>') bc--;                
                }
                n = trim(n);
                if (n.starts_with("__cdecl")) n.remove_prefix(7);
                auto rc = n.find('(');
                if (rc != n.npos) n = n.substr(0,rc);
                n = trim(n);
                return n;
            }
        

            static void debug_traced_resume(std::coroutine_handle<promise_type> h) {
                auto &me = h.promise();
                logOutputCB(LogLevel::trace, [&]{
                    return std::pair(me.coro_location,std::format("Fragment is running: {}",me.coro_location.function));
                });
                me.old_resume(h);
            }



            std::suspend_always initial_suspend(std::source_location loc = std::source_location::current())  noexcept {
                this->coro_location = Logger::from(loc);
                this->coro_location.function =short_name(loc.function_name());
                logOutputCB(LogLevel::trace,  [&]{
                    auto h = std::coroutine_handle<promise_type>::from_promise(*this);
                    auto frame_ptr = reinterpret_cast<void (**)(std::coroutine_handle<promise_type>) >(h.address());
                    old_resume = *frame_ptr;
                    *frame_ptr = &debug_traced_resume;
                    return std::pair(Logger::from(loc),std::format("Fragment created: {}", coro_location.function));
                });
                return {};
            }        

            ~promise_type() {
                logOutputCB(LogLevel::trace, [&]{
                    return std::pair(coro_location,std::format("Fragment finished: {}", coro_location.function));
                });
            }

            template<typename _Awt>
            requires (!AwaitTransformDisabled<std::decay_t<_Awt> >)
            auto await_transform(_Awt &&awt) {
                if constexpr(coro::is_awaiter<_Awt>) {
                    return TransformedAwaiterExecWorker<_Awt &&>(std::forward<_Awt>(awt));
                } else {
                    return TransformedAwaiterExecWorker<coro::extract_awaiter_t<_Awt> >(awt);
                }
            }
            template<typename _Awt>
            requires (AwaitTransformDisabled<std::decay_t<_Awt> >)
            decltype(auto) await_transform(_Awt &&x) {return std::forward<_Awt>(x);}
            auto await_transform(std::suspend_always &awt) {return awt;}
            auto await_transform(std::suspend_always &&awt) {return awt;}
            auto await_transform(std::suspend_never &awt) {return awt;}
            auto await_transform(std::suspend_never &&awt) {return awt;}
            auto await_transform(typename coro::coroutine<T>::promise_type::finisher &awt) {return awt;}
            auto await_transform(typename coro::coroutine<T>::promise_type::finisher &&awt) {return awt;}
            
        };

        Async() = default;
        Async(coro::coroutine<T> x):coro::coroutine<T>(std::move(x)) {}

        class set_location {
            Logger::Location loc;
        public:
            struct no_transform_awaiter{};
            set_location(const set_location &) = delete;
            set_location &operator=(const set_location &) = delete;
            set_location(std::string_view function, std::string_view file,  uint_least32_t line)
                :loc(file,function,line) {}
            static constexpr bool await_ready() {return false;}
            bool await_suspend(std::coroutine_handle<> h) {
                void *addr = h.address();
                auto hp = std::coroutine_handle<promise_type>::from_address(addr);
                auto &promise = hp.promise();
                logOutputCB(LogLevel::trace, [&]{
                    return std::pair(loc,std::format("Fragment {} transfered to {} ", promise.coro_location.function, loc.function));
                });
                promise.coro_location = loc;
                return false;
            }
            static constexpr void await_resume() {}            
        };
    };


}