#pragma once

#include "serialize.hpp"
#include "schema_fwd.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace srl {


    ///manages schema hashes and type mapping
    class SchemaHashMapping {
    public:

        ///generate schema hash from type, and write its binary from into DB
        /**
        @tparam type to generate hash
        @param store_cb function called to store hash and binary schema
        @return generated hash
        */
        template<typename T, std::invocable<SchemaHash, std::string> WriteCallback> 
        requires(!std::is_reference_v<T>)
        SchemaHash generate_hash(WriteCallback &&store_cb) {            
            return generate_hash_internal<T, WriteCallback>(&store_cb);
        }   

        ///generate schema hash from type, do not store anything
        /**
        @tparam type to generate hash
        @return generated hash        
        */
        template<typename T> 
        SchemaHash generate_hash() {            
            return generate_hash_internal<T, decltype([](auto,auto){})>(nullptr);
        }   

        
        static SchemaHashMapping instance;

    protected:
        //rember hash and whether data has been stored into database
        std::unordered_map<std::string_view, std::pair<SchemaHash,bool> > _mapping;
        std::mutex _mx;

        template<typename T, std::invocable<SchemaHash, std::string> WriteCallback> 
        requires(!std::is_reference_v<T>)
        SchemaHash generate_hash_internal(WriteCallback *cb) {            
            auto tn = srl::schema_type_name<T>;
            {
                std::lock_guard _(_mx);
                auto iter = _mapping.find(tn);
                if (iter != _mapping.end()) {
                    if (!cb || iter->second.second ) return iter->second.first;
                }
            }    

            srl::BinarySchemaGenerator gen;
            gen.build_schema<T>();
            auto schema = gen.get_schema();
            std::string s;
            srl::BinarySerializer srl([&](std::span<const std::uint8_t> z){
                s.append(reinterpret_cast<const char *>(z.data()), z.size());
            });
            srl(schema);
            std::hash<std::string> hasher;
            SchemaHash h = hasher(s);
            bool stored = cb;
            if (stored) (*cb)(s, h);
            {
                std::lock_guard _(_mx);
                _mapping[tn] = std::pair(h, stored);
            }
            return h;
        }

    };


    inline SchemaHashMapping  SchemaHashMapping::instance = {};

    template<typename T>
    inline std::string serialize_schema() {
        std::vector<std::uint8_t> buff;
        srl::BinarySerializer serializer([&](std::span<const std::uint8_t> data){
            buff.insert(buff.end(), data.begin(), data.end());
        });
        srl::BinarySchemaGenerator sch;
        sch.build_schema<T>();
        serializer(sch.get_schema());
        return {buff.begin(), buff.end()};
    }

    template<typename T>
    inline std::optional<SchemaHash> schema_hash_for_type = {};
    template<typename T>
    inline bool schema_is_stored = false;


    template<typename T>
    inline SchemaHash get_schema_hash() {
        if (!schema_hash_for_type<T>) {
            std::hash<std::string> hasher;
            schema_hash_for_type<T> = schema_hash_for_type<T> = hasher(serialize_schema<T>());
        }
        return schema_hash_for_type<T>.value();
    }


}