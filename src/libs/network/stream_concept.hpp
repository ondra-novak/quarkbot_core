#pragma once

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
namespace network {

template<typename T>
concept StreamType = requires(T obj, std::string_view data)  {
    /* 
    read is blocking and returns empty string when EOF or timeout
    there is no way to determine which is EOF and which is timeout, 
      but websocket stream always send one ping on first empty response,
      and closes connection on second. EOF should be also detected by returning false during write
      Note: timeout specification also defines ping interval
      MT safety only allows to call write and read in two threads, but no MT safety for single function
    */
    {obj.read()} -> std::convertible_to<std::string_view>;
    /*
    write to stream
    returns false if connection is closed or broken
    write should not be blocking - an output buffer is expected        
      MT safety only allows to call write and read in two threads, but no MT safety for single function
    */
    {obj.write(data)} -> std::convertible_to<bool>;

    /*
    put back data 
    */
    {obj.put_back(data)} ->std::same_as<void>;
};

template<typename StreamType>
class StreamWrapper {
public:
    StreamWrapper(std::unique_ptr<StreamType> stream): _stream(std::move(stream)) {}
    
    StreamType &get() {return *_stream;}
    StreamType *operator->() {return _stream.get();}
    std::string_view read() {return _stream->read();}
    bool write(std::string_view data) {return _stream->write(data); }
    void put_back(std::string_view data) {return _stream->put_back(data);}

protected:
    std::unique_ptr<StreamType> _stream;

};



}