#pragma once

#include "market_event.h"
#include "shared/cluster_alloc.h"
#include "shared/cluster_alloc_reference.h"
#include <any>

namespace quarkbot {





///Fast allocator of market events
/**
 * @tparam T class representing market event
 *
 * The class uses fast allocator to allocate and reuse nolonger used memory
 * The class is not MT Safe. It is expected that allocated market event is read only
 * and its deallocation is MT safe.
 */
template<typename T>
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
        return std::allocate_shared<Event>(ClusterAllocReference<Event,8,true>(&_instance), std::forward<Args>(args)...);
    }


protected:

    ///instance of the allocator
    std::any _instance;
};



}
