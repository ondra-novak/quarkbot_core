#pragma once


#include "ifc/account.hpp"
#include "ifc/defs.hpp"
#include "ifc/event_stream_publisher.hpp"
#include "ifc/publisher_base.hpp"
#include "ifc/queue_stream_publisher.hpp"
#include "ifc/stream_defs.hpp"
#include <memory>
#include <type_traits>
#include <unordered_map>
namespace quarkbot {


    template<typename Factory>
    requires(std::is_invocable_r_v<std::shared_ptr<void>, Factory, PMarketInstrument, PAccount, StreamTypeItem::Type, const StreamParams *>)
    class PublisherManager {
    public:

        struct Key {
            PMarketInstrument instrument;   //instrument
            PAccount account;               //account can be null
            StreamTypeItem::Type type;          //steam type - statically allocated
            const StreamParams *params;     //pointer to stream params (statically allocated)
            bool queue;

            bool operator==(const Key &) const = default;
            size_t hash() const {
                std::hash<const IMarketInstrument *> h1;
                std::hash<const IAccount *> h2;
                std::hash<std::string_view> h3;
                std::hash<const StreamParams *> h4;

                auto hash1 = h1(instrument.get());
                auto hash2 = h2(account.get());
                auto hash3 = h3(type);
                auto hash4 = h4(params);
                return hash1+hash2+hash3+hash4;
            }
        };

        struct KeyHash {auto operator()(const Key &k){return k.hash();}};

        using PublisherRef = std::weak_ptr<PublisherBase>;

        using MapType = std::unordered_map<Key, PublisherRef, KeyHash> ;

        PublisherManager(Factory factory):_factory(std::move(factory)) {}


        template<StreamType T, bool queue>
        auto get_publisher(const PMarketInstrument &instrument, const PAccount &acc) {
            using RetType = std::conditional_t<queue,QueueStreamPublisher<T>, EventStreamPublisher<T> >;
            Key k{instrument, acc, T::type, stream_params<T>, queue};
            std::shared_ptr<RetType> retval;
            auto iter = _map.find(k);
            if (iter == _map.end()) return retval;
            auto lk = iter->second.lock();
            if (!lk) return retval;
            retval =  std::static_pointer_cast<RetType>(lk);
            return retval;
        }

        template<StreamType T>
        auto get_event_publisher(const PMarketInstrument &instrument, const PAccount &acc) {
            return get_publisher<T, false>(instrument, acc);
        }

        template<StreamType T>
        auto get_queue_publisher(const PMarketInstrument &instrument, const PAccount &acc) {
            return get_publisher<T, true>(instrument, acc);
        }



    protected:

        Factory _factory;
        MapType _map;


    };


}
