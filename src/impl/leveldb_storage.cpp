#include "leveldb_storage.hpp"
#include <cstdint>
#include <leveldb/db.h>
#include <leveldb/iterator.h>
#include <leveldb/options.h>
#include <leveldb/status.h>
#include <leveldb/write_batch.h>
#include <memory>
#include <stdexcept>
#include <sys/types.h>


namespace quarkbot {

class LevelDBProxy {
public:

    uint8_t resolve_keyspace(std::string_view name);
    void erase_keyspace(uint8_t id);
    leveldb::DB &get_db() const {return *_db;}

protected:
    std::unique_ptr<leveldb::DB> _db;
};


LevelDBStorage::LevelDBStorage(PLevelDBProxy db, std::string_view name)
:_proxy(std::move(db)), _keyspace_id(_proxy->resolve_keyspace(name))
{

}
LevelDBStorage::LevelDBStorage(PLevelDBProxy db, uint8_t instance_id)
:_proxy(std::move(db)), _keyspace_id(instance_id)
{

}
static std::string build_key(uint8_t id, std::string_view key) {
    std::string s;
    s.push_back(static_cast<char>(id));
    s.append(key);
    return s;
}

static void key_append_rev(std::string &key, std::size_t rev) {
    key.push_back(0);
    while (rev) {
        key.push_back(static_cast<char>(rev & 0xFF));
        rev >>= 8;
    }    

}

static std::string build_key(uint8_t id, std::string_view key, std::size_t rev) {
    auto k = build_key(id, key);
    key_append_rev(k, rev);
    return k;
}

static bool check_status(const leveldb::Status &st) {
    if (!st.ok()) {
        if (st.IsNotFound()) return false;
        throw std::runtime_error("LevelDB error: "+ st.ToString());
    }
    return true;
}

static std::size_t extract_revision(std::string_view s) {
    auto len = std::min<std::size_t>(s.size(),sizeof(std::size_t));
    std::size_t rev = 0;
    for (std::size_t i = 0; i < len; ++i) {
        rev = rev << 8 | static_cast<std::uint8_t>(s[i]);        
    }
    return rev;
}

LevelDBStorage::Value LevelDBStorage::get(Key key) const {
    auto &db = _proxy->get_db();
    std::string valbuff;
    if (key.sequence) {
        auto k = build_key(_keyspace_id, key.name);
        auto r = check_status(db.Get({},k, &valbuff));
        if (!r) return {0,false,{}};
        auto rev = extract_revision(valbuff);
        key_append_rev(k, rev);
        r = check_status(db.Get({}, k, &valbuff));
        return {rev, r, std::move(valbuff)};
    } else {
        auto k = build_key(_keyspace_id+1, key.name);
        auto r = check_status(db.Get({},k, &valbuff));
        return {0, r, std::move(valbuff)};
    }

}
LevelDBStorage::Value LevelDBStorage::get(Key key, Revision rev) const {
    auto &db = _proxy->get_db();
    if (!key.sequence) return get(key);
    else {
        std::string valbuff;
        std::string k = build_key(_keyspace_id, key.name, rev);
        auto r = check_status(db.Get({},k, &valbuff));
        return {rev, r, std::move(valbuff)};
    }
}

std::vector<std::string> LevelDBStorage::list(const Key &filter) const {
    auto &db = _proxy->get_db();
    auto iter = std::unique_ptr<leveldb::Iterator>(db.NewIterator({}));
    auto beg = build_key(static_cast<std::uint8_t>(_keyspace_id+(filter.sequence?0:1)),filter.name);
    iter->Seek(beg);
    auto st = iter->status();
    std::vector<std::string> out;
    while (st.ok()) {
        auto kslice = iter->key();
        if (!kslice.starts_with(beg)) break;
        std::string_view key(kslice.data(), kslice.size());
        auto n = key.find('\0');
        if (n == key.npos) {
            out.emplace_back(key);
        }
        iter->Next();
        st = iter->status();
    }
    return out;
}

class LevelDBTransaction: public IStorageTransaction {
public:
    LevelDBTransaction(PLevelDBProxy proxy, uint8_t keyspace_id, bool sync)
        :_proxy(std::move(proxy))
        ,_keyspace_id((keyspace_id))
        ,_sync(sync) {}


    Revision get_revision(std::string_view keyname) {
        std::string s;
        auto r = check_status(_proxy->get_db().Get({}, build_key(_keyspace_id, keyname),&s));
        if (r) return extract_revision(s);
        return 0;

    }

    std::pair<std::string,Revision> put_rev(std::string_view key) {
        std::string k = build_key(_keyspace_id, key);            
        std::string v;
        auto t = check_status(_proxy->get_db().Get({}, k, &v));
        auto r = t?extract_revision(v):0;
        ++r;
        auto nk = k;
        key_append_rev(nk,r);
        auto nksv = std::string_view(nk);
        nksv = nksv.substr(1);
        _batch.Put(k, {nksv.data(), nksv.size()});
        return std::pair<std::string,Revision>{nk, r};

    }

    Revision put(Key key, std::string_view value_blob) {
        std::string v;
        if (!key.sequence) {
            _batch.Put(build_key(_keyspace_id+1, key.name), {value_blob.data(), value_blob.size()});
            return 0;
        }  else {
            auto r = put_rev(key.name);
            _batch.Put(r.first, {value_blob.data(), value_blob.size()});
            return r.second;
        }

    }

    virtual Revision erase(Key key) {
        std::string v;
        if (!key.sequence) {
            _batch.Delete(build_key(_keyspace_id+1, key.name));
            return 0;
        }  else {
            auto r = put_rev(key.name);
            return r.second;
        }
    }

    void prune_history(std::string_view key, Revision to) {
        std::string v;
        std::string k = build_key(_keyspace_id, key);
        auto t = check_status(_proxy->get_db().Get({},k, &v));
        if (!t) return;
        Revision maxrev =extract_revision(v);
        to = std::min(to,maxrev);
        auto fkey = build_key(_keyspace_id, key);
        fkey.push_back('\0');
        auto iter = std::unique_ptr<leveldb::Iterator>(_proxy->get_db().NewIterator({}));
        iter->Seek(fkey);
        auto st = iter->status();
        while (st.ok()) {
            auto k = iter->key();
            if (!k.starts_with(fkey))  break;
            k.remove_prefix(fkey.size());
            auto rev = extract_revision({k.data(), k.size()});
            if (rev < to) {
                _batch.Delete(iter->key());                
            }
        }
    }

    virtual void commit() {
        leveldb::WriteOptions opts;
        opts.sync = _sync;
        check_status(_proxy->get_db().Write(opts, &_batch));
    }
    

protected:
    leveldb::WriteBatch _batch;    
    PLevelDBProxy _proxy;
    uint8_t _keyspace_id;
    bool _sync;
};


PStorageTransaction LevelDBStorage::write(bool sync) {
    return std::make_unique<LevelDBTransaction>(_proxy, _keyspace_id, sync);
}

PLevelDBProxy open_db(const std::filesystem::path &path, const leveldb::Options &options);
PLevelDBProxy open_db(const std::filesystem::path &path, bool create_if_not_exists);


}

