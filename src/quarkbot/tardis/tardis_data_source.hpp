#pragma once
#include "quarkbot/abstract/backtest_data_source.hpp"
#include <filesystem>
#include <string>

struct gzFile_s;

namespace quarkbot {

///Reads a gzip-compressed CSV file exported by Tardis.dev, line by line
class TardisCsvDataSource {
public:
    TardisCsvDataSource(std::string instrument, std::filesystem::path csv_gz_path);
    ~TardisCsvDataSource();
    TardisCsvDataSource(const TardisCsvDataSource &) = delete;
    TardisCsvDataSource &operator=(const TardisCsvDataSource &) = delete;

protected:
    const std::string &instrument() const { return _instrument; }
    bool read_line(std::string &out);

private:
    std::string _instrument;
    gzFile_s *_gz = nullptr;
};

///Backtest data source that generates Trade events from a Tardis trades CSV export
class TardisTradesDataSource : public TardisCsvDataSource {
public:
    using TardisCsvDataSource::TardisCsvDataSource;
    bool operator()(BacktestEvent &ev);
private:
    bool _header_parsed = false;
    int _col_timestamp = -1;
    int _col_price = -1;
    int _col_amount = -1;
};

///Backtest data source that generates Quote events from a Tardis quotes CSV export
class TardisQuotesDataSource : public TardisCsvDataSource {
public:
    using TardisCsvDataSource::TardisCsvDataSource;
    bool operator()(BacktestEvent &ev);
private:
    bool _header_parsed = false;
    int _col_timestamp = -1;
    int _col_bid_price = -1;
    int _col_bid_size = -1;
    int _col_ask_price = -1;
    int _col_ask_size = -1;
};

} // namespace quarkbot
