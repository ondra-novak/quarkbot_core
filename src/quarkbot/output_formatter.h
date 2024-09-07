#pragma once
#include "common.h"
#include <string_view>
#include <bitset>
#include <vector>

namespace quarkbot {

class OutpuFormatter {
public:

    // Vlastní buffer pro zápis do std::vector<char>
    class vector_streambuf : public std::streambuf {
    public:
        vector_streambuf(std::vector<char>& vec) : vec_(vec) {}

    protected:
        // Přetížená metoda pro zápis jednoho znaku
        virtual int_type overflow(int_type ch) override {
            if (ch != traits_type::eof()) {
                vec_.push_back(static_cast<char>(ch));
                return ch;
            } else {
                return traits_type::eof();
            }
        }

        // Přetížená metoda pro zápis více znaků
        virtual std::streamsize xsputn(const char* s, std::streamsize count) override {
            vec_.insert(vec_.end(), s, s + count);
            return count;
        }

    private:
        std::vector<char>& vec_;
    };


    template<typename ... Args>
    void format(const std::string_view &pattern, Args && ... args) {
        vector_streambuf buffer(_buffer);
        std::ostream os(&buffer);
        format(os, pattern, std::forward<Args>(args)...);
    }

    void clear_buffer() {
        _buffer.clear();
    }
    void resize_buffer(std::size_t sz) {
        _buffer.resize(sz);
    }

    void push_back(char c) {
        _buffer.push_back(c);
    }
    std::string_view get_buffer() const {
        return {_buffer.begin(), _buffer.end()};
    }
    auto size() const {
        return _buffer.size();
    }


protected:
    std::vector<char> _buffer;

    template<typename T>
    void format_item(std::ostream &out, const T &val) {
        if constexpr(can_output_to_ostream<T>) {
            out << val;
        } else if constexpr(std::is_invocable_v<T>) {
            format_item(out,val());
        } else if constexpr(has_to_string_global<T>) {
            format_item(out,to_string(val));
        } else if constexpr(std::is_same_v<T, std::exception_ptr>) {
            try {
                std::rethrow_exception(val);
            } catch (std::exception &e) {
                format_item(out, e.what());
            } catch (...) {
                format_item(out, "<unknown exception>");
            }
        } else if constexpr(is_container<T>) {
            out << '(';
            auto beg = val.begin();
            auto end = val.end();
            if (beg != end) {
                format_item(out,*beg);
                ++beg;
                while (beg != end) {
                    out << ',';
                    format_item(out,*beg);
                    ++beg;
                }
            }
            out << ')';
        } else if constexpr(is_pair_type<T>) {
            _buffer.push_back('<');
            format_item(val.first);
            _buffer.push_back(':');
            format_item(val.second);
            _buffer.push_back('>');
        } else {
            static_assert(assert_error<T>, "Cannot print type, define operator << ");
        }
    }

    template<typename ... Args>
    void format(std::ostream &out, const std::string_view &pattern, Args && ... args) {
        std::bitset<64> mask={};
        auto iter = pattern.begin();
        auto end = pattern.end();
        unsigned int cur_index = 0;
        while (iter != end) {
            if (*iter == '{') {
                unsigned int index = 0;
                ++iter;
                if (iter != end && *iter == '{') {
                    out << '{';
                    ++iter;
                    continue;
                }
                while (iter != end && *iter >='0' && *iter <='9') {
                    index = index * 10 +  (*iter-'0');
                }
                if (iter != end && *iter == '}') ++iter;
                if (index == 0) {
                    ++cur_index;
                    while (cur_index < mask.size() && mask[cur_index-1]) ++cur_index;
                    index = cur_index;
                } else {
                    mask.set(index-1);
                }
                format_nth_item(index-1, out, std::forward<Args>(args)...);
            } else {
                out.put(*iter);
                ++iter;
            }
        }
    }

    template<typename T, typename ... Args>
    void format_nth_item(unsigned int index, std::ostream &out, T && val, Args && ... args) {
        if (index) {
            format_nth_item(index-1, out, std::forward<Args>(args)...);
        } else {
            format_item(out, val);
        }
    }
    void format_nth_item(unsigned int index, std::ostream &out) {
        out << "{" << index << "}";
    }


};


}
