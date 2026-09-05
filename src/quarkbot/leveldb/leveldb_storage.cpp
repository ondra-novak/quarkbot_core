#include "leveldb_storage.hpp"
#include "../common/mem_storage.hpp"
#include "../common/storage_common.hpp"
#include "quarkbot/abstract/istorage.hpp"
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
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <sys/types.h>


namespace quarkbot {


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
    _default_options = ops;
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

static inline std::string_view slice2string_view(const leveldb::Slice &s) {
    return std::string_view(s.data(), s.size());
}

static std::string build_key(uint8_t id, std::string_view var, std::string_view key ) {
    std::string s;
    s.push_back(static_cast<char>(id));
    s.append(var);
    s.push_back(0);
    s.append(key);
    return s;
}

static std::string build_key(uint8_t id, std::string_view var, RecordKey key ) {
    return build_key(id,var,record_key_to_string(key));
}

static std::string build_key(uint8_t id, std::string_view var) {
    std::string s;
    s.push_back(static_cast<char>(id));
    s.append(var);
    return s;
}

static RecordKey extract_key(std::string_view s) {
    //record key is always recordkey_string_size bytes from end;
    return string_to_record_key(s.substr(s.size()-recordkey_string_size));
}


std::uint8_t LevelDBStorageManager::find_storage(std::string_view name) {
    std::string v;
    bool found = check_status(_db->Get({}, build_key(directory_id, name), &v));
    //an empty record carries no keyspace id - treat it as missing, so the storage
    //gets a fresh keyspace instead of silently aliasing keyspace 0
    if (!found || v.empty()) return directory_id;
    else return static_cast<std::uint8_t>(v[0]);
}
std::uint8_t LevelDBStorageManager::create_storage(std::string_view name) {
    std::bitset<max_storage_count + 1> used;
    auto iter = std::unique_ptr<leveldb::Iterator>(_db->NewIterator({}));
    auto k = build_key(directory_id, "");
    iter->Seek(k);
    while (iter->Valid()) {
        if (!iter->key().starts_with(k)) break;
        if (!iter->value().empty()) {
            std::uint8_t val = static_cast<std::uint8_t>(iter->value().data()[0]);
            if (val < used.size()) used.set(val);
        }
        iter->Next();
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(max_storage_count); ++i) {
        if (!used.test(i)) {
            auto id = static_cast<std::uint8_t>(i);
            check_status(_db->Put({}, build_key(directory_id, name), build_key(id, "")));
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
    check_status(_db->Write({}, &b));
}

std::vector<std::string> LevelDBStorageManager::list() {
    std::vector<std::string> out;
    auto iter = std::unique_ptr<leveldb::Iterator>(_db->NewIterator({}));
    auto prefix = build_key(directory_id, "");
    iter->Seek(prefix);
    while (iter->Valid()) {
        if (!iter->key().starts_with(prefix)) break;
        out.emplace_back(slice2string_view(iter->key()).substr(prefix.size()));
        iter->Next();
    }
    return out;
}

LevelDBStorage::LevelDBStorage(PDB db, uint8_t instance_id)
    :_proxy(std::move(db))
    ,_keyspace_id(instance_id) {}

LevelDBStorage::Value LevelDBStorage::get(std::string_view variable_name) const {
    std::string v;
    bool found = check_status(_proxy->Get({}, build_key(_keyspace_id, variable_name), &v));
    if (!found || v.size() != recordkey_string_size) return {{}, {}, false, {}};
    return get(variable_name, string_to_record_key(v));
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

LevelDBStorage::Enumerator LevelDBStorage::get_enumerator(std::string_view variable_name,
        const RecordKey &from, const RecordKey &to, RangeDirection dir) const {

    const bool ascending = dir == RangeDirection::ascending;
    //the direction only restates the order of the bounds - a disagreement is a mistake
    //on the caller's side, not a request to iterate the other way
    if (ascending ? !(from < to) : !(from > to)) return [](ValueView &){return false;};

    //bounds of the half-open range [lower, upper), computed once for both directions
    auto lo = build_key(_keyspace_id, variable_name, ascending?from:to);
    auto hi = build_key(_keyspace_id, variable_name, ascending?to:from);
    auto iter = std::shared_ptr<leveldb::Iterator>(_proxy->NewIterator({}));
    auto sz = variable_name.size()+2;

    // The iterator is advanced lazily, at the beginning of the next call - never right
    // after filling ValueView. leveldb::DBIter::value() may point into a buffer owned by
    // the iterator (saved_value_ while iterating backwards, or the current block of an
    // SST file), and advancing invalidates it. Lazy advance keeps the view valid exactly
    // as long as ValueView promises: until the next iteration step.
    if (ascending) {
        iter->Seek(lo);
        return [iter, hi, sz, advance = false](ValueView &w)mutable -> bool {
            if (advance) iter->Next();
            advance = true;
            if (!iter->Valid()) return false;
            auto key = slice2string_view(iter->key());
            if (key >= hi) return false;
            w.key = extract_key(key.substr(sz));
            w.data = slice2string_view(iter->value());
            return true;
        };
    } else {
        //land on the largest key below the exclusive upper bound
        iter->Seek(hi);
        if (iter->Valid()) iter->Prev(); else iter->SeekToLast();
        return [iter, lo, sz, advance = false](ValueView &w)mutable -> bool {
            if (advance) iter->Prev();
            advance = true;
            if (!iter->Valid()) return false;
            auto key = slice2string_view(iter->key());
            if (key < lo) return false;
            w.key = extract_key(key.substr(sz));
            w.data = slice2string_view(iter->value());
            return true;
        };
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
        if (dbkey.size() > recordkey_string_size + 1 && dbkey[dbkey.size() - recordkey_string_size - 1] == '\0') {
            varname.clear();
            varname.append(dbkey.substr(0, dbkey.size() - recordkey_string_size-1) );
            out.push_back(varname.substr(1));   //remove keyspace_id
            varname.push_back('\x01');  //next key;
            iter->Seek(varname);
        } else {
            iter->Next();
        }
    }
    return out;
}
LevelDBStorage::Value LevelDBStorage::get_schema_binary(srl::SchemaHash h) const {
    auto hbin = std::bit_cast<std::array<char, sizeof(h)> >(h);
    std::string key = build_key(schema_keyspace, {hbin.begin(), hbin.end()});
    Value out;
    out.exists = check_status(_proxy->Get({}, key, &out.data));
    return out;
};

PStorageTransaction LevelDBStorage::write(CommitMode cmode) {
    return std::make_unique<LevelDBTransaction>(shared_from_this(), cmode);    
}
uint8_t LevelDBStorage::get_keyspace_id() const {return _keyspace_id;}
LevelDBStorage::PDB LevelDBStorage::get_db() const {return _proxy;}


PStorage LevelDBTransaction::get_storage() const {
    return _storage;
}
void LevelDBTransaction::commit() {
    auto db = _storage->get_db();
    leveldb::WriteOptions opts;
    opts.sync = _cmode == CommitMode::sync;
    check_status(db->Write(opts,&_batch));    
    auto repl = _storage->get_replicator();
    _batch.Iterate(&repl);
    _batch.Clear();;    
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
    if (update_last_revision != UpdateLastRevision::disable) {
        if (update_last_revision == UpdateLastRevision::enable_erase_last) {
            std::string v;
            auto db = _storage->get_db();
            bool found = check_status(db->Get({}, build_key(kid, variable_name), &v));
            if (found) {
                _batch.Delete(build_key(kid,variable_name,v));
            }
        }
        auto rw = record_key_to_string(key);
        _batch.Put(build_key(kid,variable_name), {rw.data(), rw.size()});
    }
}
void LevelDBTransaction::erase(std::string_view variable_name) {
    auto iter =std::unique_ptr<leveldb::Iterator>( _storage->get_db()->NewIterator({}));
    char kid = static_cast<char>(_storage->get_keyspace_id());
    iter->Seek(build_key(_storage->get_keyspace_id(), variable_name));
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
}
void LevelDBTransaction::erase(std::string_view variable_name, const RecordKey &key) {
    _batch.Delete(build_key(_storage->get_keyspace_id(), variable_name, key));

}
void LevelDBTransaction::put_schema_binary(srl::SchemaHash hash, std::string_view binary) {
    auto hbin = std::bit_cast<std::array<char, sizeof(srl::SchemaHash)> >(hash);
    _batch.Put(build_key(LevelDBStorage::schema_keyspace, {hbin.data(), hbin.size()}), {binary.data(), binary.size()});
}

void LevelDBStorage::add_replicator(Replicator::Connection consumer) {
    connect(_watcher,consumer);
}

void LevelDBTransaction::put(const IStorage::ReplicatorEvent &event) {
    //the event key is logical - prepend the keyspace this transaction writes into
    auto kid = event.is_schema? LevelDBStorage::schema_keyspace: _storage->get_keyspace_id();
    auto key = build_key(kid, event.key);
    if (event.erase) {
        _batch.Delete(key);
    } else {
        _batch.Put(key, {event.value.data(), event.value.size()});
    }
}

void LevelDBStorage::ReplicatorHandler::Put(const leveldb::Slice& key, const leveldb::Slice& value) {
    emit(key, slice2string_view(value), false);
}
void LevelDBStorage::ReplicatorHandler::Delete(const leveldb::Slice& key) {
    emit(key, {}, true);
}

void LevelDBStorage::ReplicatorHandler::emit(const leveldb::Slice &key, std::string_view value, bool erase) {
    using Type = ReplicatorEvent::Type;
    auto k = slice2string_view(key);
    if (k.empty()) return;
    auto kid = static_cast<std::uint8_t>(k[0]);
    bool is_schema = kid == schema_keyspace;
    auto logical = k.substr(1);

    if (is_schema) {
        //no IStorageTransaction operation erases a schema and delete_storage never
        //touches the schema keyspace, so a delete here has no event type and is dropped
        if (erase) return;
        auto h = schema_key_to_hash(logical);
        if (!h) return;
        repl(ReplicatorEvent{.type = Type::put_schema, .value = value, .schema_hash = *h,
                             .key = logical, .erase = erase, .is_schema = true});
        return;
    }

    //a data key ends with '\0' plus the 16 byte RecordKey; a last-revision pointer is
    //the bare name. A name whose own tail looks like that suffix would be misread, which
    //is inherent to the layout - variable names are identifiers in practice.
    const auto suffix = recordkey_string_size + 1;
    if (logical.size() > suffix && logical[logical.size() - suffix] == '\0') {
        repl(ReplicatorEvent{
            .type = erase?Type::erase_key:Type::put_key_value,
            .name = logical.substr(0, logical.size() - suffix),
            .recordkey = string_to_record_key(logical.substr(logical.size() - recordkey_string_size)),
            .value = value, .key = logical, .erase = erase});
        return;
    }

    if (erase) {
        repl(ReplicatorEvent{.type = Type::erase_latest, .name = logical,
                             .key = logical, .erase = true});
        return;
    }
    //the pointer keeps the newest revision in its *value*, not in its key
    if (value.size() != recordkey_string_size) return;
    repl(ReplicatorEvent{.type = Type::update_latest, .name = logical,
                         .recordkey = string_to_record_key(value),
                         .value = value, .key = logical});
}


LevelDBStorage::ReplicatorHandler LevelDBStorage::get_replicator()  {
    return ReplicatorHandler{_watcher};
}

bool LevelDBStorage::is_schema_stored(srl::SchemaHash hash) const {
    std::scoped_lock _(_set_mutex);
    if (_stored_schemas.contains(hash)) return true;
    auto r = get_schema_binary(hash);
    if (r.exists) {
        _stored_schemas.insert(hash);
        return true;
    }
    return false;
}

}

