#pragma once

#include "defs.hpp"
#include "order.hpp"
#include "storage.hpp"
#include "serializer.hpp"
#include "tradable_instrument.hpp"
namespace quarkbot {    

    ///Class responsible for storing and restoring orders in storage and also store their fills
    /**
        OrderStorage is responsible for storing orders in storage and restoring them on strategy startup. 
        It also stores fills for orders, so when strategy is restarted, it can pull fills that happened while strategy was down.
        The stored fills can be later used to analyze trading history, calculate statistics, or for any other purpose.
     */    
    class OrderStorage {
    public:

        ///constructor
        /**
         * @brief Constructs an OrderStorage instance.
         * @param storage The storage instance to use.
         * @param order_prefix The prefix for order keys. For multiexchange strategies, it is recommended to use different prefixes for different exchanges to avoid key collisions.
         * @param fill_key The key for fill storage, this is sequence key, so multiple fills are stored with increasing revision number.
         */
        OrderStorage(PStorage storage, std::string order_prefix,std::string fill_key)
            :_storage(std::move(storage))
            ,_order_prefix(order_prefix)
            ,_fill_key(fill_key) {}


    ///store order in storage, return fill if there is new fill since last store
    /**
        Use this function in coroutine which is reading order states and fills. This function extract one fill which
        was not seen before, and returns it. It also stores order state to be able to restore it later in case the
        strategy is restarted. If there is no new fill, it returns nullopt. If the order is done, it is removed from storage.
        @param order order to store
        @return optional containing new fill, or nullopt if there is no new fill
     */
    inline std::optional<Fill> order_store(Order &order) {
        //do not store orders without id
        if (order.get_id().empty()) return {};
        std::string order_key_str = _order_prefix + order.get_id();        

        auto wr = _storage->write();
        IStorage::Key key(order_key_str, false);

        auto fillvalue = _storage->get({_fill_key, true});
        std::optional<Fill> out;
        if (fillvalue.exists) {
            Fill last_fill = from_binary_blob<Fill>(fillvalue.data);
            out = order.read_fill();
            while (out && out->time <= last_fill.time) { //todo - deduplication
                out = order.read_fill();
            }
        } else {
            out = order.read_fill();
        }
        
        if (out) {
            wr->put({_fill_key, true}, to_binary_blob(*out));
        }

        if (order.done()) {
            wr->erase(key);
        } else {
            wr->put(key, order.get_instrument()->serialize_order(order));
        }
        wr->commit();        
        return out;
    }


    ///restore orders from storage and call callback for each restored order
    /**
    Use this function on strategy startup to restore orders from previous runs. 
    It retrieves all orders from storage, restores them using instrument's restore_order function, 
    and calls callback for each restored order. These restored orders have their status set to "restored" until
    the connector asynchronously synchronizes their state with exchange. Once the state is synchronized, 
    if order is still found on exchange, its status is updated to their current status. This includes also already filled orders,
    so strategy can get fills that happened while it was down. 
    If order is not found on exchange, it is marked as lost, and its fills are not emitted 
          (this shouldn't happen often and points to some issues with connector synchronization).    
    Restored orders also emits their fills.

    @param instrument instrument to use for restoring orders
    @param cb callback to call for each restored order
     */

    
    template<std::invocable<Order> Callback>
    inline void order_restore(const PTradableInstrument &instrument, Callback &&cb) {        
        auto keys = _storage->list({_order_prefix, false});
        for (auto &k:keys) {
            auto val = _storage->get({k, false});
            if (val.exists) {
                Order ord = instrument->restore_order(val.data);
                cb(ord);
            }
        }
    }

    protected:
        PStorage _storage;
        std::string _order_prefix;
        std::string _fill_key;

    };



   

}