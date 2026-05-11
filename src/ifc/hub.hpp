#include "basic_coro/awaitable.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "ifc/defs.hpp"
#include "ifc/execution_worker.hpp"
#include "utils/init_with.hpp"
#include <mutex>
#include <optional>
#include <queue>
#include <utility>
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
    std::shared_ptr<Hub> create() {
        return std::make_shared<Hub>();
    }

    ///push value, by move
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
    awaitable<bool> push(T &&val) {
        return push_gen(std::move(val));        
    }
    ///push value, by copy
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
    awaitable<bool> push(const T &val) {
        return push_gen(val);
    }

    ///pop value
    /**
        @return awaitable
        @noe by calling without co_await causes non-blocking attempt
        
        @note If caller is not running in an excution worker it is possible that will be resumed 
        in an execution worker of the consumer
    */
    awaitable<T> pop() {
        coro::prepared_coro rsm;
        std::scoped_lock _(_mx);
        if (_closed) return std::nullopt;
        if (_producers.empty()) return [this](auto promise){
            coro::prepared_coro out;
            coro::prepared_coro rsm;
            std::scoped_lock _(_mx);
            if (_closed) {
                out = promise(std::nullopt);
            } else if (_producers.empty()) {
                _consumers.push(std::move(promise));
            } else {
                auto &r = _producers.front();
                out = promise(r.fetch());
                rsm = r.resume(true);
                _producers.pop();
            }
            return out;
        }; else {
            return awaitable<T>(InitWith([&]{
                auto &r = _producers.front();
                T out = r.fetch();
                rsm = r.resume(true);
                _producers.pop();
                return out;
            }));

            
        }
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
        while (!_producers.empty()) {
            _producers.front().resume(false);
            _producers.pop();
        }
        while (!_consumers.empty()) _consumers.pop();
    }

    ~Hub() {
        close();
    }

private:
    class Producer {
        coro::awaitable<bool>::result ntf;
        PExecutionWorker worker;
        union {
            T *val;
            const T *cval;
        };
        bool move;    
    public:
        Producer(const T &val, coro::awaitable<bool>::result ntf)
            :ntf(std::move(ntf))
            ,worker(IExecutionWorker::current())
            ,cval(&val)
            ,move(false) {}
        Producer(T &&val, coro::awaitable<bool>::result ntf)
            :ntf(std::move(ntf))
            ,worker(IExecutionWorker::current())
            ,val(&val)
            ,move(false) {}
        Producer(Producer &&other):ntf(std::move(other.ntf)), worker(std::move(other.worker)), move(other.move) {
            if (move) val = other.val; else cval = other.cval;
        }
        ~Producer() {
            resume(false);
        }
        coro::prepared_coro resume(bool value) {
            auto c = ntf(value);
            if (c && worker) worker->resume(std::move(c));
            return c;
        }
        T fetch() {
            if (move) return T(std::move(*val)); else return T(*cval);
        }
    };

    class Consumer {
        awaitable<T>::result _result;
        PExecutionWorker _worker;

    public:
        Consumer(awaitable<T>::result result)
            :_result(std::move(result))
            ,_worker(IExecutionWorker::current()) {}
        template<typename X>
        coro::prepared_coro send(X &&val) {
            auto out = _result(std::forward<X>(val));
            if (_worker) _worker->resume(std::move(out));
            return out;
        } 
    };

    std::mutex _mx;
    std::queue<Consumer> _consumers;
    std::queue<Producer> _producers;
    bool _closed = false;

    template<typename X>
    awaitable<bool> push_gen(X &&val) {
        coro::prepared_coro rsm;
        std::scoped_lock _(_mx);  
        if (_closed) return false;      
        if (_consumers.empty()) return [this,&val](auto promise) {
            coro::prepared_coro out;
            coro::prepared_coro rsm;
            std::scoped_lock _(_mx);
            if (_closed) {
                out = promise(false);
            } else if (_consumers.empty()) {
                _producers.push(Producer(std::forward<X>(val), std::move(promise)));
            } else {
                rsm = _consumers.front().send(std::forward<X>(val));
                _consumers.pop();
                out = promise(true);
            }
            return out;
        }; else {
            rsm = _consumers.front().send(std::forward<X>(val));
            _consumers.pop();
            return true;
        }
    }


};


}