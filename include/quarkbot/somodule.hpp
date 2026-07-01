#pragma once

#include "abstract/somodule_def.hpp"
#include "quarkbot/context.hpp"
#include "quarkbot/hash/class_hash.hpp"
#include "version.hpp"
namespace quarkbot {

    class SoModulePluginRegistry : public ISoModulePlugin {
    public:
        virtual void retrieve_version(InterfaceVersion *ver) const override {
            *ver = api_version;
        }
        virtual std::size_t get_strategy_count() const override {
            return _strategies.size();
        }
        virtual std::string_view get_strategy_name(std::size_t index) const override {
            if (index < _strategies.size()) {
                return _strategies[index].name;
            }
            return {};
        }
        virtual StrategyFragment start_strategy(std::size_t index, StrategyContext &&ctx) const override {
            return _strategies[index].start_func(std::move(ctx));
        }

        template<typename Strategy, std::derived_from<StrategyContext> _Context = StrategyContext>
        void register_strategy(std::string_view name) {
            _strategies.push_back({std::string(name), class_hash<_Context>, [name](StrategyContext &&ctx) {
                return StrategyContext::create_and_start_strategy<Strategy, _Context>(static_cast<_Context &&>(ctx));
            }});
        }

        virtual std::size_t get_strategy_context_type(std::size_t index) const override {
            if (index < _strategies.size()) {
                return _strategies[index].context_type;
            }
            return 0;
        }

        static SoModulePluginRegistry& instance() {
            static SoModulePluginRegistry _instance;
            return _instance;
        }

    protected:
        struct StrategyInfo {
            std::string name;
            std::size_t context_type; //class_hash of used context type - strategy is rejected if hash mismatches
            std::function<StrategyFragment(StrategyContext &&)> start_func;
        };
        std::vector<StrategyInfo> _strategies;  
    };


///Define a strategy to be exported from the shared object module plugin
/**
This class is used to register a strategy with the shared object module plugin registry. It takes a
    strategy class and a context type as template parameters. The strategy class must have a constructor that 
    takes a context object and a main() method that returns a StrategyFragment. 
    The context type must be derived from StrategyContext.

*/
template<typename Strategy, std::derived_from<StrategyContext> _Context = StrategyContext>
class ExportedStrategy {
public:
    ExportedStrategy(std::string_view name) {
        SoModulePluginRegistry::instance().register_strategy<Strategy, _Context>(name);
    }
};

}

extern "C" {
    ///Entry point for shared object module plugin
    /**
     * @return Pointer to the ISoModulePlugin interface implemented by the plugin
     */
    inline const quarkbot::ISoModulePlugin* quarkbot_so_module_entry_point(){
        return &quarkbot::SoModulePluginRegistry::instance();
    }
}