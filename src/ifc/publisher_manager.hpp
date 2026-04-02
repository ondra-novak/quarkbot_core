#pragma once


#include "ifc/account.hpp"
#include "ifc/defs.hpp"
#include "ifc/event_stream_publisher.hpp"
#include "ifc/publisher_base.hpp"
#include "ifc/queue_stream_publisher.hpp"
#include "ifc/stream.hpp"
#include "ifc/stream_defs.hpp"
#include <memory>
#include <type_traits>
#include <unordered_map>
namespace quarkbot {


    template<typename Factory>
    requires(std::is_invocable_r_v<std::shared_ptr<PublisherBase>, Factory, PMarketInstrument, PAccount, StreamTypeItem::Type, const StreamParams *>)
    class PublisherManager {
    public:
    

        struct KeyWithoutParams {
            PMarketInstrument instrument;   //instrument
            PAccount account;               //account can be null
            StreamTypeItem::Type type;          //steam type - statically allocated
            bool queue;

            bool operator==(const KeyWithoutParams &) const = default;
            size_t hash() const {
                std::hash<const IMarketInstrument *> h1;
                std::hash<const IAccount *> h2;
                std::hash<std::string_view> h3;

                auto hash1 = h1(instrument.get());
                auto hash2 = h2(account.get());
                auto hash3 = h3(type);
                return hash1 + hash2 + hash3 + static_cast<size_t>(queue);
            }
        };

        struct KeyWithoutParamsHash { auto operator()(const KeyWithoutParams &k) const { return k.hash(); } };

        using PublisherRef = std::weak_ptr<PublisherBase>;
        using InnerMap = std::unordered_map<const StreamParams *, PublisherRef>;
        using MapType = std::unordered_map<KeyWithoutParams, InnerMap, KeyWithoutParamsHash>;

        PublisherManager(Factory factory):_factory(std::move(factory)) {}


        template<StreamType T, bool queue>
        auto get_publisher(const PMarketInstrument &instrument, const PAccount &acc) {
            using RetType = std::conditional_t<queue,QueueStreamPublisher<T>, EventStreamPublisher<T> >;
            KeyWithoutParams k{instrument, acc, T::type, queue};
            auto params = stream_params<T>;
            std::shared_ptr<RetType> retval;
            auto outer_iter = _map.find(k);
            if (outer_iter == _map.end()) return retval;
            auto inner_iter = outer_iter->second.find(params);
            if (inner_iter == outer_iter->second.end()) return retval;
            auto lk = inner_iter->second.lock();
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

        // Method for connector to get all publishers for a given type, instrument, account, queue
        // Returns a vector of shared_ptr to PublisherBase for all params
        template<StreamType T, bool queue>
        std::vector<std::shared_ptr<PublisherBase>> get_all_publishers(const PMarketInstrument &instrument, const PAccount &acc) {
            KeyWithoutParams k{instrument, acc, T::type, queue};
            std::vector<std::shared_ptr<PublisherBase>> result;
            auto outer_iter = _map.find(k);
            if (outer_iter == _map.end()) return result;
            for (auto &[params, pub_ref] : outer_iter->second) {
                if (auto pub = pub_ref.lock()) {
                    result.push_back(pub);
                }
            }
            return result;
        }

        // Method to create or get a publisher (for subscribers or connectors)
        template<StreamType T, bool queue>
        auto create_or_get_publisher(const PMarketInstrument &instrument, const PAccount &acc) {
            using RetType = std::conditional_t<queue, QueueStreamPublisher<T>, EventStreamPublisher<T>>;
            KeyWithoutParams k{instrument, acc, T::type, queue};
            auto params = stream_params<T>;
            auto outer_iter = _map.find(k);
            if (outer_iter == _map.end()) {
                // Create new inner map
                InnerMap inner;
                _map[k] = std::move(inner);
                outer_iter = _map.find(k);
            }
            auto &inner = outer_iter->second;
            auto inner_iter = inner.find(params);
            if (inner_iter == inner.end() || !inner_iter->second.lock()) {
                // Create new publisher using factory
                auto pub = std::static_pointer_cast<PublisherBase>(_factory(instrument, acc, T::type, params));
                inner[params] = pub;
                return std::static_pointer_cast<RetType>(pub);
            } else {
                return std::static_pointer_cast<RetType>(inner_iter->second.lock());
            }
        }
        
        

    protected:

        Factory _factory;
        MapType _map;


    };


}
