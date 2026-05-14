#pragma once

#include "basic_coro/awaitable_transform.hpp"
#include "ifc/defs.hpp"
#include "ifc/streaming.hpp"
#include <type_traits>
namespace quarkbot {


///Predicate for stream data transformations
/**
@param @1 const reference to source value / last known value
@param @2 reference to result
@param @3 boolean flag, if this flag is true, new value has been delivered.
@retval true transformation is done, publish the result
@retval false transformation is not done yet, we need more values. if @3 is false, it causes function current() returns false

@note if @3 is true, you can use @2 to store partial results and return false - value will not be published. 
The variant with @3 = false can happen only if previous call published the value

*/
template<typename T, typename From, typename To>
concept StreamTranformPreficate = std::is_invocable_r_v<bool, T, const From &, To &, bool>;


    ///Implementation of stream transform 
/**
@see transform_stream
*/
template<typename To,typename From,StreamTranformPreficate<From, To> Pred>
class StreamTransform: public IEventStream<To> {
public:

    StreamTransform(EventStream<From> source, Pred pred)
        :_pred(std::move(pred)), _src_stream(std::move(source)) {}

protected:
    Pred _pred;
    EventStream<From> _src_stream;
    From _src_val;    
    std::size_t _missed = 0;

    struct TrnFn {
        StreamTransform *_owner;
        To *_to;
        awaitable<bool> operator()(bool val) {
            if (!val) return val;
            if (!_owner->_pred(_owner->_src_val, *_to, true)) {
                return _owner->read_internal(*_to, _owner->_missed);
            }
            return true;
        }
    };

    coro::awaitable_transform<coro::awaitable<bool>, TrnFn> _async_trn;

    virtual coro::awaitable<bool> read_internal(To &ref, std::size_t &missed) {
        return _async_trn(_src_stream.next(_src_val, missed),TrnFn{this, &ref});
    }
    virtual bool current(To &ref) {
        return _pred(_src_val, ref, false);
    }
    virtual bool is_open() const {
        return _src_stream.is_open();
    }
    virtual void close() {
        return _src_stream.close();
    }
};

///Transform stream by a predicate
/**
@tparam To target type
@param source source stream
@param pred predicate. See StreamTranformPreficate concept documentation

@see StreamTranformPreficate;

*/
template<typename To, typename From, StreamTranformPreficate<From, To> Pred>
EventStream<To> transform_stream(EventStream<From> source, Pred pred) {
    return {
        std::make_unique<StreamTransform<To, From, Pred> >(std::move(source), std::move(pred))
    };
}


}