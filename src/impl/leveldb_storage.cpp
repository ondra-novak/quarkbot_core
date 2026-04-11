#include "leveldb_storage.hpp"
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <leveldb/db.h>
#include <leveldb/iterator.h>
#include <leveldb/options.h>
#include <leveldb/status.h>
#include <leveldb/write_batch.h>
#include <memory>
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

static std::string build_key(uint8_t id, std::string_view key) {
    std::string s;
    s.push_back(static_cast<char>(id));
    s.append(key);
    return s;
}

static void append_rev(std::string &key, std::size_t rev) {
    for (int i = 0; i < 8; ++i) {
        key.push_back(static_cast<char>((rev >> ((7-i) * 8)) & 0xFF));
    }
}

static void key_append_rev(std::string &key, std::size_t rev) {
    append_rev(key, rev);
}

static std::string build_key(uint8_t id, std::string_view key, std::size_t rev) {
    auto k = build_key(id, key);
    key_append_rev(k, rev);
    return k;
}

static std::string build_revision(std::size_t rev) {
    std::string r;
    append_rev(r , rev);
    return r;
}


static std::size_t extract_revision(std::string_view s) {
    s = s.substr(s.length()-8);
    std::size_t rev = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        rev = (rev << 8) | static_cast<std::uint8_t>(s[i]);
    }
    return rev;
}


