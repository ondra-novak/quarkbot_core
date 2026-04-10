#pragma once

#include <basic_coro/awaitable.hpp>
#include "defs.hpp"
#include "market_instrument.hpp"

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace quarkbot {


    class IStorageTransaction;
    using PStorageTransaction =  std::unique_ptr<IStorageTransaction> ;

    ///Interface for storage of strategy data. 
    /* Storage is used to store strategy state, parameters, 
       or any other data that needs to be preserved between runs.    
    */
    class IStorage {
    public:

        ///revision of value, used for sequence keys
        using Revision = std::size_t;

        ///Key for storage value
        struct Key {
            ///key name - must be unique for strategy
            std::string_view name;
            ///if true, storage keeps history of values and allows to retrieve value by revision. If false, only last value is kept and revision is ignored
            bool sequence;

            Key(std::string_view name, bool sequence = true):name(name),sequence(sequence) {}
        };


        ///value stored in storage
        struct Value {
            ///Revision of value, if key is sequence. For non sequence keys, revision is always 0
            Revision rev;
            ///true if value exists, false if key is not found or it is tombstone (deleted value)
            bool exists;
            ///data blob
            std::string data;
            ///return true if value exists, false if it is tombstone or key is not found
            operator bool() const {return exists;}
        };
        
        virtual ~IStorage() = default;
        ///returns last known value for key. If key is not found, or it is deleted, returned value has exists=false
        virtual Value get(Key key) const = 0;
        ///returns specified revision of value for key. If key is not found, or it is deleted, returned value has exists=false
        virtual Value get(Key key, Revision rev) const = 0;
        ///returns all keys in storage
        /**
            @param filter key filter to apply - you can specify whether you want sequence or non sequence keys, or filter by name prefix. 
            @return vector of all keys matching the filter
         */
        virtual std::vector<std::string> get_all_keys(const Key &filter) const = 0;        
        ///creates transaction for writing values. Transaction is used to write multiple values atomically. Transaction must be commited by calling commit() method. If transaction is not commited, all changes are discarded
        virtual PStorageTransaction write() = 0;

    };


    class IStorageTransaction {
    public:
        using Key = IStorage::Key;
        using Revision = IStorage::Revision;

        ///put value to storage. 
        /** If key is sequence, value is added as new revision, and revision number is automatically incremented. 
            If key is not sequence, value is overwritten and revision is set to 0
            @param key key to put value for
            @param value_blob data blob to store
            @return revision of stored value. For sequence keys, it is automatically incremented. For non sequence keys, it is always 0
        */
        virtual Revision put(Key key, std::string_view value_blob) = 0;
        ///erase value for key. For sequence keys, it adds tombstone value with next revision. For non sequence keys, it deletes value and sets revision to 0
        /**
            @param key key to erase
            @return revision of tombstone value for sequence keys, or 0 for non sequence keys
         */        
        virtual Revision erase(Key key) = 0;
        ///prune history of key. For sequence keys, it removes all revisions from "from" to "to" (inclusive). For non sequence keys, it does nothing
        /**
            @param key key to prune history for
            @param from starting revision to prune
            @param to ending revision to prune
            @note always keeps last revision, even if it is in range. This means that if "to" is greater than last revision, it is set to last revision
         */
        virtual void prune_history(Key key, Revision from, Revision to) = 0;
        ///commit transaction. After commit, all changes are applied to storage. If transaction is not commited, all changes are discarded
        /**
          You should drop transaction object after commit, or if you don't want to commit, to discard changes.
          The state of the transaction after commit is undefined, and should not be used.
         */
        virtual void commit() = 0;

        virtual ~IStorageTransaction() = default;
    };

};