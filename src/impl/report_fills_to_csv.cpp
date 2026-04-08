#include "report_fills_to_csv.hpp"
#include "ifc/tradable_instrument.hpp"
#include "ifc/account.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <system_error>

namespace quarkbot {

PReporter FillsToCsvReporter::create(std::filesystem::path target_file) {

}
FillsToCsvReporter::FillsToCsvReporter(const std::filesystem::path &target_file) {
    _outf = fopen(target_file.c_str(), "wt");
    if (!_outf) throw std::system_error(errno, std::system_category(), "Can't open output file: "+ target_file.string());
    fprintf(_outf,"time,instrument,side,quantity,position,balance,currency");
}
FillsToCsvReporter::~FillsToCsvReporter() {
    fclose(_outf);
}


void FillsToCsvReporter::on_fill(const Fill &fill, const Order &order) {
    auto instr = order.get_instrument();
    auto acc = instr->get_account();
    const auto &info = instr->get_info();
    const auto &curr = info.is_leveraged()?info.pnl_currency:info.quote_currency;

    auto t = std::chrono::system_clock::to_time_t(fill.time);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(fill.time.time_since_epoch()).count() % 1000;
    struct tm tinfo;
    gmtime_r(&t, &tinfo);

    fprintf(_outf, "%04d-%02d-%02d %02d:%02d:%02d.%03d,\"%s\",%s,%s,%s,%s,%s", 
        tinfo.tm_year+1900, tinfo.tm_mon+1, tinfo.tm_mday, tinfo.tm_hour, tinfo.tm_min, tinfo.tm_sec, static_cast<int>(ms),
        info.name.c_str(), fill.side == Side::buy?"BUY":"SELL",

    );

}
void FillsToCsvReporter::on_order_placed(const Order & {
    //empty
}
void FillsToCsvReporter::on_order_status(const Order &, const OrderStatusUpdate &) {
    //empty
}


}