#pragma once
#include "quarkbot/abstract/backtest_data_source.hpp"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct gzFile_s;

namespace quarkbot {

///Reads a gzip-compressed CSV file exported by Tardis.dev, line by line
class TardisCsvDataSource {
public:
    TardisCsvDataSource(std::string instrument, std::filesystem::path csv_gz_path);
    ~TardisCsvDataSource();
    TardisCsvDataSource(const TardisCsvDataSource &) = delete;
    TardisCsvDataSource &operator=(const TardisCsvDataSource &) = delete;
    TardisCsvDataSource(TardisCsvDataSource &&other) noexcept;

protected:
    const std::string &instrument() const { return _instrument; }
    bool read_line(std::string &out);
    ///index of the named header column, -1 when the file has no such column
    int optional_column(std::string_view name) const;
    ///like optional_column, but records a missing name for check_columns()
    int require_column(std::string_view name);
    ///throw naming every column require_column() did not find
    void check_columns() const;
    const std::filesystem::path &path() const {return _path;}

private:
    std::string _instrument;
    std::filesystem::path _path;
    gzFile_s *_gz = nullptr;
    std::vector<std::string> _header;
    std::string _missing;
};

///Backtest data source that generates Trade events from a Tardis trades CSV export
class TardisTradesDataSource : public TardisCsvDataSource {
public:
    TardisTradesDataSource(std::string instrument, std::filesystem::path csv_gz_path);
    bool operator()(BacktestEvent &ev);
private:
    int _col_local_timestamp, _col_price, _col_amount;
    std::size_t _min_cols;
};

///Backtest data source that generates Quote events from a Tardis quotes CSV export
class TardisQuotesDataSource : public TardisCsvDataSource {
public:
    TardisQuotesDataSource(std::string instrument, std::filesystem::path csv_gz_path);
    bool operator()(BacktestEvent &ev);
private:
    int _col_local_timestamp, _col_bid_price, _col_bid_amount, _col_ask_price, _col_ask_amount;
    std::size_t _min_cols;
};

} // namespace quarkbot
