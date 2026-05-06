#pragma once

#include "defs.hpp"
#include "order_defs.hpp"
#include "storage.hpp"
namespace quarkbot {

    ///helper object which is used by ITradableInstrument implementation to manage storing and restoring orders from / to database.     
    class OrderStorage {
    public:
        OrderStorage(PStorage storage, std::string fill_var, std::string order_var)
            : _storage(std::move(storage)) 
            , _fill_var(std::move(fill_var))
            ,_order_var(std::move(order_var))
        {
        }

        ///Create transaction, all writes must be inside transaction
        PStorageTransaction write() {
            return _storage->write();
        }

        
        ///store a fill
        void store_fill(const PStorageTransaction &tx, const Fill &fill) {
            tx->store(_fill_var, fill.key, fill, UpdateLastRevision::disable);
        }
        ///close order - called when order is done
        void close_order(const PStorageTransaction &tx, const RecordKey &key) {
            tx->erase(_order_var, key);
        }
        ///open order - called when id is asssigned
        void open_order(const PStorageTransaction &tx, const RecordKey &key, const std::string &id, const OrderParameters &params) {
            tx->store(_order_var, key, std::pair(id, params));
        }
        ///load saved active orders
        /**
        Returns necessery informations about orders which can be understand by adapter
         */
        std::vector<std::pair<std::string, OrderParameters> > load_opened_orders() const {
            std::vector<std::pair<std::string, OrderParameters> > out;
            for (auto val: _storage->select_range(_order_var, RecordKey::min(), RecordKey::max())) {
                out.push_back({});
                if (!val.extract(out.back()))  out.pop_back();
            }
            return out;
        }



    protected:
        PStorage _storage;
        PStorageTransaction _transaction;
        std::string _fill_var;
        std::string _order_var;
        
    };


}