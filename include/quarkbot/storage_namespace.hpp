#pragma once


#include "abstract/istorage.hpp"
#include "quarkbot/serializer/schema_fwd.hpp"
#include <memory>
namespace quarkbot {

///provides namespaces on top of existing storage
/** Note storage is not MT safe */
    class StorageNamespace final: public IStorage, public std::enable_shared_from_this<StorageNamespace> {
    public:

        StorageNamespace(PStorage root, std::string_view prefix)
            :_root(std::move(root))
            ,_key(prefix)
            ,_prefix_size(prefix.size()) {}

        virtual Value get(std::string_view variable_name) const override {
            return _root->get(add_prefix(variable_name));
        }
        virtual Value get(std::string_view variable_name, const RecordKey &key) const override {
            return _root->get(add_prefix(variable_name),key);
        }
        virtual Enumerator get_enumerator(std::string_view variable_name, const RecordKey &from, const RecordKey &to, RangeDirection dir) const override {
            return _root->get_enumerator(add_prefix(variable_name), from, to, dir);
        }
        virtual std::vector<std::string> list(std::string_view prefix ) const override {
            auto r = _root->list(add_prefix(prefix));
            for (auto &x: r) {
                x.erase(0, _prefix_size);
            }
            return r;
        }
        virtual Value get_schema_binary(srl::SchemaHash h) const override {
            return _root->get_schema_binary(h);
        }
        virtual bool is_schema_stored(srl::SchemaHash h) const override {
            return _root->is_schema_stored(h);
        }
        virtual void add_replicator(Replicator::Connection consumer) override {
            _root->add_replicator(std::move(consumer));
        }
        virtual PStorageTransaction write() override;
            
        virtual std::string_view get_namespace() const override {return {_key.data(), _prefix_size};}
        virtual PStorage get_root_storage() const override {return  _root;}


    protected:
        PStorage _root;
        mutable std::string _key;
        std::size_t _prefix_size;
        std::string_view add_prefix(std::string_view key) const {
            _key.resize(_prefix_size+key.size());
            std::copy(key.begin(), key.end(), _key.begin()+static_cast<std::ptrdiff_t>(_prefix_size));
            return _key;
        }
    };


    class StorageNamespaceTransaction final: public IStorageTransaction {
    public:

        StorageNamespaceTransaction(std::shared_ptr<StorageNamespace> storage, std::string_view key) 
            :_storage(std::move(storage))
            ,_root_tx(_storage->get_root_storage()->write())
            ,_key(key)
            ,_prefix_size(key.size()) {}

        virtual PStorage get_storage() const  override {return _storage;}
        virtual void commit(bool sync) override {_root_tx->commit(sync);}
        virtual RecordKey put(std::string_view variable_name, std::string_view content) override {
            return _root_tx->put(add_prefix(variable_name), content);
        }
        virtual void put(std::string_view variable_name, const RecordKey &key, std::string_view content,
            UpdateLastRevision update_last_revision) override {
                return _root_tx->put(add_prefix(variable_name), key, content, update_last_revision);
            }
        virtual void erase(std::string_view variable_name) override {
            _root_tx->erase(add_prefix(variable_name));
        }
        virtual void erase(std::string_view variable_name, const RecordKey &key) override {
            _root_tx->erase(add_prefix(variable_name), key);
        }
        virtual void put_schema_binary(srl::SchemaHash hash, std::string_view binary) override {
            _root_tx->put_schema_binary(hash, binary);
        }
        virtual void put(const IStorage::ReplicatorEvent &event) override {
            _root_tx->put(event);
        }
    protected:
        std::shared_ptr<StorageNamespace> _storage;
        PStorageTransaction _root_tx;
        std::string _key;
        std::size_t _prefix_size;

        std::string_view add_prefix(std::string_view key)  {
            _key.resize(_prefix_size+key.size());
            std::copy(key.begin(), key.end(), _key.begin()+static_cast<std::ptrdiff_t>(_prefix_size));
            return _key;
        }
    };


    inline PStorageTransaction StorageNamespace::write() {
        return std::make_unique<StorageNamespaceTransaction>(shared_from_this(),std::string_view{_key.data(), _prefix_size});
    }

    inline PStorage IStorage::create_namespace(PStorage root, std::string_view prefix)  {
        PStorage real_root = root->get_root_storage();
        std::string real_prefix (root->get_namespace());
        real_prefix.append(prefix);
        if (!real_root ) real_root = root;
        return std::make_shared<StorageNamespace>(real_root, real_prefix);
    }

}