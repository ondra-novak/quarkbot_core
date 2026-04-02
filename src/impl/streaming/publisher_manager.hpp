#pragma once


#include "ifc/account.hpp"
#include "ifc/defs.hpp"
#include "publisher_base.hpp"
#include "ifc/stream_defs.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
namespace quarkbot {


    class PublisherManager {
    public:

        using PPublisher = std::shared_ptr<PublisherBase>;
        using WPPublisher = std::weak_ptr<PublisherBase>;
        using Factory = std::function<std::shared_ptr<PublisherBase>(PMarketInstrument, PAccount, StreamTypeItem::Type, const StreamParams *)>;

        struct Key {
            PMarketInstrument instrument;   //instrument
            PAccount account;               //account can be null
            StreamTypeItem::Type type;          //steam type - statically allocated

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
        

        struct KeyHash {std::size_t operator()(const Key &k) const {return k.hash();}};

        struct ValueItem {
            const StreamParams *params; //constexpr allocated params
            WPPublisher publisher; //weak reference to publisher
        };

        using Value = std::vector<ValueItem>;
        
        using MapType = std::unordered_map<Key, Value, KeyHash> ;

        PublisherManager(Factory factory):_factory(std::move(factory)) {}

        template<std::invocable<const StreamParams *, PPublisher> Callback>
        bool enum_all_publishers(const PMarketInstrument &instrument, const PAccount &account, StreamTypeItem::Type type, Callback &&cb) {
            std::scoped_lock _(_mx);
            auto iter = _map.find(Key{instrument, account, type});
            if (_map.end() == iter) return false;
            Value &v = iter->second;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const ValueItem &itm){
                auto lk =  itm.publisher.lock();
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
                                                    const StreamParams *params) {
            std::scoped_lock _(_mx);                                                    
            Key k{instrument, account, type};
            Value &v = _map[k];
            PPublisher pub;
            for (ValueItem &itm: v) {
                if (itm.params == params) {
                    pub = itm.publisher.lock();
                    if (pub) {
                        return pub->create_subscriber(pub);
                    }
                    pub = _factory(instrument, account, type, params);
                    if (pub) {
                        itm.publisher = pub;
                        return pub->create_subscriber(pub);
                    }
                    return {};
                }
            }
            pub = _factory(instrument,account, type, params);
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
