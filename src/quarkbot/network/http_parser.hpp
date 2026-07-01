#pragma once

#include "string_utils.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
namespace network {


    class HttpParser {
    public:

        constexpr bool operator()(std::string_view text) {
            if (_complete || text.empty()) {
                _hdr_data.clear();
                _headers.clear();
                _unprocessed ={};
                _hdrend_index = 0;
                _complete = false;
            }
            std::size_t pos = 0;
            for (const char c: text) {
                _hdr_data.push_back(c);
                ++pos;
                if (hdrend[_hdrend_index] == c) {
                    ++_hdrend_index;
                    if (_hdrend_index == hdrend.size()) {
                        _complete = true;
                        _hdr_data.resize(_hdr_data.size()-hdrend.size());
                        _unprocessed = text.substr(pos);
                        parse_header();
                        return true;
                    }
                } else {
                    _hdrend_index = 0;
                }                            
            }
            return false;
        }


        constexpr auto method() const {return _first_line_parts[0];}
        constexpr auto path() const {return _first_line_parts[1];}
        constexpr unsigned int code() const {
            return static_cast<unsigned int>(parse_number(_first_line_parts[1]).value_or(0));
        }
        constexpr auto message() const {
            return _first_line_parts[2];
        }
        constexpr auto version_request() const {
            return _first_line_parts[2];
        }
        constexpr auto version_response() const {
            return _first_line_parts[0];
        }
        constexpr auto header(std::string_view key) {
            std::optional<std::string_view> value;
            auto iter = std::lower_bound(_headers.begin(), _headers.end(), std::pair(key, std::string_view()), compare_keys);
            if (iter != _headers.end() && quarkbot::compare_icase(iter->first, key) == 0) {
                value = iter->second;
            }
            return value;
        }

        constexpr auto lower_bound(std::string_view key) const {
            return std::lower_bound(_headers.begin(), _headers.end(), std::pair(key, std::string_view()), compare_keys);
        }

        constexpr  auto upper_bound(std::string_view key) const {
            return std::upper_bound(_headers.begin(), _headers.end(), std::pair(key, std::string_view()), compare_keys);
        }
        constexpr auto begin() const {
            return _headers.begin();
        }
        constexpr auto end() const {
            return _headers.end();
        }
        constexpr static std::optional<std::size_t> parse_number(std::string_view text) {
            std::size_t st = 0;
            for (const char c: text) {
                if (c < '0' || c > '9') return {};
                st = st * 10 + static_cast<unsigned int>(c - '0');
            }
            return st;
        }
        constexpr auto unprocessed() const {return _unprocessed;}

    protected:
        static constexpr std::string_view hdrend = "\r\n\r\n";

        std::vector<char> _hdr_data;
        std::uint8_t _hdrend_index = 0;
        std::vector<std::pair<std::string_view, std::string_view> > _headers;
        std::array<std::string_view, 3> _first_line_parts = {};
        std::string_view _unprocessed;
        bool _complete = false; 

        constexpr void parse_header() {
            auto hdrtext = std::string_view{_hdr_data.data(), _hdr_data.size()};
            std::string_view first_line = split(hdrtext, "\r\n");            
            do {
                std::string_view ln = split(hdrtext, "\r\n");
                std::string_view key = trim(split(ln, ":"));
                std::string_view value =trim(ln);
                _headers.push_back({key,value});
            } while (!hdrtext.empty());
            _first_line_parts[0] = trim(split(first_line," "));
            _first_line_parts[1] = trim(split(first_line," "));
            _first_line_parts[2] = trim(first_line);
            std::stable_sort(_headers.begin(), _headers.end(), compare_keys);
        }

        constexpr static bool compare_keys(const std::pair<std::string_view,std::string_view> &a, const std::pair<std::string_view,std::string_view> &b) {
            return quarkbot::compare_icase(a.first, b.first) < 0;
        }
    };


    class HttpBuilder {
    public:
        constexpr void start_request(std::string_view method, std::string_view uri, std::string_view version = "HTTP/1.1") {
            hdrs.clear();
            append_text(method);
            hdrs.push_back(' ');
            append_text(uri);
            hdrs.push_back(' ');
            append_text(version);
            append_nl();
            _first_line_len = hdrs.size();
        }

        constexpr void start_response(unsigned int code, std::string_view message, std::string_view version = "HTTP/1.1") {
            hdrs.clear();
            append_text(version);
            hdrs.push_back(' ');
            append_number(code, 1);            
            hdrs.push_back(' ');
            append_text(message);
            append_nl();
            _first_line_len = hdrs.size();
        }

        constexpr void add_header(std::string_view key, std::string_view value) {
            append_text(key);
            append_text(": ");
            append_text(value);            
            append_nl();
        }

        constexpr void add_headers(std::span<const std::pair<std::string_view, std::string_view> > hdrs) {
            for (auto &[k,v]: hdrs) add_header(k,v);
        }
    

        constexpr void add_header(std::string_view key, std::size_t value) {
            append_text(key);
            append_text(": ");
            append_number(value, 1);
            append_nl();
        }

        constexpr void finish() {
            append_nl();
        }

        constexpr auto copy_headers(const HttpBuilder &from) {
            hdrs.insert(hdrs.end(), from.hdrs.begin() + static_cast<std::ptrdiff_t>(from._first_line_len), from.hdrs.end());
        }


        constexpr std::string_view get_result() const {
            return {hdrs.data(), hdrs.size()};
        }

        constexpr operator std::string_view() const {return get_result();}

        void clear() {
            hdrs.clear();
            _first_line_len = 0;
        }


    protected:
        std::vector<char> hdrs;
        std::size_t _first_line_len = 0;
        

        constexpr void append_number(std::size_t x, int lz) {
            if (x || lz > 0) {
                append_number(x/10, lz-1);
                hdrs.push_back(static_cast<char>((x%10)+'0'));
            }
        }        
        
        constexpr void append_nl() {
            hdrs.push_back('\r');
            hdrs.push_back('\n');
        }

        constexpr void append_text(std::string_view x) {
            hdrs.insert(hdrs.end(), x.begin(), x.end());
        }
        
    };


}