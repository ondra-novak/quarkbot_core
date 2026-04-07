#pragma once

#include "ifc/defs.hpp"
#include "ifc/reporter.hpp"
#include <filesystem>
namespace quarkbot {

class FillsToCsvReporter final: public IReporter {
public:

    static PReporter create(std::filesystem::path target_file);
    FillsToCsvReporter(const std::filesystem::path &target_file);
    virtual ~FillsToCsvReporter();


    void on_fill(const Fill &fill, const Order &order) override;
    void on_order_placed(const Order &order) override;
    void on_order_status(const Order &order, const OrderStatusUpdate &status) override;


protected:
    FILE *_outf;

};

}