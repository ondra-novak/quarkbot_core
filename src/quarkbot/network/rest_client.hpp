#pragma once

#include "http_parser.hpp"
#include "quarkbot/utils/string_utils.hpp"
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
namespace network {

template<typename StreamFactory>
class RestClient {
public:
    using Stream = std::decay_t<std::invoke_result_t<StreamFactory> >;
    
    constexpr RestClient(std::string host, std::string path_prefix, StreamFactory factory)
        :_host(std::move(host))
        ,_path_prefix(std::move(path_prefix))
        ,_factory(std::move(factory)) {}
    

    enum class TE {
        ///chunked transfer encoding
        chunked,
        ///content length user
        limited,
        ///read until EOF
        unlimited,
        ///empty body
        empty
    };
    

    struct Response {
        unsigned int code;
        std::string_view message;
        TE _te;
        RestClient *owner;
        
        const HttpParser &get_headers() {
            return owner->_parser;
        }
        std::string_view read_body() {
            switch (_te) {
                case TE::chunked: return owner->read_chunked();
                case TE::limited: return owner->read_limited();
                case TE::unlimited: return owner->read_unlimited();
                default: return {};
            }
        }
        auto read_body_as_charstream() {
            return [this, s = std::string_view()]() mutable ->std::optional<char> {
                if (s.empty()) {
                    s = read_body();
                    if (s.empty()) return std::nullopt;
                }
                char c = s.front();
                s.remove_prefix(1);
                return c;
            };
        }
        
    };

    struct BodyDef {
        //content type is required otherwise body is not sent
        std::string_view type;
        std::string_view content;        
    };

    constexpr void clear_headers() {
        _global_headers.clear();
    }
    
    ///add global headers 
    constexpr void add_header(std::string_view key, std::string_view value) {
        _global_headers.add_header(key, value);
    }

    constexpr void add_headers(std::span<const std::pair<std::string_view, std::string_view> > hdrs) {
        _global_headers.add_headers(hdrs);
    }

    constexpr Response send(std::string_view method, std::string_view path,
            std::span<const std::pair<std::string_view, std::string_view> > headers, 
            BodyDef body = BodyDef{{},{}}) {
        
                
        bool send_body = false;
        
        std::string whole_path(_path_prefix);
        whole_path.append(path);

        _local_headers.start_request(method,whole_path);
        _local_headers.add_header("Host", _host);
        _local_headers.copy_headers(_global_headers);
        if (quarkbot::compare_icase(method, "GET") && quarkbot::compare_icase(method, "HEAD")) {
            send_body = true;
            _local_headers.add_header("Content-Length", body.content.size());
            _local_headers.add_header("Content-Type", body.type);            
        }
        for (auto &[k, v]: headers) {
            _local_headers.add_header(k,v);
        }
        _local_headers.finish();

        if (_cur_stream.has_value() && (!_te || *_te != TE::unlimited)) {
            
            while (_te) {
                switch(*_te) {
                    case TE::chunked: read_chunked();break;
                    case TE::limited: read_limited();break;
                    default: _te.reset();break;
                }
            }

            _cur_stream.value().write(_local_headers.get_result());
            if (send_body) _cur_stream.value().write(body.content);
            auto data = _cur_stream.value().read();
            if (!data.empty()) {
                _cur_stream->put_back(data);
                return get_response();
            }
            _cur_stream.reset();
        }
        _cur_stream = _factory();
        _cur_stream.value().write(_local_headers.get_result());
        if (send_body) _cur_stream.value().write(body.content);
        auto data = _cur_stream.value().read();
        if (!data.empty()) {
            _cur_stream->put_back(data);
            return get_response();
        }
        return bad_response();
    }

    constexpr Response send(std::string_view method, std::string_view path,
        std::initializer_list<std::pair<std::string_view, std::string_view> > headers, BodyDef body = BodyDef{{},{}}) {
            return send(method, path, {headers.begin(), headers.end()}, body);        
    }

    constexpr Response GET(std::string_view path) {
            return send("GET", path, {});        
    }


protected:
    std::string _host;
    std::string _path_prefix;
    StreamFactory _factory;
    HttpBuilder _global_headers;    
    HttpBuilder _local_headers;
    HttpParser _parser;
    std::optional<Stream> _cur_stream;
    std::optional<TE> _te = {};
    
    
    std::size_t _limit;    
    bool _chunk_end = false;
    bool _reading_chunk = false;


    constexpr Response bad_response() {
        _cur_stream.reset();
        return {499,"Bad response", TE::empty, this};
    }    

    constexpr Response get_response() {
        while (!_parser(_cur_stream->read()));
        _cur_stream->put_back(_parser.unprocessed());

        auto te = _parser.header("Transfer-Encoding");
        auto cl = _parser.header("Content-Length");
        if (te) {
            if (compare_icase(te.value(),"chunked") == 0) {
                if (cl) return bad_response();
                return prepare_chunked();
            } if (compare_icase(te.value(),"identity") != 0) {
                return bad_response();
            }
        }
        if (cl) {
            auto sz = _parser.parse_number(cl.value());
            if (sz) {
                return prepare_limited(*sz);
            } else {
                return bad_response();
            }
        }
        return prepare_unlimited();
    }

    constexpr std::string_view read_chunked() {        
        while (true) {
            if (_limit == 0 && !_reading_chunk)  {
                if (_chunk_end) {
                    _te.reset();
                    return {};
                }
                _reading_chunk = true;
            }            std::string_view s = _cur_stream->read();        
            if (s.empty()) {
                _cur_stream.reset();
                return {};
            }

            if (_reading_chunk) {
                std::size_t pos = 0;
                for (auto x: s) {
                    char u = fast_to_upper(x);
                    ++pos;
                    if (u >= '0' && u <='9') _limit = _limit * 16 + static_cast<std::size_t>(u - '0');
                    else if (u >= 'A' && u <='F') _limit = _limit * 16 + static_cast<std::size_t>(u - 'A'+10);
                    else if (u == '\r') continue;
                    else if (u == '\n') {
                        _chunk_end = _limit == 0;
                        _limit+=2;
                        _reading_chunk = false;
                        _cur_stream->put_back(s.substr(pos));
                        break;
                    }
                }        
            } else if (_limit>2) {
                auto ret = s.substr(0,_limit-2);
                _limit -= ret.size();
                _cur_stream->put_back(s.substr(ret.size()));                                
                return ret;
            } else {
                auto sz = std::min(_limit, s.size());
                _limit -= sz;
                _cur_stream->put_back(s.substr(sz));                                                
            }
        }
    }

    constexpr std::string_view read_limited() {
        std::string_view s = _cur_stream->read();
        auto ret = s.substr(0,_limit);
        _limit -= ret.size();
        _cur_stream->put_back(s.substr(ret.size()));
        if (_limit == 0) _te.reset();
        return ret;
    }
    constexpr std::string_view read_unlimited() {
        std::string_view s = _cur_stream->read();
        if (s.empty()) {
            _cur_stream.reset();
            _te.reset();
        }
        return s;
    }
    
    constexpr Response prepare_chunked() {
        _limit = 0;
        _chunk_end = false;
        _te = TE::chunked;

        return {_parser.code(), _parser.message(), TE::chunked, this};
    }
    constexpr Response prepare_limited(std::size_t sz) {
        _limit = sz;
        _te = TE::limited;
        return {_parser.code(), _parser.message(), TE::limited, this};
    }
    constexpr Response prepare_unlimited() {
        _te = TE::unlimited;
        return {_parser.code(), _parser.message(), TE::limited, this};
    }

};


}