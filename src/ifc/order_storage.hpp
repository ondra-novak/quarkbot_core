#pragma once

#include "ifc/defs.hpp"
#include "ifc/order.hpp"
#include "ifc/storage.hpp"
#include "ifc/types.hpp"
namespace quarkbot {


    




    class OrderStorage {
    public:
        OrderStorage(PStorage storage, std::string fill_var, std::string order_var)
            : _storage(std::move(storage)) 
            , _fill_var(std::move(fill_var))
            ,_order_var(std::move(order_var))
        {
        }

        PStorageTransaction write() {
            return _storage->write();
        }

        void store_fill(const PStorageTransaction &tx, const Fill &fill) {
            
        }



    protected:
        PStorage _storage;
        PStorageTransaction _transaction;
        std::string _fill_var;
        std::string _order_var;
        
    }


}