#include "../quarkbot/backtest/storage_dump.hpp"
#include "../quarkbot/common/mem_storage.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/storage_srl.hpp"
#include "tests/check.h"

#include <sstream>
#include <string>

using namespace quarkbot;

///every record of every variable becomes one row, variables sorted by name, records by key;
///values with a stored schema are rendered as JSON, values without one fall back to binary_content;
///fields containing a comma or quote are CSV-quoted
void test_dump_csv() {
    Storage storage(MemStorage::create(MemStorage::keep_history));
    auto wr = storage.write();
    wr.store("num", RecordKey{2,1}, 42);
    wr.store("num", RecordKey{1,7}, 7);
    wr.store("a,b", RecordKey{5,0}, std::string("x\"y"));
    wr.put("raw", RecordKey{3,0}, "AB", UpdateLastRevision::enable);
    wr.commit();

    std::ostringstream out;
    dump_storage_csv(storage, out);

    std::string expected =
        "key,ordered,random,value\n"
        "\"a,b\",5,0,\"\"\"x\\\"\"y\"\"\"\n"
        "num,1,7,7\n"
        "num,2,1,42\n"
        "raw,3,0,\"[\"\"4142\"\",\"\"AB\"\"]\"\n";
    CHECK_EQUAL(out.str(), expected);
}

void test_dump_empty_storage() {
    Storage storage(MemStorage::create(MemStorage::keep_history));
    std::ostringstream out;
    dump_storage_csv(storage, out);
    CHECK_EQUAL(out.str(), std::string("key,ordered,random,value\n"));
}

int main() {
    test_dump_csv();
    test_dump_empty_storage();
    return 0;
}
