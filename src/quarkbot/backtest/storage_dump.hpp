#pragma once

#include "quarkbot/storage.hpp"
#include <ostream>

namespace quarkbot {

///writes all records of the storage as CSV
/**
    Format: header `key,ordered,random,value`, one row per record. Variables are sorted by name,
    records by their RecordKey. `value` is the record decoded to JSON through its stored schema
    (binary_content when the schema is not available). Fields are quoted per RFC 4180 when needed.

    Intended for comparing results of repeated backtests.
 */
void dump_storage_csv(const Storage &storage, std::ostream &out);

}
