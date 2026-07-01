#pragma once

#include <charconv>
#include <string_view>
#include <tuple>
namespace network {

    struct CrackedUrl {
        bool valid = false;                 //record is valid
        std::string_view schema = {};    //contains https, http, wss, ws, etc
        std::string_view host = {};  //contains host without port
        unsigned int port = 0;      //contains port, default 80 for http and 443 for https
        std::string_view path = {};  //contains path with query, default "/"
        bool is_default_port = false; //true if port is default for the schema
    };


    constexpr CrackedUrl crack_url(std::string_view url) {
        CrackedUrl result;
        auto sep = url.find("://");
        if (sep == url.npos) return result;
        result.schema = url.substr(0, sep);
        unsigned def_port;
        if (result.schema == "http" || result.schema == "ws") {
            def_port = 80;
        } else if (result.schema == "https" || result.schema == "wss") {
            def_port = 443;
        } else {
            return result;
        }

         auto url_without_schema = url.substr(sep + 3);
        sep = url_without_schema.find('/');

        std::string_view hostport;
        if (sep == url_without_schema.npos) {
            result.path = "/";
            hostport = url_without_schema   ;
        } else {
            result.path = url_without_schema.substr(sep);
            hostport = url_without_schema.substr(0, sep);
        }
        sep = hostport.find(':');
        if (sep == hostport.npos) {
            result.host = hostport;            
            result.port = def_port;
        } else {
            result.host = hostport.substr(0,sep);
            auto r = std::from_chars(hostport.begin()+sep+1, hostport.end(), result.port, 10);
            if (r.ec != std::errc{}) return result;
        }
        result.is_default_port = result.port == def_port;   
        result.valid = true;
        return result;
}

}