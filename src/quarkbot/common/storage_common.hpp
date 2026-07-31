#pragma once

#include "quarkbot/types.hpp"
#include <string>
namespace quarkbot {

    std::string wholeKey(std::string_view variable_name, const RecordKey &key);
    std::string wholeKey(std::string_view variable_name, const std::string_view &key);
    std::string record_key_to_string(const RecordKey &key);
    RecordKey string_to_record_key(std::string_view str);
    constexpr auto recordkey_string_size = sizeof(RecordKey);

}