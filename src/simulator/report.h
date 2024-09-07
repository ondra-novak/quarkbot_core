#pragma once


#include "sim_order.h"
namespace quarkbot {

class IReport {
public:


    virtual ~IReport() = default;


    virtual void order_place(Timestamp tp, const SimOrder &order) = 0;
    virtual void order_status(Timestamp tp, const SimOrder &order, const Order::Report &rpt) = 0;
};


}
