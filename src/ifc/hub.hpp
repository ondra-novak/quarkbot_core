#pragma once
#include "basic_coro/awaitable.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "ifc/defs.hpp"
#include "ifc/execution_worker.hpp"
#include "utils/init_with.hpp"
#include <deque>
#include <iterator>
#include <mutex>
#include <optional>
#include <queue>
#include <type_traits>
#include <utility>
#include <variant>
namespace quarkbot {

///Represents synchronization point - hub - for multiple coroutines
/**
    allows to transfer a value between two coroutines synchronizing them at this point.
    it also allows to retrieve values from multiple sources to single coroutine or
    broadcast value to multiple coroutines

    one side - producer - calls co_await push() to send produced value. The producer is
    blocked until the value is consumed
    other side - consumer - calls co_await pop() to retrieve a value. The consumer is
    blocked until a value is produced

    the hub can be also closed by calling close(). In this case value exchange is impossible
*/
template<typename T>
class Hub {
public:


    Hub() = default;

    ///create shared hub
    static std::shared_ptr<Hub> create() {
        return std::make_shared<Hub>();
    }


    ///send a va value, by move - after return, referenced value is undefined
    /**
        @param val value
        @return awaitable 
        @retval true value consumed
        @retval false the hub is closed
        @note by calling without co_await causes non-blocking attempt. To continue in suspend, the
        reference to the value must stay valid

        @note If caller is not running in an excution worker it is possible that will be resumed 
        in an execution worker of the consumer
    */
    awaitable<bool> send(T &&val) {
        return write_gen(std::move(val));        
    }
    ///send value, by copy - after return, reference value is unchanged
    /**
        @param val value
        @return awaitable 
        @retval true value consumed
        @retval false the hub is closed
        @note by calling without co_await causes non-blocking attempt. To continue in suspend, the
        reference to the value must stay valid

        @note If caller is not running in an excution worker it is possible that will be resumed 
        in an execution worker of the consumer

    */
    awaitable<bool> send(const T &val) {
        return write_gen(val);
    }


    ///receive value from hub
    /**
        @param val reference to a variable which receives the value
        @return awaitable. Use co_await to wait result
        @retval true value stored
        @retval false hub has been closed, no more value can be received
    */
    awaitable<bool> receive(T &val) {
        return read_gen(val);
    }

    ///receive value from hub construct the value inside of optional
    /**
        Can be used to transfer values without default constructor. Use optional as placeholder
        @param reference to an optional value
        @return awaitable. Use co_await to wait result
        @retval true value constructed
        @retval false hub has been closed, no more value can be received
    */
    awaitable<bool> receive(std::optional<T> &val) {
        return read_gen(val);
    }

    ///close the hub unblocking both sided
    /**
        Closes the hub for good.
        If there are awaiting consumers, they are resumed with no-value/canceled exception
        If there are awaiting producers, they are resumed with false status
        any futher attempt to push or pop value fails immediately
    */
    void close() {
        std::scoped_lock _(_mx);
        _closed = true;
        _producers.clear();
        _consumers.clear();
    }

    ~Hub() {
        close();
    }

private:

    class Common {
        coro::awaitable<bool>::result ntf;
        PExecutionWorker worker;
    public:
        Common(coro::awaitable<bool>::result ntf)
            :ntf(std::move(ntf))
            ,worker(IExecutionWorker::current()) {}
        ~Common() {
            resume(false);
        }
        coro::prepared_coro resume(bool value) {
            auto c = ntf(value);
            if (c && worker) worker->resume(std::move(c));
            return c;
        }
    };

    class Producer : public Common{
        std::variant<T *, const T *> source;
    public:
        Producer(const T &val, coro::awaitable<bool>::result ntf)
            :Common(std::move(ntf))
            ,source(&val) {}
        Producer(T &&val, coro::awaitable<bool>::result ntf)
            :Common(std::move(ntf))
            ,source(&val) {}
        coro::prepared_coro assign_to(T &v) {
            if (std::holds_alternative<T *>(source) ) {
                v = std::move(*std::get<T *>(source));
            } else {
                v = *std::get<const T *>(source);
            }
            return this->resume(true);
        }
        coro::prepared_coro assign_to(std::optional<T> &v) {            
            if (std::holds_alternative<T *>(source) ) {
                v.emplace(std::move(*std::get<T *>(source)));
            } else {
                v.emplace(*std::get<const T *>(source));
            }
            return this->resume(true);
        }
    };

    class Consumer : public Common{
        std::variant<T *, std::optional<T> *> target;

    public:
        Consumer(awaitable<bool>::result result, T &target)
            :Common(std::move(result))
            ,target(&target)
            {}
        Consumer(awaitable<bool>::result result, std::optional<T> &target)
            :Common(std::move(result))
            ,target(&target)
            {}

        template<typename X>
        coro::prepared_coro send(X &&val) {
            if (std::holds_alternative<std::optional<T> *>(target)) {
                std::get<std::optional<T> *>(target)->emplace(std::forward<X>(val));
            } else {
                *std::get<T *>(target) = std::forward<X>(val);
            }
            return this->resume(true);
        } 
    };

    std::mutex _mx;
    std::deque<Consumer> _consumers;
    std::deque<Producer> _producers;
    bool _closed = false;

    template<typename X>
    awaitable<bool> write_gen(X &&val) {
        //resume point for consumer
        coro::prepared_coro rsm;
        //lock internals
        std::scoped_lock _(_mx);  
        //return false if closed
        if (_closed) return false;      
        //if no consumers, prepare suspend function
        if (_consumers.empty()) return [this,&val](auto promise) {
            //resume point for consumer - returned 
            coro::prepared_coro out;
            //resume point for producer
            coro::prepared_coro rsm;
            //lock internals
            std::scoped_lock _(_mx);
            //resulve with false if closed
            if (_closed) {
                out = promise(false);
            } else if (_consumers.empty()) {
                //register producer if no consumers
                _producers.emplace_back(std::forward<X>(val), std::move(promise));
            } else {
                //pick consumer, send value
                rsm = _consumers.front().send(std::forward<X>(val));
                //pop it
                _consumers.pop_front();
                //resolve with true
                out = promise(true);
            }
            return out;
        }; else {
            //pick consumer, send value
            rsm = _consumers.front().send(std::forward<X>(val));
            //resolve with true
            _consumers.pop_front();
            //return true;
            return true;
        }
    }

    template<typename X>
    awaitable<bool> read_gen(X &target) {
        //declare resume point for producer
        coro::prepared_coro rsm;
        //lock internals
        std::scoped_lock _(_mx);
        //return false is closed
        if (_closed) return false;
        //if no producents, prepare suspend point
        if (_producers.empty()) return [this, &target](auto promise){
            //output coroutine - to resume consumer
            coro::prepared_coro out;
            //resume point - to resume producer
            coro::prepared_coro rsm;
            //lock internals
            std::scoped_lock _(_mx);            
            if (_closed) {
                //resolve with false if closed
                out = promise(false);
            } else if (_producers.empty()) {
                //register consumer if no producers
                _consumers.emplace_back(std::move(promise), target);
            } else {
                //assign its value to target
                rsm = _producers.front().assign_to(target);
                //resolve with true
                out = promise(true);
                //remove producer from queue
                _producers.pop_front();
            }
            return out;
        }; else {
            //assign to target
            rsm = _producers.front().assign_to(target);
            //remove producer from queue
            _producers.pop_front();
            //return true
            return true;
        }
    }


};

template<typename T>
using PHub = std::shared_ptr<Hub<T> >;

}