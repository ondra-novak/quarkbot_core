#include "check.h"
#include "../utils/coro_dispatch.hpp"
#include "coro/src/basic_coro/awaitable.hpp"
#include "coro/src/basic_coro/coroutine.hpp"
#include "coro/src/basic_coro/trace.hpp"
#include "utils/dispatcher.hpp"
#include <chrono>
#include <thread>
#ifdef BASIC_CORO_ENABLE_TRACE
#include <coroutine>
#include <source_location>
#endif

#ifdef BASIC_CORO_ENABLE_TRACE

template<typename IO> IO &operator << (IO &io, std::coroutine_handle<> h) {
    io << h.address();
    return io;
}

template<typename IO> IO &operator << (IO &io, const std::source_location &loc) {
    io << loc.function_name() << "(" << loc.file_name() << ":" <<  loc.line() <<")";
    return io;
}

void basic_coro_trace_create(std::coroutine_handle<> h) noexcept {
    std::cout << "Create coro: " << h << std::endl;
}
void basic_coro_trace_destroy(std::coroutine_handle<> h) noexcept {
    std::cout << "Destroy coro: " << h << std::endl;
}
void basic_coro_trace_init(std::coroutine_handle<> h, std::source_location loc) noexcept {
    std::cout << "Init coro: " << h << " at " << loc << std::endl;
}

void basic_coro_trace_suspend(std::coroutine_handle<> h, std::source_location loc) noexcept {
    std::cout << "Suspend coro: " << h << " at " << loc << std::endl;
}
void basic_coro_trace_resume(std::coroutine_handle<> h) noexcept {
    std::cout << "Resume coro: " << h << std::endl;
}
void basic_coro_trace_exception(std::coroutine_handle<> h) noexcept {
    std::cout << "Exception coro: " << h << std::endl;
}
void basic_coro_trace_setname(std::coroutine_handle<> h, std::string_view name) noexcept {
    std::cout << "Set name of  coro: " << h << " = " << name <<  std::endl;
}
#endif

coro::awaitable<int> api_call_run() {
    return [](auto promise) {
        auto disp_promise = CoroDispatchProxy<int>(std::move(promise));
        std::thread thr([disp_promise = std::move(disp_promise)]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            disp_promise(42);
        });
        thr.detach();

    };
}


coro::awaitable<int> run_fn() {
    co_await coro::set_name("standard run, resume from other thread");
    auto id1 = std::this_thread::get_id();    
    int r = co_await api_call_run();
    auto id2 = std::this_thread::get_id();    
    CHECK(id1 == id2);
    co_return r;
}

coro::coroutine<int> run_exception_fn(bool &done) {
    co_await coro::set_name("exception run");
    int r = co_await api_call_run();
    done = true;
    if (r == 42) throw r;
    co_return 0;    
}

int main() {
    bool enqueued = false;
    bool exception = false;
    Dispatcher::start();
    Dispatcher::set_enqueue_ntf([&]{enqueued = true;});
    coro::async_unhandled_exception = []{
        Dispatcher::report_exception();
    };
    Dispatcher::set_exception_ntf([&]{
        exception = true;
        try {
            throw;
        } catch (int e) {
            CHECK_EQUAL(e, 42);
        };
    });
    

    auto r = run_fn();
    bool done;
    ([&]()->coro::coroutine<void> {
         co_await r; done = true;
    })();
    while (!done) {
        Dispatcher::dispatch_one();
    }


    bool e_done;
    run_exception_fn(e_done);
    while (!e_done) {
        Dispatcher::dispatch_one();
    }



    CHECK_EQUAL(r.get(), 42);
    CHECK(enqueued);
    CHECK(exception);
}