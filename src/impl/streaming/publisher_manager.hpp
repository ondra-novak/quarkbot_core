#pragma once


#include "ifc/account.hpp"
#include "ifc/defs.hpp"
#include "publisher_base.hpp"
#include "ifc/stream_defs.hpp"
#include "utils/hashable.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
namespace quarkbot {


    template<typename InstrumentKey = PMarketInstrument>
    class PublisherManager {
    public:

        using PPublisher = std::shared_ptr<PublisherBase>;
        using WPPublisher = std::weak_ptr<PublisherBase>;

        struct Key {
            InstrumentKey instrument;   //instrument
            PAccount account;               //account can be null
            StreamTypeItem::Type type;          //steam type - statically allocated

            bool operator==(const Key &) const = default;
            size_t hash() const {
                Hasher<InstrumentKey> h1;
                std::hash<const IAccount *> h2;
                std::hash<std::string_view> h3;                

                auto hash1 = h1(instrument);
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


        template<std::invocable<const StreamParams *, PPublisher> Callback>
        bool enum_all_publishers(const InstrumentKey &instrument, const PAccount &account, StreamTypeItem::Type type, Callback &&cb) {
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

        bool any_publisher(const InstrumentKey &instrument, const PAccount &account, StreamTypeItem::Type type) const {
            std::scoped_lock _(_mx);
            auto iter = _map.find(Key{instrument, account, type});
            return _map.end() != iter;
        }

        template<std::invocable<> Factory>
        std::unique_ptr<IEventStreamBase> connect_to(const InstrumentKey &instrument,
                                                    const PAccount &account,
                                                    StreamTypeItem::Type type,
                                                    const StreamParams *params,
                                                    Factory factory
                                                ) {
            static_assert(std::is_invocable_r_v<PPublisher, Factory>);
            std::scoped_lock _(_mx);                                                    
            Key k{instrument, account, type};
            Value &v = _map[k];
            PPublisher pub;
            auto r = std::remove_if(v.begin(), v.end(), [&](const ValueItem v){
                if (v.params == params) {
                    pub = v.publisher.lock();
                    return !pub;
                }
                return true;
            });
            v.erase(r, v.end());

            if (!pub) {
                pub = factory();
                v.push_back({params,pub});
            }
            if (pub) {
                return pub->create_subscriber(pub);
            }

            return {};            
        }


    protected:
        mutable std::mutex _mx;
        MapType _map;


    };


}
