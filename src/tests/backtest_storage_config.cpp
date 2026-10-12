#include "../quarkbot/backtest/config_backtest.hpp"
#include "../quarkbot/common/mem_storage.hpp"
#include "../quarkbot/leveldb/leveldb_storage.hpp"
#include "quarkbot/storage.hpp"
#include "tests/check.h"

#include <filesystem>
#include <fstream>
#include <leveldb/options.h>
#include <stdexcept>
#include <string>

using namespace quarkbot;

///RAII guard: fresh directory for one test case, removed on scope exit
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char *name)
        :path(std::filesystem::temp_directory_path() / (std::string("quarkbot_bt_storage_test_") + name)) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {std::filesystem::remove_all(path);}
};

static BacktestConfig write_config(const std::filesystem::path &dir, const std::string &content) {
    auto ini = dir / "backtest.ini";
    std::ofstream(ini) << content;
    return BacktestConfig::load(ini);
}

///creates a LevelDB database with one variable `v` in the storage `name`
static void make_db(const std::filesystem::path &path, std::string_view name, std::string_view value) {
    leveldb::Options opts;
    opts.create_if_missing = true;
    Storage storage(LevelDBStorageManager::open_db(path, opts).get_storage(name));
    auto wr = storage.write();
    wr.put("v", value);
    wr.commit();
}

static std::string read_v(const Storage &storage) {
    auto val = storage.get("v");
    return val.exists ? val.data : std::string("<missing>");
}

static bool is_leveldb(const Storage &storage) {
    return std::dynamic_pointer_cast<LevelDBStorage>(storage.get_handle()) != nullptr;
}

void test_no_section_uses_memory() {
    TempDir tmp("nosection");
    auto cfg = write_config(tmp.path, "[log]\nlevel=info\n");
    Storage storage = cfg.configure_storage();
    CHECK(std::dynamic_pointer_cast<MemStorage>(storage.get_handle()) != nullptr);
}

void test_preset_is_copied() {
    TempDir tmp("copied");
    make_db(tmp.path / "preset.db", "strategy", "from_preset");
    auto cfg = write_config(tmp.path, "[storage]\npreset=preset.db\ndatabase=work.db\n");
    Storage storage = cfg.configure_storage();
    CHECK(is_leveldb(storage));
    CHECK_EQUAL(read_v(storage), std::string("from_preset"));
}

void test_rerun_resets_to_preset() {
    TempDir tmp("rerun");
    make_db(tmp.path / "preset.db", "strategy", "from_preset");
    auto cfg = write_config(tmp.path, "[storage]\npreset=preset.db\ndatabase=work.db\n");
    {
        Storage storage = cfg.configure_storage();
        auto wr = storage.write();
        wr.put("v", "changed_by_run");
        wr.commit();
    }
    Storage storage = cfg.configure_storage();
    CHECK_EQUAL(read_v(storage), std::string("from_preset"));
    //preset itself must stay untouched by the run
    Storage preset(LevelDBStorageManager::open_db(tmp.path / "preset.db").get_storage("strategy"));
    CHECK_EQUAL(read_v(preset), std::string("from_preset"));
}

void test_custom_storage_name() {
    TempDir tmp("name");
    make_db(tmp.path / "preset.db", "alpha", "alpha_value");
    auto cfg = write_config(tmp.path, "[storage]\npreset=preset.db\ndatabase=work.db\nname=alpha\n");
    Storage storage = cfg.configure_storage();
    CHECK_EQUAL(read_v(storage), std::string("alpha_value"));
}

void test_no_preset_starts_empty() {
    TempDir tmp("nopreset");
    make_db(tmp.path / "work.db", "strategy", "stale");
    auto cfg = write_config(tmp.path, "[storage]\ndatabase=work.db\n");
    Storage storage = cfg.configure_storage();
    CHECK(is_leveldb(storage));
    CHECK_EQUAL(read_v(storage), std::string("<missing>"));
}

void test_refuses_to_delete_non_leveldb_directory() {
    TempDir tmp("notdb");
    std::filesystem::create_directories(tmp.path / "work.db");
    std::ofstream(tmp.path / "work.db" / "keep.txt") << "precious";
    auto cfg = write_config(tmp.path, "[storage]\ndatabase=work.db\n");
    CHECK_EXCEPTION(std::runtime_error, cfg.configure_storage());
    CHECK(std::filesystem::exists(tmp.path / "work.db" / "keep.txt"));
}

void test_refuses_database_same_as_preset() {
    TempDir tmp("same");
    make_db(tmp.path / "preset.db", "strategy", "from_preset");
    auto cfg = write_config(tmp.path, "[storage]\npreset=preset.db\ndatabase=./preset.db\n");
    CHECK_EXCEPTION(std::runtime_error, cfg.configure_storage());
    Storage preset(LevelDBStorageManager::open_db(tmp.path / "preset.db").get_storage("strategy"));
    CHECK_EQUAL(read_v(preset), std::string("from_preset"));
}

int main() {
    test_no_section_uses_memory();
    test_preset_is_copied();
    test_rerun_resets_to_preset();
    test_custom_storage_name();
    test_no_preset_starts_empty();
    test_refuses_to_delete_non_leveldb_directory();
    test_refuses_database_same_as_preset();
    return 0;
}
