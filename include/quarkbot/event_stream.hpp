#pragma once

#include "abstract/ieventstream.hpp"
#include "utils/wrapper.hpp"
#include "strategy_fragment.hpp"
#include <concepts>
#include <format>
#include <memory>
#include <stdexcept>
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

    template<typename _Hub, std::derived_from<T> _ItemWithContext>
    friend StrategyFragment feed_to(EventStream<T> stream, _Hub hub, _ItemWithContext context) {
        while (co_await stream.receive(context) && co_await hub.send(context));
    }



};




}