#pragma once

#include "quarkbot/abstract/somodule_def.hpp"
#include <filesystem>
#include <string_view>
namespace quarkbot {

class SoModuleStrategyList {
public:

    struct StrategyInfo {
        std::string_view name;
        std::size_t context_type;       

        template<typename _Context>
        bool context_type_matches() const {
            return context_type == class_hash<_Context>;
        }
    };

    static SoModuleStrategyList load_module(const std::filesystem::path path);


    bool is_version_compatible() const {
        return _version_compatible;
    }

    std::span<const StrategyInfo> get_strategies() const {
        return _strategies;
    }

    template<std::derived_from<StrategyContext> _Context>
    StrategyFragment start_strategy(std::size_t idx, _Context &&context) const {
        std::size_t context_type = class_hash<_Context>;
        return start_strategy_with_hash(idx, static_cast<StrategyContext &&>(context), context_type);
    }



protected:
    const ISoModulePlugin *_plugin = nullptr;
    std::vector<StrategyInfo> _strategies;
    bool _version_compatible = false;

    SoModuleStrategyList(const ISoModulePlugin *plugin);

    StrategyFragment start_strategy_with_hash(std::size_t idx, StrategyContext &&context, std::size_t context_type) const;
};

}