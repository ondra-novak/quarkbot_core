#pragma once

#include "ifc/defs.hpp"
#include "ifc/stream.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
namespace quarkbot {

class DataSourceBase: public IDataSource {
public:

    struct Key {
        std::string topic;
        MarketStreamTypeItem::Type type;        
        constexpr bool operator==(const Key &other) const = default;
    };


    ///post
    /**
    post update
    @param topic topic
    @param data data
    @note not MT safe
    @note disable_stream can be called under lock
    
    */
    void post(const Key &topic, const MarketStreamTypeItem &data);

    ///Subscribe receiver
    /**
    @param topic
    @param receiver

    @note You need to keep reference to subscriber. Once reference is dropped, unsubscribe operation is performed. 
    */
    virtual bool subscribe(std::string_view topic, std::shared_ptr<IDataReceiver> receiver);
    

protected:

    ///implementation enables stream from exchange
    /**
    @param topic topic / instrument 
    @param type data type
    @retval true supported topic
    @retval flase unsupported topic
    @note called under lock, when subscribe is called by the strategy
     */
    virtual bool enable_stream(std::string_view topic, MarketStreamTypeItem::Type type) = 0;

    ///implementation disables stream 
    /**
     * @param topic topic/Instrument
     * @param type data type
     * @note it is called under a lock, and probably in context of post() function
     */
    virtual bool disable_stream(std::string_view topic, MarketStreamTypeItem::Type type) = 0;


protected:  

    struct HashKey {
        std::size_t operator()(const Key &key) const {
            auto hasher = std::hash<std::string_view>();
            return hasher(key.topic) + hasher(key.type);
        }
    };
    

    using Targets = std::vector<std::weak_ptr<IDataReceiver> >;
    using Map = std::unordered_map<Key, Targets, HashKey>;

    std::mutex _mx;
    Map _map;

    bool broadcast(std::unique_lock<std::mutex> &lk, Targets &tgs,  std::size_t rd_pos, std::size_t wr_pos, 
         const MarketStreamTypeItem &data);
};

} 