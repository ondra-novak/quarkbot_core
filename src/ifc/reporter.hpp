#pragma once

#include "ifc/defs.hpp"
#include "ifc/order.hpp"
#include "ifc/types.hpp"
namespace quarkbot {

class IReporter {
public:
    virtual void on_fill(const Fill &fill, const Order &order) = 0;
    virtual void on_order_placed(const Order &order) = 0;
    virtual void on_order_status(const Order &order, const OrderStatusUpdate &status) = 0;
    virtual ~IReporter() = default;    

};

}