#pragma once

#include "quarkbot/serializer/schema_fwd.hpp"
#include "quarkbot/storage.hpp"
#include <cstdint>
#include <leveldb/db.h>
#include <leveldb/status.h>
#include <leveldb/write_batch.h>
#include <stdexcept>
#include <string_view>
#include <filesystem>
#include <memory>
#include <leveldb/options.h>
#include <unordered_set>


namespace quarkbot {


    class LevelDBStorageManager final: public IStorageManager {
    public:
        using PDB = std::shared_ptr<leveldb::DB>;

        // 0x00..0xFD: storage keyspaces, 0xFE: schema (LevelDBStorage::schema_keyspace), 0xFF: directory

        static constexpr int max_storage_count = 253;
        static constexpr  std::uint8_t schema_keyspace  = 0xFE;
        static constexpr std::uint8_t directory_id = 0xFF;

        LevelDBStorageManager(PDB db):_db(std::move(db)) {}

        static void set_default_options(const leveldb::Options &ops);        
        static LevelDBStorageManager open_db(const std::filesystem::path &path);        
        static LevelDBStorageManager open_db(const std::filesystem::path &path, const leveldb::Options &options);
        virtual PStorage get_storage(std::string_view name) override;
        virtual void delete_storage(std::string_view name) override;
        virtual std::vector<std::string> list() override;


        PDB get_db() const {return _db;}

        class Exception: public std::runtime_error {
        public:
            Exception(leveldb::Status st)
                :std::runtime_error("LevelDB error: "+st.ToString())
                ,_st(std::move(st)) {}

            const leveldb::Status &get_status() {return _st;}
        protected:
            leveldb::Status _st;
        };
        

    protected:

        std::uint8_t find_storage(std::string_view name);
        std::uint8_t create_storage(std::string_view name);
        

        PDB _db;
        static leveldb::Options _default_options;
    };



    class LevelDBStorage final : public IStorage, public std::enable_shared_from_this<LevelDBStorage>{
    public:
        using PDB = LevelDBStorageManager::PDB;

        constexpr static std::uint8_t schema_keyspace  = LevelDBStorageManager::schema_keyspace;

        mutable std::mutex _set_mutex;
        mutable std::unordered_set<srl::SchemaHash> _stored_schemas = {};


        LevelDBStorage(PDB db, uint8_t instance_id);

        virtual Value get(std::string_view variable_name) const override;
        virtual Value get(std::string_view variable_name, const RecordKey &key) const override;
        virtual Enumerator get_enumerator(std::string_view variable_name, const RecordKey &from, const RecordKey &to, RangeDirection dir) const override;
        virtual std::vector<std::string> list(std::string_view prefix ) const override;
        virtual Value get_schema_binary(srl::SchemaHash h) const override;
        virtual PStorageTransaction write() override;
        virtual void add_replicator(Replicator::Connection consumer) override;
        virtual bool is_schema_stored(srl::SchemaHash hash) const override;

        uint8_t get_keyspace_id() const;
        PDB get_db() const;

        ///Turns records of a committed WriteBatch into ReplicatorEvents
        /**
            Physical keys carry a leading keyspace byte, which is stripped here: events
            must expose logical keys only, so they stay applicable to a different keyspace
            or a different backend. The keyspace byte also tells data records apart from
            schema records, which share the batch but live in schema_keyspace.
        */
        class ReplicatorHandler final: public leveldb::WriteBatch::Handler {
        public:
            ReplicatorHandler(Replicator &repl, std::uint8_t keyspace_id)
                :repl(repl), keyspace_id(keyspace_id) {}
            virtual void Put(const leveldb::Slice& key, const leveldb::Slice& value) override;
            virtual void Delete(const leveldb::Slice& key) override;
        protected:
            Replicator &repl;
            std::uint8_t keyspace_id;

            void emit(const leveldb::Slice &key, std::string_view value, bool erase);
        };

        ReplicatorHandler get_replicator();

    protected:
        PDB _proxy;
        uint8_t _keyspace_id;
        Replicator _watcher;
    };


class LevelDBTransaction final: public IStorageTransaction {
public:
    using PDB = LevelDBStorageManager::PDB;

    explicit LevelDBTransaction(std::shared_ptr<LevelDBStorage> storage);
    
    virtual PStorage get_storage() const  override;
    virtual void commit(bool sync) override;
    virtual RecordKey put(std::string_view variable_name, std::string_view content) override;
    virtual void put(std::string_view variable_name, const RecordKey &key, std::string_view content,
        UpdateLastRevision update_last_revision) override;
    virtual void erase(std::string_view variable_name) override;
    virtual void erase(std::string_view variable_name, const RecordKey &key) override;
    virtual void put_schema_binary(srl::SchemaHash hash, std::string_view binary) override;
    virtual void put(const IStorage::ReplicatorEvent &event) override;

protected:
    leveldb::WriteBatch _batch;    
    std::shared_ptr<LevelDBStorage> _storage;


};

}
