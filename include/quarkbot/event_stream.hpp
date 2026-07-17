#pragma once

#include "abstract/ieventstream.hpp"
#include "basic_coro/awaitable.hpp"
#include "basic_coro/concepts.hpp"
#include "utils/wrapper.hpp"
#include "strategy_fragment.hpp"
#include <concepts>
#include <format>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>
namespace quarkbot {

///Event stream for specific type of events - wrapper over IEventStreamBase, which provides typed access to event data
/**
@tparam T type of events, must satisfy StreamType concept. The stream provides access to event data through T::view() method

@note EventStream can be copied, which causes sharing of the stream. However keep in mind, this creates single
stream group. When the EventStream is canceled, it cancels whole group, not single instance. If you
need separate groups, subscribe separate streams
*/
template<typename T>
class EventStream : Wrapper<IEventStream<typename StreamViewType<T>::type > >{
public:
    using Wrapper<IEventStream<typename StreamViewType<T>::type > >::Wrapper;
    ///Type of view returned by T::view() method
    using ViewType = typename StreamViewType<T>::type;
    using ValueType = T;
    using value_type = T;


    static EventStream from_base(std::shared_ptr<IEventStreamBase> base) {
        auto dpc =std::dynamic_pointer_cast<IEventStream<ViewType> >(base);
        if (!dpc) {
            const auto *ptr = base.get();
            throw std::invalid_argument(std::format("Cannot construct {} from stream of type {}",
                typeid(EventStream<ViewType>).name(), typeid(*ptr).name()));
        }                
        return EventStream(std::move(dpc));
    }

    ///check if stream is open
    bool is_open() const {return this->_ptr->is_open();}
    ///close the stream
    /**
    @note if the stream is shared (created instances from single source), this function
    cancels whole group, not only this instance. 
    */
    void close() {this->_ptr->close();}
    ///conversion to bool - true if stream is open, false if closed
    operator bool() const {return is_open();}
    ///read next event, if available, and copy it to ref
    coro::awaitable<bool> receive(T &val) {
        if constexpr (HasStreamView<T>) {
            return this->_ptr->receive(val.view());
        } else {
            return this->_ptr->receive(val);
        }
    }

    EventStream &stop_on(std::stop_token tkn) & {
        this->_ptr->set_stop_token(std::move(tkn));
        return *this;
    }
    EventStream &&stop_on(std::stop_token tkn) && {
        this->_ptr->set_stop_token(std::move(tkn));
        return std::move(*this);
    }
    EventStream &stop_on(const std::stop_source &src) & {
        this->_ptr->set_stop_token(src.get_token());
        return *this;
    }
    EventStream &&stop_on(const std::stop_source &src) && {
        this->_ptr->set_stop_token(src.get_token());
        return std::move(*this);
    }

         
    ///receive next event, if available, and copy it to ref, also report count of missed events     
    coro::awaitable<bool> receive(T &val, std::size_t &missed) {return this->_ptr->next(val.view(), missed);}

    bool current(T &val) {
          if constexpr (HasStreamView<T>) {
            return this->_ptr->current(val.view());
        } else {
            return this->_ptr->current(val);
        }
    }

    auto get_handle() const {return this->_ptr;}

    ///Feed events to a hub
    /**
        @param hub target hub
        @param context contains T or a struct derived from T
        @return StrategyFragment of running coroutine
    */
    template<typename _Hub, std::derived_from<T> _ItemWithContext = T>
    requires HubProducer<_Hub, _ItemWithContext>
    StrategyFragment feed_to( _Hub hub, const _ItemWithContext &context = _ItemWithContext{}) {
        EventStream<T> me(*this);
        auto coro = [](EventStream<T> stream, _Hub hub, _ItemWithContext context) -> StrategyFragment {
            while (co_await stream.receive(context) && co_await hub.send(context));
        };
        return coro(me, std::move(hub), context);
    };

    ///Feed events to a callback
    /**
        @param cb a callback function which receives events. The callback function must return bool or any awaitable of bool (Async<bool>).
        The callback can suspend self causing to suspend whole cycle. The callback returns false if it needs to stop receiving events

        

     */
    template<std::invocable<T &> _CB>
    StrategyFragment feed_to(_CB &&cb) {
        EventStream<T> me(*this);
        using Res = std::invoke_result_t<_CB, T &>;
        if constexpr (coro::is_awaiter<Res>) {
            static_assert(std::is_convertible_v<coro::awaiter_result<Res>, bool>,
                "Callback must return bool, or awaitable<bool>");
        } else {
            static_assert(std::is_convertible_v<Res, bool>,
                "Callback must return bool, or awaitable<bool>");
        }

        auto coro = [](EventStream<T> stream, _CB cb) -> StrategyFragment {
            T v;
            if constexpr(coro::is_awaiter<Res>) {
                while (co_await stream.receive(v) && co_await cb(v));    
            } else {
                while (co_await stream.receive(v) && cb(v));
            }
        };
        return coro(me, std::forward<_CB>(cb));
    }


};




}