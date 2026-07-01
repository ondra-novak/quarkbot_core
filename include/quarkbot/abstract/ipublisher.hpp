#pragma once

#include "ieventstream.hpp"
#include "../hash/class_hash.hpp"
#include <memory>
namespace quarkbot {
template<typename T>
concept StreamWithParam = requires{
    typename T::Param;
};

template<typename T>
concept StreamWithConstantParam = requires{
    typename T::Param;
    {T::param} -> std::convertible_to<typename T::Param>;
};

template<typename T>
concept StreamWithoutParam = !StreamWithParam<T>;

template<typename T> class EventStream;

class IPublisher {

public:
    virtual std::unique_ptr<IEventStreamBase> subscribe_stream(std::size_t class_hash, const void *params) = 0;
    virtual ~IPublisher() = default;

    template<typename T>    
    requires (StreamWithoutParam<T>)
    EventStream<T> subscribe() {
        auto x = this->subscribe_stream(class_hash<typename StreamViewType<T>::type>, nullptr);
        if (x) return EventStream<T>::from_base(std::move(x));
        else return EventStream<T>::create_null();
    }

    template<typename T>    
    requires (StreamWithConstantParam<T>)
    EventStream<T> subscribe() {
        auto x = this->subscribe_stream(class_hash<typename StreamViewType<T>::type>, &T::param);
        if (x) return EventStream<T>::from_base(std::move(x));
        else return EventStream<T>::create_null();
    }

    template<typename T>
    requires (StreamWithParam<T>)
    EventStream<T> subscribe(const typename T::Params &param) {
        auto x = this->subscribe_stream(class_hash<T>, &param);
        if (x) return EventStream<T>::from_base(std::move(x));
        else return EventStream<T>::create_null();
    }

};

}