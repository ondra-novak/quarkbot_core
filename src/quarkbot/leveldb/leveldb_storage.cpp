#include "leveldb_storage.hpp"
#include "quarkbot/utils/bigendian.hpp"
#include <algorithm>
#include <bit>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <leveldb/db.h>
#include <leveldb/iterator.h>
#include <leveldb/options.h>
#include <leveldb/status.h>
#include <leveldb/write_batch.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <sys/types.h>


namespace quarkbot {


struct FixBufForRecordKey {
    std::array<char, sizeof(RecordKey)> buf;
    operator RecordKey() const {
        return std::bit_cast<RecordKey>(buf);
    }
    operator std::string_view() const {
        return std::string_view(buf.data(), buf.size());
    }   
};

inline FixBufForRecordKey record_key_to_string(const RecordKey &key) {
    FixBufForRecordKey buf;
    auto iter = buf.buf.begin();
    big_endian_binarize(key.ordered, iter);
    big_endian_binarize(key.random, iter);
    return buf;
}

inline RecordKey string_to_record_key(std::string_view str) {
    RecordKey key;
    auto iter = str.begin();
    big_endian_unbinarize(key.ordered, iter);
    big_endian_unbinarize(key.random, iter);
    return key;
}


struct SnapshotDeleter {
    leveldb::DB *db;
    void operator()(const leveldb::Snapshot *s){
        db->ReleaseSnapshot(s);
    }
};

using PSnapshot = std::unique_ptr<const leveldb::Snapshot, SnapshotDeleter>;

PSnapshot get_snapshot(leveldb::DB &db) {
    return PSnapshot(db.GetSnapshot(), {&db});
}

leveldb::Options LevelDBStorageManager::_default_options = {};

static bool check_status(const leveldb::Status &st) {
    if (!st.ok()) {
        if (st.IsNotFound()) return false;
        throw std::runtime_error("LevelDB error: "+ st.ToString());
    }
    return true;
}


void LevelDBStorageManager::set_default_options(const leveldb::Options &ops) {
    _default_options = std::move(ops);
}
LevelDBStorageManager LevelDBStorageManager::open_db(const std::filesystem::path &path) {
    return open_db(path, _default_options);
}

LevelDBStorageManager LevelDBStorageManager::open_db(const std::filesystem::path &path, const leveldb::Options &options) {
    leveldb::DB *ptr = nullptr;
    auto st = leveldb::DB::Open(options, path.string(),&ptr);
    if (!st.ok()) throw Exception(std::move(st));
    return PDB(ptr);
}

static std::string build_key(uint8_t id, std::string_view var, RecordKey key ) {
    std::string s;
    s.push_back(static_cast<char>(id));
    s.append(var);
    s.push_back(0);
    s.append(record_key_to_string(key));
    return s;
}

static std::string build_key(uint8_t id, std::string_view var) {
    std::string s;
    s.push_back(static_cast<char>(id));
    s.append(var);
    return s;
}

static RecordKey extract_key(std::string_view s) {
    //record key is always sizeof(RecordKey) bytes from end;
    return string_to_record_key(s.substr(s.size()-sizeof(RecordKey)));
}


std::uint8_t LevelDBStorageManager::find_storage(std::string_view name) {
    std::string v;
    bool found = check_status(_db->Get({}, build_key(directory_id, name), &v));
    if (!found) return directory_id;
    else return static_cast<std::uint8_t>(v[0]);
}
std::uint8_t LevelDBStorageManager::create_storage(std::string_view name) {
    std::bitset<max_storage_count + 1> used;
    auto iter = std::unique_ptr<leveldb::Iterator>(_db->NewIterator({}));
    auto k = build_key(directory_id, "");
    iter->Seek(k);
    while (iter->Valid()) {
        if (!iter->key().starts_with(k)) break;
        std::uint8_t val = static_cast<std::uint8_t>(iter->value().data()[0]);
        if (val < used.size()) used.set(val);
        iter->Next();
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(max_storage_count); ++i) {
        if (!used.test(i)) {
            auto id = static_cast<std::uint8_t>(i);
            _db->Put({}, build_key(directory_id, name), build_key(id, ""));
            return id;
        }
    }
    return directory_id;
}



PStorage LevelDBStorageManager::get_storage(std::string_view name) {
    auto id = find_storage(name);
    if (id == directory_id) id = create_storage(name);
    if (id == directory_id) throw std::runtime_error("LevelDB storage: No room for another storage");
    return std::make_shared<LevelDBStorage>(_db, id);
}

void LevelDBStorageManager::delete_storage(std::string_view name) {
    leveldb::WriteBatch b;
    auto id = find_storage(name);
    if (id == directory_id) return ;    
    auto iter = std::unique_ptr<leveldb::Iterator>(_db->NewIterator({}));

    auto delete_prefix = [&](std::uint8_t id) {
        auto k = build_key(id,"");
        iter->Seek(k);
        while (iter->Valid()) {
            if (!iter->key().starts_with(k))  break;
            b.Delete(iter->key());
            iter->Next();
        }
    };

    delete_prefix(id);
    b.Delete(build_key(directory_id, name));
    _db->Write({}, &b);   
}

LevelDBStorage::Value LevelDBStorage::get(std::string_view variable_name) const {
    std::string v;
    bool found = check_status(_proxy->Get({}, build_key(_keyspace_id, variable_name), &v));
    if (!found || v.size() != sizeof(RecordKey)) return {{}, {}, false, {}};
    std::array<char, sizeof(RecordKey)> buff;
    std::copy(v.begin(), v.end(), buff.begin());
    return get(variable_name, std::bit_cast<RecordKey>(buff));
}
LevelDBStorage::Value LevelDBStorage::get(std::string_view variable_name, const RecordKey &key) const {
    std::string v;
    Value out {{},{}, false, key};
    bool found = check_status(_proxy->Get({}, build_key(_keyspace_id, variable_name, key), &v));
    if (found) {
        out.data = std::move(v);
        out.exists = true;
    }
    return out;
}

static inline std::string_view slice2string_view(const leveldb::Slice &s) {
    return std::string_view(s.data(), s.size());
}

LevelDBStorage::Enumerator LevelDBStorage::get_enumerator(std::string_view variable_name, const RecordKey &since, const RecordKey &until) const {
    auto iter = std::shared_ptr<leveldb::Iterator>(_proxy->NewIterator({}));
    auto beg = build_key(_keyspace_id, variable_name, since);
    auto end = build_key(_keyspace_id, variable_name, until);
    if (beg < end) {
        iter->Seek(beg);
        return [iter, end, sz = variable_name.size()+2](ValueView &w)mutable -> bool {
            if (!iter->Valid() || slice2string_view(iter->key()) >= end) return false;
            w.key = extract_key(slice2string_view(iter->key()).substr(sz));
            w.data = slice2string_view(iter->value());
            iter->Next();
            return true;
        };
    } else if (beg > end) {
        iter->Seek(beg);
        // Seek lands at first >= beg; step back if it overshot so we start inclusive at beg
        if (!iter->Valid() || slice2string_view(iter->key()) > beg) iter->Prev();
        return [iter, end, sz = variable_name.size()+2](ValueView &w)mutable -> bool {
            if (!iter->Valid()) return false;
            auto key = slice2string_view(iter->key());
            if (key <= end) return false;
            w.key = extract_key(key.substr(sz));
            w.data = slice2string_view(iter->value());
            iter->Prev();
            return true;
        };
    } else {
        return [](ValueView &){return false;};
    }

}
std::vector<std::string> LevelDBStorage::list(std::string_view str_prefix) const {
    //we search for all keys in format <kid 1b><name><0><recordkey 16b>
    //if found, add <name> to list and seek to <kid 1b><name><1> - which skips all recordkeys
    std::vector<std::string> out;
    auto iter = std::unique_ptr<leveldb::Iterator>(_proxy->NewIterator({}));
    auto prefix = build_key(_keyspace_id, str_prefix);
    iter->Seek(prefix);
    std::string varname;
    while (iter->Valid()) {
        if (!iter->key().starts_with(prefix)) break;        
        auto dbkey = slice2string_view(iter->key());        
        if (dbkey.size() > sizeof(RecordKey) + 1 && dbkey[dbkey.size() - sizeof(RecordKey) - 1] == '\0') {
            varname.clear();
            varname.append(dbkey.substr(0, dbkey.size() - sizeof(RecordKey)-1) );
            out.push_back(varname.substr(1));   //remove keyspace_id
            varname.push_back('\x01');  //next key;
            iter->Seek(varname);
        } else {
            iter->Next();
        }
    }
    return out;
}
LevelDBStorage::Value LevelDBStorage::get_schema_binary(SchemaHash h) const {
    auto hbin = std::bit_cast<std::array<char, sizeof(h)> >(h);
    std::string key = build_key(schema_keyspace, {hbin.begin(), hbin.end()});
    Value out;
    out.exists = check_status(_proxy->Get({}, key, &out.data));
    return out;
};

PStorageTransaction LevelDBStorage::write() {
    return std::make_unique<LevelDBTransaction>(shared_from_this());
}
uint8_t LevelDBStorage::get_keyspace_id() const {return _keyspace_id;}
LevelDBStorage::PDB LevelDBStorage::get_db() const {return _proxy;}


PStorage LevelDBTransaction::get_storage() const {
    return _storage;
}
void LevelDBTransaction::commit(bool sync) {
    auto db = _storage->get_db();
    leveldb::WriteOptions opts;
    opts.sync = sync;
    check_status(db->Write(opts,&_batch));
    _batch = {};    
}

static std::atomic<std::uint64_t> unique_key_generator = {};

RecordKey LevelDBTransaction::put(std::string_view variable_name, std::string_view content) {
    RecordKey key{
        static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),
        unique_key_generator++
    };
    put(variable_name, key, content, UpdateLastRevision::enable);
    return key;
}
void LevelDBTransaction::put(std::string_view variable_name, const RecordKey &key, std::string_view content, UpdateLastRevision update_last_revision) {
    auto kid = _storage->get_keyspace_id();
    _batch.Put(build_key(kid,variable_name, key), {content.data(), content.length()});
    auto &watcher = _storage->get_watcher();
    if (update_last_revision == UpdateLastRevision::enable) {
        auto rw = std::bit_cast<std::array<char, sizeof(RecordKey)> >(key);
        _batch.Put(build_key(kid,variable_name), {rw.data(), rw.size()});
        watcher(*this, variable_name, key, content);
    }
}
void LevelDBTransaction::erase(std::string_view variable_name) {
    auto iter =std::unique_ptr<leveldb::Iterator>( _storage->get_db()->NewIterator({}));
    auto &watcher = _storage->get_watcher();    
    iter->Seek({variable_name.data(), variable_name.size()});
    char kid = static_cast<char>(_storage->get_keyspace_id());
    auto sz = variable_name.size();
    while (iter->Valid()) {
        auto k = slice2string_view(iter->key());
        if (k.empty() || k[0] != kid || k.substr(1,sz) != variable_name) break;
        k = k.substr(1);       
        if (k.size() == sz || k[sz] == '\0')  {
            _batch.Delete(iter->key());
        }
        iter->Next();        
    }
    watcher(*this,variable_name, RecordKey::min(), std::nullopt);
}
void LevelDBTransaction::erase(std::string_view variable_name, const RecordKey &key) {
    _batch.Delete(build_key(_storage->get_keyspace_id(), variable_name, key));
    auto &watcher = _storage->get_watcher();    
    watcher(*this, variable_name, key, std::nullopt);

}
void LevelDBTransaction::put_schema_binary(SchemaHash hash, std::string_view binary) {
    auto hbin = std::bit_cast<std::array<char, sizeof(SchemaHash)> >(hash);
    _batch.Put(build_key(LevelDBStorage::schema_keyspace, {hbin.data(), hbin.size()}), {binary.data(), binary.size()});
}

void LevelDBStorage::add_precommit_hook_connection(WatcherSlot::Connection consumer) {
    connect(_watcher,consumer);
}

}

