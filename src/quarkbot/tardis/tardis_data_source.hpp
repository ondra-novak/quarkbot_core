#pragma once
#include "quarkbot/abstract/backtest_data_source.hpp"
#include "quarkbot/decimal.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct gzFile_s;

namespace quarkbot {

///Reads a gzip-compressed CSV file exported by Tardis.dev, line by line
class TardisCsvDataSource {
public:
    explicit TardisCsvDataSource(std::filesystem::path csv_gz_path);
    ~TardisCsvDataSource();
    TardisCsvDataSource(const TardisCsvDataSource &) = delete;
    TardisCsvDataSource &operator=(const TardisCsvDataSource &) = delete;
    TardisCsvDataSource(TardisCsvDataSource &&other) noexcept;

protected:
    bool read_line(std::string &out);
    ///index of the named header column, -1 when the file has no such column
    int optional_column(std::string_view name) const;
    ///like optional_column, but records a missing name for check_columns()
    int require_column(std::string_view name);
    ///throw naming every column require_column() did not find
    void check_columns() const;
    const std::filesystem::path &path() const {return _path;}
    ///symbol of the current row as exchange:symbol, cached from the first row
    const std::string &row_symbol(std::string_view exchange, std::string_view symbol);
    ///throw a runtime_error naming the file and the row last read
    [[noreturn]] void row_error(std::string_view message) const;
    ///parse a decimal column, turning a parse failure into a row_error
    Decimal parse_decimal(std::string_view value, std::string_view column) const;
    ///parse a microsecond unix timestamp column, rejecting anything but digits
    ///that fit system_clock::duration
    std::chrono::system_clock::time_point parse_us_timestamp(
            std::string_view value, std::string_view column) const;

private:
    bool read_line_raw(std::string &out);

    std::string _symbol;
    std::filesystem::path _path;
    gzFile_s *_gz = nullptr;
    std::vector<std::string> _header;
    std::string _missing;
    ///number of the row last read; the header is row 1
    std::uint64_t _line = 0;
};

///Backtest data source that generates Trade events from a Tardis trades CSV export
class TardisTradesDataSource : public TardisCsvDataSource {
public:
    explicit TardisTradesDataSource(std::filesystem::path csv_gz_path);
    bool operator()(BacktestEvent &ev);
private:
    int _col_exchange, _col_symbol, _col_local_timestamp, _col_price, _col_amount, _col_side;
    std::size_t _min_cols;
};

///Backtest data source that generates Quote events from a Tardis quotes CSV export
class TardisQuotesDataSource : public TardisCsvDataSource {
public:
    explicit TardisQuotesDataSource(std::filesystem::path csv_gz_path);
    bool operator()(BacktestEvent &ev);
private:
    int _col_exchange, _col_symbol, _col_local_timestamp, _col_bid_price, _col_bid_amount,
        _col_ask_price, _col_ask_amount;
    std::size_t _min_cols;
};

} // namespace quarkbot
