#include "quarkbot/backtest/var_inspector.hpp"
#include "quarkbot/common/mem_storage.hpp"
#include "quarkbot/storage.hpp"
#include "tests/check.h"

using namespace quarkbot;

///a put made with UpdateLastRevision::disable emits no last-revision pointer event; the
///inspector must still learn about the variable from the data event itself (put_key_value),
///not only from the pointer update (it used to key off the raw physical key on the wrong
///event, which both fed it junk and, for disable-mode writes, never fed it anything at all)
///
///the first, ordinary put seeds a last-revision pointer so the variable is resolvable via
///get() regardless of what the disable-mode put does - isolating the check to exactly the
///question this bug is about: does a disable-mode write add its variable to the updated set?
void test_disable_mode_put_is_reported() {
    Storage storage(MemStorage::create());

    VariableInspector inspector;
    inspector.attach_storage(storage);

    auto trn = storage.write();
    trn.put("v", RecordKey{1,0}, "first", UpdateLastRevision::enable);
    trn.commit();
    inspector.clear_updated();

    auto trn2 = storage.write();
    trn2.put("v", RecordKey{2,0}, "second", UpdateLastRevision::disable);
    trn2.commit();

    auto updated = inspector.inspect_all_updated();
    CHECK(updated.find("v") != updated.end());
}

int main() {
    test_disable_mode_put_is_reported();
    return 0;
}
