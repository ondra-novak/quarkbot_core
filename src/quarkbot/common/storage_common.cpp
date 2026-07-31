#include "storage_common.hpp"
#include "quarkbot/utils/bigendian.hpp"

namespace quarkbot {

std::string wholeKey(std::string_view variable_name, const std::string_view &key) {
    std::string whole_key;
    whole_key.resize(variable_name.size()+key.size()+1);
    auto iter = std::copy(variable_name.begin(), variable_name.end(), whole_key.begin());
    *iter++ = '\0';
    std::copy(key.begin(), key.end(), iter);
    return whole_key;
}

std::string wholeKey(std::string_view variable_name, const RecordKey &key) {
    return wholeKey(variable_name, record_key_to_string(key));

}


std::string record_key_to_string(const RecordKey &key) {
    std::string out;
    auto iter = std::back_inserter(out);
    big_endian_binarize(key.ordered, iter);
    big_endian_binarize(key.random, iter);
    return out;
}

RecordKey string_to_record_key(std::string_view str) {
    RecordKey key;
    auto iter = str.begin();
    iter = big_endian_unbinarize(key.ordered, iter);
    big_endian_unbinarize(key.random, iter);
    return key;
}

}