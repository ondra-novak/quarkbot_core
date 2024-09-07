#pragma once

#include "report.h"

namespace quarkbot {


class ReportCSV: public IReport {
public:

    ReportCSV(std::string f);

    virtual void order_place(Timestamp tp, const SimOrder &order) override;
    virtual void order_status(quarkbot::Timestamp tp, const SimOrder &order, const Order::Report &rpt)override;
};


}
