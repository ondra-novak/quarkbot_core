#pragma once

#include "defs.hpp"
#include "types.hpp"
#include "order_defs.hpp"
#include "storage.hpp"
namespace quarkbot {

    ///helper object which is used by ITradableInstrument implementation to manage storing and restoring orders from / to database.     


    class OrderStorage {
    public:

        struct OrderStoredState {
            std::string id;
            OrderParameters parameters;
            
            auto fields(this auto &self) {
                return std::tie(self.id, self.parameters);
            } 
        };

        struct FilledState {
            Decimal filled = {};
            Decimal turnover = {};
            auto field(this auto &self) {
                return std::tie(self.filled, self.turnover);
            }
        };

        struct OrderRestoredState {
            OrderStoredState st;
            FilledState fill_st;
        };

        OrderStorage(Storage storage, std::string fill_var)
            : _storage(std::move(storage)) 
            , _fill_var(fill_var)
            ,_order_var(fill_var+":o")
            ,_order_fill_var(fill_var+":f")
        {
        }

        ///Create transaction, all writes must be inside transaction
        StorageTransaction write() {
            return _storage.write();
        }

        
        ///helps with fill deduplication
        bool check_fill_exists(const Fill &f) const {
            return _storage.get(_fill_var, f.key).exists;
        }

        ///store a fill
        void store_fill(StorageTransaction &tx, const Fill &fill) {
            tx.store(_fill_var, fill.key, fill, UpdateLastRevision::disable);
        }
        void store_filled(StorageTransaction &tx, const RecordKey &key,FilledState fst) {
            tx.store(_order_fill_var, key, fst);
        }

        ///close order - called when order is done
        void close_order(StorageTransaction &tx, const RecordKey &key) {
            tx.erase(_order_var, key);
            tx.erase(_order_fill_var, key);
        }
        ///open order - called when id is asssigned
        void open_order(StorageTransaction &tx, const RecordKey &key, OrderStoredState st) {
            tx.store(_order_var, key, st);
        }
        ///load saved active orders
        /**
        Returns necessery informations about orders which can be understand by adapter
         */
        std::vector<OrderRestoredState> load_opened_orders() const {
            std::vector<OrderRestoredState> out;
            for (auto val: _storage.select_range(_order_var, RecordKey::min(), RecordKey::max())) {
                OrderStoredState st;
                val.extract(st);
                FilledState d = {};
                auto val2 = _storage.get(_order_fill_var, val.key);
                val2.extract(d);
                out.push_back({st, d});
            }
            return out;
        }



    protected:
        Storage _storage;
        PStorageTransaction _transaction;
        std::string _fill_var;
        std::string _order_var;
        std::string _order_fill_var;
        
    };


}