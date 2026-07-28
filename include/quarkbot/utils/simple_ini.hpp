

#include <concepts>
#include <istream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

/// Simple INI file reader
/**
    * @tparam Source The type of the source function that provides the INI file content.
    * The source function must be invocable with no arguments and return a std::string_view containing the next block of text from the INI file.
    * The source function should return an empty string_view when there is no more data to read.
    *
    * @tparam need_comments A boolean template parameter that indicates whether to include comments in the output rows.
    * If true, the output rows will include comments; if false, comments will be ignored.
    *
    * The IniReader class reads an INI file from a given source and provides an interface to iterate over its rows.
    * Each row consists of a section name, a key, and a value. If need_comments is true, comments are also included in the output rows.
    *
    * Example usage:
    * @code
    * auto source = []() -> std::string_view { / implementation to read from a file or string / };
    * IniReader<decltype(source), true> reader(source);
    * IniReader<decltype(source), true>::Row row;
    * while (reader.next(row)) {
    *     // Process row.section, row.key, row.value, and row.comment (if need_comments is true)
    * }
    * @endcode
    *   
*/
template<std::invocable<> Source, bool need_comments = false>
class IniReader {
public:
    using SrcRes = std::invoke_result_t<Source>;
    static_assert(std::is_convertible_v<SrcRes, std::string_view>);

    struct Row_Without_Comments {
        ///current section name, empty if no section defined yet
        std::string_view section;
        ///key name
        std::string_view key;
        ///value
        std::string_view value;
    };
    struct Row_With_Comments {
        ///current section name, empty if no section defined yet
        std::string_view section;
        ///key name
        std::string_view key;
        ///value
        std::string_view value;
        ///comment, empty if no comment
        std::string_view comment = {};
    };
    using Row = std::conditional_t<need_comments, Row_With_Comments, Row_Without_Comments>;

    ///Construct the reader with given source
    constexpr IniReader(Source source): _source(std::move(source)) {}

    ///Get next row from the INI file
    /**
        * @param row Reference to a Row object that will be filled with the next row's data.
        * @return true if a row was successfully read; false if there are no more rows to read.
        * The row will contain the section name, key, and value. If need_comments is true, the
        * row will also contain any comments associated with the row.
        */
    constexpr bool next(Row &row) {
        while (true) {
            auto ln = next_line();
            if (!ln) return false;
            if (ln->empty()) continue;
            if (ln->starts_with('#') || ln->starts_with(';')) {
                if constexpr(need_comments) {
                    row = Row{_current_section, {} , {}, ln};
                    return true;                    
                } else {
                    continue;
                }
            }
            if (ln->starts_with('[') && ln->ends_with(']')) {
                _current_section = std::string(ln->substr(1, ln->length()-2));
                continue;;
            } 
            auto sep = ln->find('=');
            if (sep == ln->npos) {
                row = Row(_current_section, *ln, {});
                return true;
            } 
            auto key = trim(ln->substr(0,sep));
            auto value = trim(ln->substr(sep+1));
            row = Row(_current_section, key, value);
            return true;
        }
    }

    ///Create a key-value map from the INI file
    /**
        * @param sect_sep Character used to separate section names from keys in the output map.
        * @return A vector of pairs, where each pair consists of a key and its corresponding value.
        * The key is formed by concatenating the section name and the key name, separated
            by the specified sect_sep character. If need_comments is true, comments are ignored in the output map.
            */
    std::vector<std::pair<std::string, std::string> > create_kv_map(char sect_sep = '#') {
        Row rw;
        std::vector<std::pair<std::string, std::string> > out;
        while (next(rw)) {
            if constexpr (need_comments) {
                if (!rw.comment.empty()) continue;
            }
            std::string key;
            key.reserve(rw.section.size()+rw.key.size()+1);
            if (!rw.section.empty()) {
                key.append(rw.section);
                key.push_back(sect_sep);
            }
            key.append(rw.key);
            out.emplace_back(std::move(key), std::string(rw.value));
        }
        return out;
    }

protected:
    Source _source;
    std::vector<char> _buffer;
    std::optional<SrcRes> _srcres;
    std::string_view _input;
    std::string _current_section;
    bool _eof = false;

    int next_char() {
        if (_input.empty()) {            
            _srcres.emplace(_source());
            _input = *_srcres;
            if (_input.empty()) return -1;
        }
        char c = _input.front();
        _input.remove_prefix(1);
        return static_cast<int>(static_cast<unsigned char>(c));
    }

    constexpr std::optional<std::string_view> next_line()  {        
        _buffer.clear();
        if (_eof) return {};
        int c = next_char();
        while (c != -1 && c != '\r' && c != '\n') {
            _buffer.push_back(static_cast<char>(c));
            c = next_char();
        }
        if (c == -1) {
            _eof = true;
            if (_buffer.empty()) return {};            
        }
        return trim({_buffer.begin(),_buffer.end()});
    }

    constexpr static std::string_view trim(std::string_view txt) {
        while (!txt.empty() && std::isspace(txt.front())) txt.remove_prefix(1);
        while (!txt.empty() && std::isspace(txt.back())) txt.remove_suffix(1);
        return txt;
    }
};

namespace _details {

class IniReaderFromStream_Source {
    std::istream &s;
    char buffer[4096];
public:
    IniReaderFromStream_Source(std::istream &s):s(s) {}
    std::string_view operator()() {
        if (!s) return {};
        s.read(buffer, sizeof(buffer));
        return {buffer, static_cast<std::size_t>(s.gcount())};
    }
};

}
using IniReaderFromStream = IniReader<_details::IniReaderFromStream_Source>;