#pragma once

#include "structured_ini.h"

#include "../quarkbot/config.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace quarkbot {

class BasicConfig: public IConfig {
public:

    BasicConfig(std::shared_ptr<StructuredIni> whole_config);
    BasicConfig(std::shared_ptr<StructuredIni> whole_config, StructuredIni::Section cur_section);

    virtual bool is_defined(std::string_view name) const override;
    virtual std::optional<std::filesystem::path> get_path(std::string_view name) const override;
    virtual std::optional<std::string_view> get_value(std::string_view name) const override;
    virtual std::optional<bool> get_value_bool(std::string_view name) const override;
    virtual std::string_view get_section_path() const override;
    virtual std::shared_ptr<const IConfig> open_section( std::string_view name) const override;
    virtual std::vector<std::string_view> list_sections() const override;
    virtual std::vector<std::string_view> list_keys() const override;

protected:

    std::shared_ptr<StructuredIni> _whole_config;
    StructuredIni::Section _cur_section;



};


}
