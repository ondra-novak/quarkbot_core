#include "basic_config.h"

namespace quarkbot {

BasicConfig::BasicConfig(std::shared_ptr<StructuredIni> whole_config)
:BasicConfig(std::move(whole_config), whole_config->root()) {}

BasicConfig::BasicConfig(std::shared_ptr<StructuredIni> whole_config,
        StructuredIni::Section cur_section)
:_whole_config(std::move(whole_config))
,_cur_section(std::move(cur_section))
{
}

bool BasicConfig::is_defined(std::string_view name) const {
    if (_cur_section.is_key_defined(name)) return true;
    return _cur_section[name].defined();
}

std::optional<std::filesystem::path> BasicConfig::get_path(std::string_view name) const {
    auto r = _cur_section.get_optional(name);
    if (!r) return {};
    return static_cast<std::filesystem::path>(*r);
}

std::optional<std::string_view> BasicConfig::get_value(std::string_view name) const {
    auto r = _cur_section.get_optional(name);
    if (!r) return {};
    return static_cast<std::string_view>(*r);
}

std::optional<bool> BasicConfig::get_value_bool(std::string_view name) const {
    auto r = _cur_section.get_optional(name);
    if (!r) return {};
    return static_cast<bool>(*r);

}

std::string_view BasicConfig::get_section_path() const {
    return _cur_section.get_section_path();
}

std::shared_ptr<const IConfig> BasicConfig::open_section(std::string_view name) const {
    return std::make_shared<BasicConfig>(_whole_config, _cur_section[name]);
}

std::vector<std::string_view> BasicConfig::list_sections() const {
    std::vector<std::string_view> out;
    out.reserve(_cur_section.sections().size());
    for (const auto &x: _cur_section.sections()) {
        out.push_back(x.first);
    }
    return out;
}

std::vector<std::string_view> BasicConfig::list_keys() const {
    std::vector<std::string_view> out;
    for (const auto &x: _cur_section) {
        out.push_back(x.first);
    }
    return out;
}

}
