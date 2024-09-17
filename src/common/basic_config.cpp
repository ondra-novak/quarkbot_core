#include "basic_config.h"

namespace quarkbot {

BasicConfig::BasicConfig(std::shared_ptr<StructuredIni> whole_config)
:BasicConfig(whole_config, whole_config->root()) {}

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
    if (_cur_section.is_key_defined(name)) return std::make_shared<BasicConfigArray>(_whole_config, _cur_section.get(name), _cur_section.get_section_path(), name);
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

bool BasicConfig::is_defined(unsigned int index) const {
    return index < std::distance(_cur_section.begin(), _cur_section.end());

}

std::optional<bool> BasicConfig::get_value_bool(unsigned int index) const {
    for (const auto &kv: _cur_section) {
        if (index == 0) return static_cast<bool>(StructuredIni::Value(kv.second));
        --index;
    }
    return {};
}

std::optional<std::filesystem::path> BasicConfig::get_path(unsigned int index) const {
    for (const auto &kv: _cur_section) {
        if (index == 0) return static_cast<std::filesystem::path>(StructuredIni::Value(kv.second));
        --index;
    }
    return {};
}

std::optional<std::string_view> BasicConfig::get_value(unsigned int index) const {
    for (const auto &kv: _cur_section) {
        if (index == 0) return kv.second.content;
        --index;
    }
    return {};

}

BasicConfigArray::BasicConfigArray(std::shared_ptr<StructuredIni> whole_config, StructuredIni::Value value,
        std::string_view path,
        std::string_view name)
        :_whole_config(whole_config)
        ,_val(value)
        {
            _path.reserve(name.size()+path.size()+2);
            _path.append(path);
            _path.append("::");
            _path.append(name);
        }


std::vector<std::string_view> BasicConfigArray::list_keys() const {
    return {};
}

std::vector<std::string_view> BasicConfigArray::list_sections() const {
    return {};
}

bool BasicConfigArray::is_defined(unsigned int index) const {
    return index < _val.count();
}

bool BasicConfigArray::is_defined(std::string_view ) const {
    return false;
}

std::optional<std::filesystem::path> BasicConfigArray::get_path(std::string_view ) const {
    return {};
}

std::optional<bool> BasicConfigArray::get_value_bool(unsigned int index) const {
    if (index >= _val.count()) return {};
    return static_cast<bool>(_val[index]);
}

std::optional<std::string_view> BasicConfigArray::get_value(std::string_view ) const {
    return {};
}

std::optional<bool> BasicConfigArray::get_value_bool(std::string_view ) const {
    return {};
}

std::optional<std::filesystem::path> BasicConfigArray::get_path(unsigned int ) const {
    return {};
}

std::string_view BasicConfigArray::get_section_path() const {
    return _path;
}

std::shared_ptr<const IConfig> BasicConfigArray::open_section(std::string_view) const {
    return {};
}

std::optional<std::string_view> BasicConfigArray::get_value(unsigned int index) const {
    if (index >= _val.count()) return {};
    return static_cast<std::string_view>(_val[index]);
}

}
