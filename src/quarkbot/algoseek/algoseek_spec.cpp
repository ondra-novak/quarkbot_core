#include "algoseek_spec.hpp"

#include "quarkbot/utils/string_utils.hpp"
#include <format>
#include <stdexcept>

namespace quarkbot {

AlgoseekSpec parse_algoseek_spec(std::string_view spec) {
    AlgoseekSpec res;
    auto qpos = spec.find('?');
    auto path = trim(spec.substr(0, qpos));
    if (path.empty()) {
        throw std::runtime_error(std::format("Algoseek source: empty file path in '{}'", spec));
    }
    res.file = std::filesystem::path(path);

    std::string_view tzone;
    if (qpos != spec.npos) {
        std::string_view query = spec.substr(qpos + 1);
        while (!query.empty()) {
            std::string_view param = split(query, "&");
            if (param.empty()) continue;
            auto eq = param.find('=');
            if (eq == param.npos) {
                throw std::runtime_error(std::format(
                    "Algoseek source: parameter '{}' has no value in '{}'", param, spec));
            }
            auto key = trim(param.substr(0, eq));
            auto value = trim(param.substr(eq + 1));
            if (key == "exchange") res.exchange = value;
            else if (key == "tzone") tzone = value;
            else if (key == "symbol") res.symbol = value;
            else throw std::runtime_error(std::format(
                "Algoseek source: unknown parameter '{}' in '{}'", key, spec));
        }
    }

    try {
        res.tz = std::chrono::locate_zone(tzone.empty() ? std::string_view("UTC") : tzone);
    } catch (const std::exception &e) {
        throw std::runtime_error(std::format(
            "Algoseek source: unknown time zone '{}' in '{}': {}", tzone, spec, e.what()));
    }
    return res;
}

}
