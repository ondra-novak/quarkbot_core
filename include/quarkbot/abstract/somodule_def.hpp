#pragma once 

#include "quarkbot/context.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include "quarkbot/version_def.hpp"
#include <cstddef>
#include <string_view>
namespace quarkbot {

    ///Interface for shared object module plugin
    class ISoModulePlugin {
    public: 
        virtual ~ISoModulePlugin()  = default;
        ///Retrieve the version of the plugin
        /**
         * @param ver Pointer to the InterfaceVersion object to be filled
         * This can cause that plugin will not be loaded if the version is incompatible with the current quarkbot version
         */
        virtual void retrieve_version(InterfaceVersion *ver) const = 0;
        ///Retrieve the number of strategies provided by the plugin
        virtual std::size_t get_strategy_count() const = 0;
        ///Retrieve the name of the strategy at the given index
        /**
        @param index The index of the strategy to retrieve the name for
        @return A string view containing the name of the strategy. This string can be used to determine path to the configuration file for the strategy. 
                The string view is valid until the plugin is unloaded.
         */
        virtual std::string_view get_strategy_name(std::size_t index) const = 0;

        ///Retrieve the class hash of the context type used by the strategy at the given index
        /**
        @param index The index of the strategy to retrieve the context type for
        @return The class hash of the context type used by the strategy. The strategy will be rejected if the context type does not match the expected type.
         */
        virtual std::size_t get_strategy_context_type(std::size_t index) const = 0;

        ///Start the strategy at the given index with the provided context
        /**
        @param index The index of the strategy to start
        @param ctx The context to be used for the strategy. The context is moved into the strategy and should not be used after this call.
        @return A StrategyFragment representing the started strategy
         */
        virtual StrategyFragment start_strategy(std::size_t index, StrategyContext &&ctx) const = 0;
    };

    using EntryPoint = const ISoModulePlugin* (*)();

}

