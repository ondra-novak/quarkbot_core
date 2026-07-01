#pragma once
#include "quarkbot/backtest_data_source.hpp"
#include <filesystem>
#include <string>

struct gzFile_s;

namespace quarkbot {

class ConfiguredDataSource : public IBacktestDataSource {
public:
    explicit ConfiguredDataSource(std::filesystem::path ini_path);
    ~ConfiguredDataSource() override;
    std::vector<InstrumentSpec> get_instrument_infos() override;
    std::optional<Event> next_event() override = 0;
protected:
    bool read_line(std::string &out);
    const InstrumentSpec &instrument_spec() const { return _spec; }
private:
    InstrumentSpec _spec;
    gzFile_s *_gz = nullptr;
    void parse_ini(const std::filesystem::path &ini_path);
};

} // namespace quarkbot
