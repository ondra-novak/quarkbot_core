#ifdef _WIN32

#else

#include "somodule.hpp"
#include "quarkbot/version.hpp"
#include <dlfcn.h>

namespace quarkbot {

    namespace {
        ///dlerror() can return nullptr (e.g. when no error is recorded), which operator+ cannot handle
        const char *safe_dlerror() {
            const char *msg = dlerror();
            return msg ? msg : "unknown error";
        }
    }

   SoModuleStrategyList SoModuleStrategyList::load_module(const std::filesystem::path path) {
        void *handle = dlopen(path.c_str(), RTLD_NOW);
        if (!handle) {
            throw std::runtime_error(std::string("Failed to load shared object module: ") + safe_dlerror());
        }
        auto entry_point = reinterpret_cast<EntryPoint>(dlsym(handle, "quarkbot_so_module_entry_point"));
        if (!entry_point) {
            dlclose(handle);
            throw std::runtime_error(std::string("Failed to find entry point in shared object module: ") + safe_dlerror());
        }
        auto plugin = entry_point();
        return SoModuleStrategyList(plugin);
    }

    SoModuleStrategyList::SoModuleStrategyList(const ISoModulePlugin *plugin) : _plugin(plugin) {
        InterfaceVersion ver;
        _plugin->retrieve_version(&ver);
        _version_compatible = ver.is_compatible_with_abi(api_version);;
        if (_version_compatible) {
            for (std::size_t i = 0, cnt = _plugin->get_strategy_count(); i < cnt; ++i) {
                _strategies.push_back({_plugin->get_strategy_name(i), _plugin->get_strategy_context_type(i)});
            }
        }
   }

    StrategyFragment SoModuleStrategyList::start_strategy_with_hash(std::size_t idx, StrategyContext &&context, std::size_t context_type) const {
        if (idx >= _strategies.size()) {
            throw std::out_of_range("Strategy index out of range");
        }
        if (_strategies[idx].context_type != context_type) {
            throw std::runtime_error("Strategy context type mismatch");
        }
        return _plugin->start_strategy(idx, static_cast<StrategyContext &&>(context));

    }




}
#endif