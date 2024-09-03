#pragma once

#include "market_event.h"
#include "shared/cluster_alloc.h"
#include <any>

namespace quarkbot {


template<typename T>
class MarketEventAllocator { // @suppress("Miss copy constructor or assignment operator")
public:

    using value_type = T;

    using Allocator = ClusterAlloc<T,8,true>;

    MarketEventAllocator() = delete;
    MarketEventAllocator(std::any *instance):_instance(instance) {}

    template<typename Q>
    MarketEventAllocator(const MarketEventAllocator<Q> &other)
        :_instance(other._instance) {}


    T *allocate(int n) {
        if (!_instance->has_value())  {
            _instance->emplace<Allocator>();
        }
        auto &a = std::any_cast<Allocator &>(*_instance);
        return a.allocate(n);
    }

    void deallocate(T *ptr, int n) {
        auto &a = std::any_cast<Allocator &>(*_instance);
        return a.deallocate(ptr,n);
    }

protected:

    template<typename Q>
    friend class MarketEventAllocator ;

    std::any *_instance;
};



///Fast allocator of market events
/**
 * @tparam _type type of market event
 * @tparam T class representing market event
 *
 * The class uses fast allocator to allocate and reuse nolonger used memory
 * The class is not MT Safe. It is expected that allocated market event is read only
 * and its deallocation is MT safe.
 */
template<MarketEventType _type, typename T>
class MarketEventFactory {
public:

    ///Event storage
    class Event : public IMarketEvent {
    public:
        template<typename ... Args>
        Event(Args &&... args):_content(std::forward<Args>(args)...) {}

        ///retrieve content
        /**
         * @return reference to content
         * You can use the reference to set fields after allocation
         *
         * Don't change fields when event is already broadcasted
         */
        T &content() {return _content;}
        ///convert object to content reference
        /**
         * @return reference to content
         * You can use the reference to set fields after allocation
         * Don't change fields when event is already broadcasted
         */
        operator T &() {return _content;}
        virtual bool retrieve_value(const std::type_info &type, void *ptr, std::size_t sz) const {
            if (type == typeid(T) && sz == sizeof(T)) {
                T *target = reinterpret_cast<T *>(ptr);
                *target = _content;
                return true;
            }
            return false;

        }
        virtual void retrieve_optional(const std::type_info &type, void *ptr, std::size_t sz) const {
            if (type == typeid(T) && sz == sizeof(std::optional<T>)) {
                auto target = reinterpret_cast<std::optional<T> *>(ptr);
                target->emplace(_content);
            }
        }
        virtual bool contains(const std::type_info &type) const {
            return type == typeid(T);
        }
        virtual MarketEventType type() const {
            return _type;
        }
        virtual void dump(std::ostream &s) const {
            if constexpr(can_output_to_ostream<T>) {
                s << _content;
            } else {
                s << "<" << typeid(T).name() << ">";
            }
        }
    protected:
        T _content;
    };


    ///Create new market event object
    /**
     * @param args arguments for construction
     * @return market event object. This can be passed to MarketEvent constructor
     * for broadcasting
     */
    template<typename ... Args>
    std::shared_ptr<Event> create(Args && ... args)  {
        return std::allocate_shared<Event>(MarketEventAllocator<Event>(&_instance), std::forward<Args>(args)...);
    }


protected:

    ///instance of the allocator
    std::any _instance;
};



}