std::uint8_t LevelDBStorageManager::find_storage(std::string_view name) {
    std::string v;
    bool found = check_status(_db->Get({},build_key(static_cast<std::uint8_t>(directory_id<<1), name), &v));
    if (!found) return directory_id;
    else return static_cast<std::uint8_t>(v[0]);
}
std::uint8_t LevelDBStorageManager::create_storage(std::string_view name) {
    std::bitset<directory_id+1> used;
    auto iter = std::unique_ptr<leveldb::Iterator>(_db->NewIterator({}));
    auto k = build_key(static_cast<std::uint8_t>(directory_id<<1), "");
    iter->Seek(k);
    while (iter->Valid()) {
        if (!iter->key().starts_with(k)) break;
        std::uint8_t val = static_cast<std::uint8_t>(iter->value().data()[0]);
        used.set(val);
        iter->Next();
    }
    for (std::size_t i = 0; i < used.size();++i) {
        if (!used.test(i)) {
            if (i >= directory_id) break;
            auto id = static_cast<std::uint8_t>(i);
            auto nk = build_key(directory_id, name);
            _db->Put({}, nk, build_key(id,""));
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

    delete_prefix(static_cast<std::uint8_t>(id<<1));
    delete_prefix(static_cast<std::uint8_t>((id<<1)+1));
    b.Delete(build_key(directory_id, name));
    _db->Write({}, &b);   
}
std::vector<std::string> LevelDBStorageManager::list() {
    LevelDBStorage tmpstor(_db,directory_id);
    return tmpstor.list({{},false});
}


LevelDBStorage::Value LevelDBStorage::get(Key key) const {
    auto &db = *_proxy;
    std::string valbuff;
    if (key.sequence) {
        auto snap = get_snapshot(db);
        leveldb::ReadOptions opts;
        opts.snapshot = snap.get();
        auto k = build_key(_keyspace_id+1, key.name);
        auto r = check_status(db.Get(opts,k, &valbuff));
        if (!r) return {0,false,{}};        
        auto rev = extract_revision(valbuff);
        k.append(valbuff);
        r = check_status(db.Get(opts, k, &valbuff));
        return {rev, r, std::move(valbuff)};
    } else {
        auto k = build_key(_keyspace_id, key.name);
        auto r = check_status(db.Get({},k, &valbuff));
        return {0, r, std::move(valbuff)};
    }

}
LevelDBStorage::Value LevelDBStorage::get(Key key, Revision rev) const {
    auto &db = *_proxy;
    if (!key.sequence) return get(key);
    else {
        std::string valbuff;
        std::string k = build_key(_keyspace_id+1, key.name, rev);
        auto r = check_status(db.Get({},k, &valbuff));
        return {rev, r, std::move(valbuff)};
    }
}

std::vector<std::string> LevelDBStorage::list(const Key &filter) const {
    std::vector<std::string> out;
    auto &db = *_proxy;
    leveldb::ReadOptions opts;
    auto snap = get_snapshot(db);
    opts.snapshot = snap.get();
    auto iter = std::unique_ptr<leveldb::Iterator>(db.NewIterator(opts));
    if (filter.sequence) {
        auto k = build_key(_keyspace_id+1,filter.name);
        std::string tmp1, tmp2;
        iter->Seek(k);
        while (iter->Valid()) {
            auto fk = iter->key();
            if (!fk.starts_with(k)) break;
            auto fv = iter->value();
            if (fv.size() == 8) {
                tmp1 = fk.ToString();
                tmp1.append(fv.data(), fv.size());
                if (check_status(db.Get(opts, tmp1, &tmp2))) {
                    fk.remove_prefix(1);
                    out.push_back(fk.ToString());
                }
            }
        }
    } else {
        auto k = build_key(_keyspace_id,filter.name);
        iter->Seek(k);
        while (iter->Valid()) {
            auto fk = iter->key();
            if (!fk.starts_with(k)) break;
            fk.remove_prefix(1);
            out.emplace_back(fk.ToString());
            iter->Next();            
        }
    }    
    return out;
}

LevelDBTransaction::LevelDBTransaction(PDB proxy, uint8_t keyspace_id, bool sync)
        :_proxy(std::move(proxy))
        ,_keyspace_id((keyspace_id))
        ,_sync(sync) {}


        
LevelDBTransaction::Revision LevelDBTransaction::put_rev(std::string_view key) {
        std::string k = build_key(_keyspace_id+1, key);            
        std::string v;
        auto t = check_status(_proxy->Get({}, k, &v));
        auto r = t?extract_revision(v):0;
        ++r;
        v = build_revision(r);
        _proxy->Put({},k,v);
        return r;
    }

LevelDBTransaction::Revision LevelDBTransaction::put(Key key, std::string_view value_blob) {
        std::string v;
        if (!key.sequence) {
            _batch.Put(build_key(_keyspace_id, key.name), {value_blob.data(), value_blob.size()});
            return 0;
        }  else {
            auto r = put_rev(key.name);
            _batch.Put(build_key(_keyspace_id+1, key.name, r), {value_blob.data(), value_blob.size()});
            return r;
        }

    }

LevelDBTransaction::Revision LevelDBTransaction::erase(Key key) {
        std::string v;
        if (!key.sequence) {
            _batch.Delete(build_key(_keyspace_id+1, key.name));
            return 0;
        }  else {
            auto r = put_rev(key.name);
            return r;
        }
    }

void LevelDBTransaction::erase(std::string_view key, Revision rev) {
    auto k = build_key(_keyspace_id+1, key, rev);
    _batch.Delete(k);
}


void LevelDBTransaction::prune_history(std::string_view key, Revision to) {
        std::string v;
        auto k = build_key(_keyspace_id+1, key);
        auto bk = build_key(_keyspace_id+1, key, 0);        
        auto t = check_status(_proxy->Get({},k, &v));
        if (!t) return;
        Revision maxrev =extract_revision(v);
        to = std::min(to,maxrev);
        auto iter = std::unique_ptr<leveldb::Iterator>(_proxy->NewIterator({}));
        iter->Seek(bk);
        while (iter->Valid()) {
            auto key = iter->key();
            if (key.starts_with(k)) break;
            auto rev = extract_revision({key.data(),key.size()});
            if (rev >= to) break;
            _batch.Delete(key);            
        }
    }

void LevelDBTransaction::commit() {
        leveldb::WriteOptions opts;
        opts.sync = _sync;
        check_status(_proxy->Write(opts, &_batch));
}
    

PStorageTransaction LevelDBStorage::write(bool sync) {
    return std::make_unique<LevelDBTransaction>(_proxy, _keyspace_id, sync);
}


}

