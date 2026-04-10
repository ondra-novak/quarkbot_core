#pragma once

#include "ifc/storage.hpp"
#include <filesystem>
#include <memory>
#include <leveldb/options.h>


namespace quarkbot {


    class LevelDBProxy;
    using PLevelDBProxy = std::shared_ptr<LevelDBProxy>;

    PLevelDBProxy open_db(const std::filesystem::path &path, const leveldb::Options &options);
    PLevelDBProxy open_db(const std::filesystem::path &path, bool create_if_not_exists);


    class LevelDBStorage : public IStorage{
    public:
        LevelDBStorage(PLevelDBProxy db, std::string_view name);
        LevelDBStorage(PLevelDBProxy db, uint8_t instance_id);

        virtual Value get(Key key) const override;
        virtual Value get(Key key, Revision rev) const override;
        virtual std::vector<std::string> list(const Key &filter) const override;
        virtual PStorageTransaction write(bool sync) override;
    protected:
        PLevelDBProxy _proxy;
        uint8_t _keyspace_id;
    };



}