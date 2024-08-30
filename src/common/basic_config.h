#pragma once

#include "../trading_ifc/config.h"

#include "structured_ini.h"
namespace trading_api {

class BasicConfig: public IConfig {
public:

    BasicConfig(std::shared_ptr<StructuredIni> whole_config);
    BasicConfig(std::shared_ptr<StructuredIni> whole_config, StructuredIni::Section cur_section);

    virtual bool is_defined(std::string_view name) const override;
    virtual std::optional<std::filesystem::path> get_path(std::string_view name) const override;
    virtual std::optional<std::string_view> get_value(std::string_view name) const override;
    virtual std::optional<bool> get_value_bool(std::string_view name) const override;
    virtual std::string_view get_section_path() const override;
    virtual std::shared_ptr<const trading_api::IConfig> open_section( std::string_view name) const override;

protected:

    std::shared_ptr<StructuredIni> _whole_config;
    StructuredIni::Section _cur_section;



};


}
