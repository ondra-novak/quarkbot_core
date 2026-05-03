#pragma once

#include "ifc/defs.hpp"
#include "ifc/storage.hpp"
#include <cstdint>
#include <leveldb/db.h>
#include <leveldb/status.h>
#include <leveldb/write_batch.h>
#include <stdexcept>
#include <string_view>
#include <filesystem>
#include <memory>
#include <leveldb/options.h>


namespace quarkbot {


    class LevelDBStorageManager: public IStorageManager {
    public:
        using PDB = std::shared_ptr<leveldb::DB>;

        static constexpr int max_storage_count = 127;
        static constexpr std::uint8_t directory_id = (max_storage_count<<1);

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



    class LevelDBStorage : public IStorage{
    public:
        using PDB = LevelDBStorageManager::PDB;

        LevelDBStorage(PDB db, uint8_t instance_id);

        virtual Value get(Key key) const override;
        virtual Value get(Key key, Revision rev) const override;
        virtual std::vector<std::string> list(const Key &filter) const override;
        virtual PStorageTransaction write(bool sync) override;
        virtual Value get_schema_raw(SchemaHash schema_hash) const override;
    protected:
        PDB _proxy;
        uint8_t _keyspace_id;
    };


class LevelDBTransaction: public IStorageTransaction {
public:
    using PDB = LevelDBStorageManager::PDB;

    LevelDBTransaction(PDB proxy, uint8_t keyspace_id, bool sync);
    
    virtual Revision put(Key key, std::string_view value_blob) override;
    virtual Revision erase(Key key) override;
    virtual void erase(std::string_view key, Revision rev) override;
    virtual void prune_history(std::string_view key, Revision to) override;
    virtual void commit() override;
    virtual void put_schema(SchemaHash hash, std::string_view schema) override;

protected:
    leveldb::WriteBatch _batch;    
    PDB _proxy;
    uint8_t _keyspace_id;
    bool _sync;

    Revision put_rev(std::string_view key);

    Revision get_revision(std::string_view keyname);
};

}


