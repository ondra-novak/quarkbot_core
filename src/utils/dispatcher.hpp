#pragma once


#include <concepts>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
class Dispatcher {
public:

    static constexpr std::size_t closure_size = sizeof(void *) * 6;

    struct Task {
        void (*resume)(void *ctx);
        void (*destroy)(void *ctx);
        char data[closure_size] = {};
    };

    ///start dispatcher for current thread
    static void start() {
        if (!instance) {
            instance = std::make_shared<Dispatcher>();
        }
    }

    ///get instance if exists
    static auto get_instance() {
        return instance;
    }

    static bool dispatch_one() {
        Dispatcher *me = instance.get();
        if (!me) return false;
        Task *p;
        {
            std::lock_guard _(me->_mx);
            if (me->_q.empty()) return false;
            p = &(me->_q.front());            
        }
        p->resume(p->data);
        p->destroy(p->data);
        {
            std::lock_guard _(me->_mx);
            me->_q.pop_front();
        }
        return true;
    }

    bool enqueue(std::invocable<> auto fn) {        
        using FN = std::decay_t<decltype(fn)>;
        if constexpr(sizeof(FN) <= closure_size ) {
            std::lock_guard _(_mx);            
            _q.push_back({[](void *ctx){
                //resume
                FN *ptr = static_cast<FN *>(ctx);
                std::invoke(*ptr);
            }, [](void *ctx){
                FN *ptr = static_cast<FN *>(ctx);
                std::destroy_at(ptr);
            }});
            void *ctx = _q.back().data;
            FN *ptr = static_cast<FN *>(ctx);
            std::construct_at(ptr, std::move(fn));
            _enqueue_task.resume(_enqueue_task.data);
        } else {
            return enqueue([fnptr = std::make_unique<FN>(std::move(fn))]()mutable{
                std::invoke(*fnptr);
            });
        }
        return true;
    }

    template<std::invocable<> Fn>
    requires(!std::is_reference_v<Fn> && sizeof(Fn) <= closure_size)
    static void set_enqueue_ntf(Fn fn) {
        auto inst = get_instance();
        inst->set_task(fn, inst->_enqueue_task);
    }

    template<std::invocable<> Fn>
    requires(!std::is_reference_v<Fn> && sizeof(Fn) <= closure_size)
    static void set_exception_ntf(Fn fn) {
        auto inst = get_instance();
        inst->set_task(fn, inst->_exception_task);
    }

    static bool report_exception() {
        auto disp = instance;
        if (disp) {
            disp->_exception_task.resume(disp->_exception_task.data);
            return true;
        }
        return false;
    }

    Dispatcher() = default;
    ~Dispatcher() {
        while (!_q.empty()) {
            Task &t = _q.front();
            t.destroy(t.data);
            _q.pop_front();
        }
        _enqueue_task.destroy(_enqueue_task.data);
        _exception_task.destroy(_exception_task.data);
    }


protected:

    std::mutex _mx;
    std::deque<Task> _q;
    Task _enqueue_task = {[](void *){}, [](void *){}};
    Task _exception_task =  {[](void *){}, [](void *){}};

    static thread_local std::shared_ptr<Dispatcher> instance;


    template<std::invocable<> Fn>
    requires(!std::is_reference_v<Fn> && sizeof(Fn) <= closure_size)
    void set_task(Fn fn, Task &t) {
        std::lock_guard _(_mx);
        void *ctx = t.data;
        Fn *ptr = static_cast<Fn *>(ctx);
        std::construct_at(ptr, std::move(fn));
        t.resume = [](void *ctx) {
            Fn *fn = static_cast<Fn *>(ctx);
            std::invoke(*fn);
        };
        t.destroy = [](void *ctx) {
            Fn *fn = static_cast<Fn *>(ctx);
            std::destroy_at(fn);;
        };
    }


};

inline thread_local std::shared_ptr<Dispatcher> Dispatcher::instance =  {};

