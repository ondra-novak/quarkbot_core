#pragma once


#include "ifc/account.hpp"
#include "ifc/defs.hpp"
#include "ifc/publisher_base.hpp"
#include "ifc/stream.hpp"
#include "ifc/stream_defs.hpp"
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
namespace quarkbot {


    template<typename Factory>
    requires(std::is_invocable_r_v<std::shared_ptr<PublisherBase>, Factory, /*(*/ PMarketInstrument, PAccount, StreamTypeItem::Type, const StreamParams *, bool /*)*/>)
    class PublishingMaps {
    public:

        using PPublisher = std::shared_ptr<PublisherBase>;
        using WPPublisher = std::weak_ptr<PublisherBase>;

        struct Key {
            PMarketInstrument instrument;   //instrument
            PAccount account;               //account can be null
            StreamTypeItem::Type type;          //steam type - statically allocated
            bool queue;

            bool operator==(const Key &) const = default;
            size_t hash() const {
                std::hash<const IMarketInstrument *> h1;
                std::hash<const IAccount *> h2;
                std::hash<std::string_view> h3;                

                auto hash1 = h1(instrument.get());
                auto hash2 = h2(account.get());
                auto hash3 = h3(type);
                return hash1+hash2+hash3;
            }
        };
        

        struct KeyHash {auto operator()(const Key &k){return k.hash();}};

        struct ValueItem {
            const StreamParams *params; //constexpr allocated params
            WPPublisher publisher; //weak reference to publisher
        };

        using Value = std::vector<ValueItem>;
        
        using MapType = std::unordered_map<Key, Value, KeyHash> ;

        PublishingMaps(Factory factory):_factory(std::move(factory)) {}

        template<std::invocable<const StreamParams *, PPublisher> Callback>
        bool enum_all_publishers(const PMarketInstrument &instrument, const PAccount &account, StreamTypeItem::Type type, bool queue, Callback &&cb) {
            std::scoped_lock _(_mx);
            auto iter = _map.find(Key{instrument, account, type, queue});
            if (_map.end() == iter) return false;
            Value &v = iter->second;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const ValueItem &itm){
                auto lk =  itm.publisher.expired();
                if (lk) {
                    std::invoke(std::forward<Callback>(cb), itm.params, lk);
                    return false;
                } 
                return true;
            }), v.end());

            if (v.empty()) {
                _map.erase(iter);
                return false;
            }
            return true;            
        }

        std::shared_ptr<IEventStreamBase> connect_to(const PMarketInstrument &instrument,
                                                    const PAccount &account,
                                                    StreamTypeItem::Type type,
                                                    const StreamParams *params,
                                                    bool queue
                                                ) {
            std::scoped_lock _(_mx);                                                    
            Key k{instrument, account, type, queue};
            Value &v = _map[k];
            PPublisher pub;
            for (ValueItem &itm: v) {
                if (itm.params == params) {
                    pub = itm.publisher.lock();
                    if (pub) {
                        return pub->create_subscriber(pub);
                    }
                    pub = _factory(instrument, account, type, params, queue);
                    if (pub) {
                        itm.publisher = pub;
                        return pub->create_subscriber(pub);
                    }
                    return {};
                }
            }
            pub = _factory(instrument,account, type, params, queue);
            if (pub) {
                v.push_back({params, pub});                
                return pub->create_subscriber(pub);
            }
            return {};            
        }

    protected:
        std::mutex _mx;
        Factory _factory;
        MapType _map;


    };


}
