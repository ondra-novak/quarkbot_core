#pragma once
#include "signals.hpp"
#include <thread>
#include <condition_variable>
#include <queue>

#include <array>



    ///Asynchronous signal dispatcher 
    /**
     * @tparam _n_threads count of threads in the thread pool.
     * Instance of this class can be shared. You need to use create() to create actual instance.
     * This argument can be zero if you need to manually process waiting tasks
     * (this variant has its own specialization)
     */
    template<unsigned int _n_threads = 1>
    class AsyncSignalDispatcher {
    public:


        using Task = SignalConsumer<void()>;
        using PTask = std::unique_ptr<Task>;

        ///dispatch a task
        /**
         * Execute task in thread pool. 
         * 
         * @param fn function noexcept and with no arguments invocable
         * 
         * The function is queued to be executed in thread pool. If there is idle thread, the
         * function is immediatelly executed
         */
        template<typename Fn>
        requires(std::is_nothrow_invocable_v<Fn>)
        void dispatch(Fn &&fn) {
            std::lock_guard _(_core->_mx);
            if (_core->_queue.empty()) _core->_cv.notify_one();
            _core->_queue.push(std::make_unique<FunctorSignalConsumer<Fn, void()> >(std::forward<Fn>(fn)));            
        }

        ///Create dispatcher
        static AsyncSignalDispatcher create() {
            auto c = std::make_shared<DispCore>();
            c->start();
            return AsyncSignalDispatcher(std::move(c));
        }

        ///Dispatches function call (with arguments)
        /**
         * @param fn a smart pointer to function to call - for compatibility with signal slots
         * @param args arguments
         * 
         * The function creates and dispatches task which contains the function itself and 
         * arguments
         * 
         */
        template<typename Fn, typename ... Args>        
        requires(std::is_nothrow_invocable_v<decltype(*std::declval<Fn>()), Args...>)
        constexpr void operator()(Fn &&fn, Args && ... args)  {
            dispatch([fn = std::move(fn), args = std::make_tuple(std::forward<Args>(args)...)]() mutable noexcept {
                std::apply([&](auto && ... args){
                    (*fn)(std::forward<Args>(args)...);
                }, std::move(args));
            });
        }

    protected:


        struct DispCore {
            std::array<std::thread,_n_threads> _thr;
            std::array<bool *, _n_threads> _kill_flags;
            std::mutex _mx;
            std::condition_variable _cv;
            std::queue<PTask> _queue;

            DispCore()  = default;
            ~DispCore() {
                {
                    std::lock_guard _(_mx);
                    while (!_queue.empty()) _queue.pop();
                    _queue.push(nullptr);
                }
                _cv.notify_all();
                auto kitr = _kill_flags.begin();
                for (auto &t: _thr) {
                    if (std::this_thread::get_id() == t.get_id()) {
                        *(*kitr) = true;                    
                        t.detach();
                    } else {
                        t.join();
                    }
                    ++kitr;
                }
            }
            void worker(unsigned int idx) {
                bool kf = false;
                _kill_flags[idx] = &kf;
                std::unique_lock lk(_mx);
                while(true) {
                    if (_queue.empty()) {
                        _cv.wait(lk);
                    } else {
                        PTask t = std::move(_queue.front());
                        if (!t) break;
                        _queue.pop();
                        lk.unlock();
                        t->operator()();
                        if (kf) return;
                        lk.lock();                        
                    }
                }
            }
            void start() {
                for (unsigned int i = 0; i < _n_threads; ++i) {
                    _thr[i] = std::thread([this,i]{worker(i);});
                }
            }
        };

        AsyncSignalDispatcher(std::shared_ptr<DispCore> core) :_core(std::move(core)) {}


        std::shared_ptr<DispCore> _core;

    };


    template<>
    class AsyncSignalDispatcher<0> {
    public:


        using Task = SignalConsumer<void()>;
        using PTask = std::unique_ptr<Task>;

        ///dispatch a task
        /**
         * Execute task in thread pool. 
         * 
         * @param fn function noexcept and with no arguments invocable
         * 
         * The function is queued to be executed in thread pool. If there is idle thread, the
         * function is immediatelly executed
         */
        template<typename Fn>
        requires(std::is_nothrow_invocable_v<Fn>)
        void dispatch(Fn &&fn) {
            std::lock_guard _(_core->_mx);
            _core->_queue.push(std::make_unique<FunctorSignalConsumer<Fn, void()> >(std::forward<Fn>(fn)));            
        }

        ///Create dispatcher
        static AsyncSignalDispatcher create() {
            auto c = std::make_shared<DispCore>();
            return AsyncSignalDispatcher(std::move(c));
        }

        ///Dispatches function call (with arguments)
        /**
         * @param fn a smart pointer to function to call - for compatibility with signal slots
         * @param args arguments
         * 
         * The function creates and dispatches task which contains the function itself and 
         * arguments
         * 
         */
        template<typename Fn, typename ... Args>        
        requires(std::is_nothrow_invocable_v<decltype(*std::declval<Fn>()), Args...>)
        constexpr void operator()(Fn &&fn, Args && ... args)  {
            dispatch([fn = std::move(fn), args = std::make_tuple(std::forward<Args>(args)...)]() mutable noexcept {
                std::apply([&](auto && ... args){
                    (*fn)(std::forward<Args>(args)...);
                }, std::move(args));
            });
        }

        ///Manually run enqueued task
        /**
         * Allows manual synchronous dispatching
         * 
         * @retval true a task has been processed
         * @retval false no more tasks
         */
        bool pump_one() {
            return _core->run_enqueued_task();
        }
        void pump_all() {
            while (pump_one());
        }


    protected:


        struct DispCore {
            std::mutex _mx;
            std::queue<PTask> _queue;

            bool run_enqueued_task() {
                std::unique_lock lk(_mx);
                if (_queue.empty()) return false;
                PTask t = std::move(_queue.front());
                if (!t) return false;
                _queue.pop();
                lk.unlock();
                t->operator()();
                return true;
            }
        };

        AsyncSignalDispatcher(std::shared_ptr<DispCore> core) :_core(std::move(core)) {}

        std::shared_ptr<DispCore> _core;

    };

    

