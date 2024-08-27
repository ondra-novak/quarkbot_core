#pragma once
#include <filesystem>
#include <string>
#include <map>
#include <sstream>
#include <fstream>
#include <vector>
#include <optional>

class StructuredIni {

    struct ValueData {
        std::string content;
        std::basic_string<int> seps;
        std::shared_ptr<std::filesystem::path> path;
    };

    struct SectionData {
        std::map<std::string, ValueData, std::less<> > key_value;
        std::map<std::string, SectionData, std::less<> > sections;
    };
public:

    static unsigned int tab_size;

    class Exception: public std::exception{};

    class NotFound: public Exception {
    public:
        NotFound(std::string path,std::string key):path(std::move(path)),key(std::move(key)) {}
        const std::string &get_key() const {return key;}
        const std::string &get_path() const {return path;}
        virtual const char *what() const noexcept override {
            if (what_msg.empty()) {
                std::ostringstream buff;
                buff << "Ini key not found: "<< path << " : " <<  key;
                what_msg = std::move(buff.str());
            }
            return what_msg.c_str();
        }

    protected:
        std::string path;
        std::string key;
        mutable std::string what_msg;
    };

    class OpenError: public Exception {
    public:
        OpenError(std::filesystem::path f):_f(f) {}
        const std::filesystem::path &get_path() const {return _f;}
        virtual const char *what() const noexcept override {
            if (what_msg.empty()) {
                std::ostringstream buff;
                buff << "Failed to open INI config: " << _f << std::endl;
                what_msg = std::move(buff.str());
            }
            return what_msg.c_str();
        }
    protected:
        std::filesystem::path _f;
        mutable std::string what_msg;
    };


    class Value {
    public:

        std::size_t size() const {return _val.seps.size()+1;};
        std::size_t count() const {return size();}
        std::string_view operator[](std::size_t idx) const {
            std::size_t beg = idx?_val.seps[idx-1]:0;
            std::size_t end = idx>=_val.seps.size()?_val.content.size():_val.seps[idx]-1;
            return std::string_view(_val.content).substr(beg, end-beg);
        }

        operator std::string_view() const {return _val.content;}
        operator std::string() const {return _val.content;}
        operator std::filesystem::path() const {
            if (_val.path) return (*_val.path)/_val.content;
            else return _val.content;
        }

        class Iterator { // @suppress("Miss copy constructor or assignment operator")
        public:
            using iterator_category = std::random_access_iterator_tag;
            using value_type = std::string_view;

            Iterator(const Value &cont, std::size_t index):_cont(&cont),_index(index) {}
            bool operator==(const Iterator &) const = default;
            std::strong_ordering operator<=>(const Iterator &) const = default;
            value_type operator *() const {return (*_cont)[_index];}
            std::filesystem::path path() const {
                if (_cont->_val.path) return (*_cont->_val.path)/(*_cont)[_index];
                else return (*_cont)[_index];
            }
            friend Iterator operator+(const Iterator &a, std::ptrdiff_t ofs) {return Iterator(*a._cont, a._index+ofs);}
            friend Iterator operator-(const Iterator &a, std::ptrdiff_t ofs) {return Iterator(*a._cont, a._index-ofs);}
            friend std::ptrdiff_t operator-(const Iterator &a, const Iterator &b) {return b._index - a._index;}
            Iterator &operator++() {++_index;return *this;}
            Iterator &operator--() {--_index;return *this;}
            Iterator operator++(int) {auto tmp = *this; ++_index;return tmp;}
            Iterator operator--(int) {auto tmp = *this; --_index;return tmp;}
            Iterator &operator+=(std::ptrdiff_t ofs) {_index+=ofs;return *this;}
            Iterator &operator-=(std::ptrdiff_t ofs) {_index-=ofs;return *this;}

        protected:
            const Value *_cont = {};
            std::size_t _index = {};
        };

        Iterator begin() const {return Iterator(*this,0);}
        Iterator end() const {return Iterator(*this,size());}


        Value(const ValueData &val):_val(val) {}
    protected:
        const ValueData &_val;
    };

    class Section {
    public:

        Section(const SectionData *s, std::string_view prev, std::string_view name)
            :s(s),path(prev) {
            path.push_back('[');
            path.append(name);
            path.push_back(']');
        }
        Section(const SectionData *s)
            :s(s) {}
        Section(const Section &) = default;
        Section &operator=(const Section &) = default;


        Value get(std::string_view key) const {
            auto iter = s->key_value.find(key);
            if (iter == s->key_value.end()) {
                throw NotFound(path, std::string(key));
            }
            return iter->second;
        }

        std::optional<Value> get_optional(std::string_view key) const {
            auto iter = s->key_value.find(key);
            if (iter == s->key_value.end()) {
                throw std::nullopt;
            }
            return Value(iter->second);
        }


        Section section(std::string_view name) const {
            static SectionData empty_section = {};
            auto iter = s->sections.find(name);
            if (iter == s->sections.end()) {
                return {&undefinedSectionData(), path, name};
            } else {
                return {&iter->second, path, name};
            }
        }

        bool defined() const {
            return s != &undefinedSectionData();
        }


        Value operator[](std::string_view key) const {return get(key);}

        const auto &sections() const {return s->sections;}
        auto begin() const {
            return s->key_value.begin();
        }
        auto end() const {
            return s->key_value.end();
        }

    protected:
        const SectionData *s = {};
        std::string path = {};

        static const SectionData &undefinedSectionData() {
            static SectionData empty_section = {};
            return empty_section;
        }
};


    Section root() const {
        return {&sect, "", ""};
    }

    void clear() {
        sect.key_value.clear();
        sect.sections.clear();
    }

    struct ExternalRef { // @suppress("Miss copy constructor or assignment operator")
        std::string name;
        std::shared_ptr<std::filesystem::path> base_path;
        SectionData *root;
    };

    void parse(std::istream &in, std::vector<ExternalRef> &&extref = {}) {
        clear();
        return parse(in, &sect, {}, std::move(extref));
    }

    void parse(std::istream &in, std::filesystem::path base_path, std::vector<ExternalRef> &&extref = {}) {
        clear();
        return parse(in, &sect, std::make_shared<std::filesystem::path>(std::move(base_path)), std::move(extref));
    }


    static void parse(std::istream &in, SectionData *root, std::shared_ptr<std::filesystem::path> path, std::vector<ExternalRef> &&extref = {}) {

        std::vector<std::pair<int, SectionData *> > padding_table = {{-1,root}};
        std::string line;
        std::string extra_line;
        std::string_view lineview;
        ValueData *vd = nullptr;
        while (!in.eof()) {
            std::getline(in, line);
            lineview = remove_comment(line);
            auto padding = remove_padding(lineview);
            if (lineview.empty()) continue;
            while (lineview.back() == '\\') {
                lineview = lineview.substr(0, lineview.size()-1);
                if (!in.eof()) {
                    std::getline(in,extra_line);
                    auto w = remove_comment(extra_line);
                    remove_padding(w);
                    line = lineview;
                    line.append(w);
                    lineview = line;
                }
            }
            char first_char = lineview.front();
            char last_char = lineview.back();
            if (first_char == '[' && last_char == ']') {
                vd = nullptr;
                while (padding_table.back().first >= padding) padding_table.pop_back();
                SectionData *cur = padding_table.back().second;
                std::string_view section_name = lineview.substr(1, lineview.size()-2);
                SectionData *new_sect = &cur->sections[std::string(section_name)];
                padding_table.push_back({padding, new_sect});
            } else if (first_char == '{' && last_char == '}') {
                vd = nullptr;
                while (padding_table.back().first > padding) padding_table.pop_back();
                SectionData *cur = padding_table.back().second;
                std::string_view ext_name = lineview.substr(1, lineview.size()-2);
                extref.push_back({std::string(ext_name), path, cur});
            } else {
                while (padding_table.back().first > padding) padding_table.pop_back();
                SectionData *cur = padding_table.back().second;
                std::string key;
                std::string value;
                auto st = read_key_or_value<true>(lineview.begin(), lineview.end(), std::back_inserter(key));
                if (st.first != lineview.end()) {
                    if (*st.first == '=') {
                        ++st.first;
                        st = read_key_or_value<false>(st.first, lineview.end(), std::back_inserter(value));
                        auto &item = cur->key_value[std::move(key)];
                        item.content = std::move(value);
                        item.path = path;
                        vd = &item;
                    } else if (vd) {
                        append(key, vd);
                    }
                    while (vd && st.first != lineview.end()) {
                        ++st.first;
                        while (st.first != lineview.end() && std::isspace(*st.first)) ++st.first;
                        if (st.first != lineview.end()) {
                            value.clear();
                            st = read_key_or_value<false>(st.first, lineview.end(), std::back_inserter(value));
                            append(value, vd);
                        }
                    }
                } else {
                    append(key, vd);
                }

            }
        }
    }

    class DefaultOpenFile {
        std::optional<std::ifstream> f;
    public:
        std::istream &operator()(const std::filesystem::path &p) {
            f.reset();
            f.emplace(p);
            if (f->operator !()) {
                throw OpenError(p);
            }
            return *f;
        }
    };

    template<std::invocable<std::filesystem::path> FileManager>
    void parse_file(std::filesystem::path file, FileManager &&fman, unsigned int max_ext_ref = 1024) {
        std::vector<ExternalRef> extref;
        auto idx = extref.size();
        extref.push_back(ExternalRef{file.filename(), std::make_shared<std::filesystem::path>(file.parent_path()), &sect});
        while (idx <= max_ext_ref && idx < extref.size()) {
            const ExternalRef &e = extref[idx];
            ++idx;
            auto p = (*e.base_path)/e.name;
            std::istream &inf = fman(p);
            parse(inf,e.root,std::make_shared<std::filesystem::path>(p.parent_path()),std::move(extref));
        }
    }

    void parse_file(std::filesystem::path file, unsigned int max_ext_ref = 1024) {
        parse_file(std::move(file), DefaultOpenFile(), max_ext_ref);
    }


protected:

    SectionData sect;

    static  std::string_view remove_comment(std::string_view s) {
        bool quotes = false;
        bool escape = false;
        std::size_t cnt = s.size();
        for(std::size_t i = 0; i < cnt; ++i) {
            char c = s[i];
            if (c == '\\') escape = true;
            else if (escape) escape = false;
            else if (c == '"') quotes = !quotes;
            else if (c == ';' || c == '#') return s.substr(0, i);
        }
        return s;
    }
    static int remove_padding(std::string_view &s) {
        int count = 0;
        while (!s.empty()) {
            char c = s.front();
            if (c == '\t') {
                count = ((count+tab_size)/tab_size)*tab_size;
            } else if (c == ' ') {
                ++count;
            } else break;
            s = s.substr(1);
        }
        while (!s.empty() && std::isspace(s.back())) {
            s = s.substr(0,s.size()-1);
        }
        return count;
    }

    template<bool key, typename In, typename Out>
    static std::pair<In,Out> read_key_or_value(In iter, In end, Out out) {
        while (iter != end && std::isspace(*iter)) {
            ++iter;
        }
        if (iter != end) {
            if (*iter == '"') {
                ++iter;
                while (iter != end && *iter != '"') {
                    if (*iter == '\\') {
                        ++iter;
                        if (iter != end) {
                            switch (*iter) {
                                case 'r': *out = '\r'; break;
                                case 'n': *out = '\n'; break;
                                case 't': *out = '\t'; break;
                                case 'b': *out = '\b'; break;
                                case 'a': *out = '\a'; break;
                                case 'f': *out = '\f'; break;
                                case 'x':  {
                                        char num[3];
                                        ++iter;
                                        if (iter != end) {
                                            num[0] = *iter;
                                            ++iter;
                                        }
                                        if (iter != end) {
                                            num[1] = *iter;
                                            ++iter;
                                        }
                                        num[2] = 0;
                                        auto code = strtol(num, nullptr, 16);
                                        *out = static_cast<char>(code);
                                        ++out;
                                        continue;
                                }
                                default:
                                        *out = *iter;break;
                            }
                            ++iter;
                            ++out;
                        }
                    } else {
                        *out = *iter;
                        ++out;
                        ++iter;
                    }

                }
                if (iter != end) ++iter;
            } else {
                while (iter != end && std::isspace(*iter)) {
                    ++iter;
                }
            }
        }
        std::string spaces;
        while (iter != end) {
            if (*iter == ',' || (key && *iter == '=')) break;
            if (std::isspace(*iter)) {
                spaces.push_back(*iter);
                ++iter;
            } else {
                out = std::copy(spaces.begin(), spaces.end(), out);
                spaces.clear();
                *out = *iter;
                ++out;
                ++iter;
            }
        }
        return {iter, out};
    }



    static void append(const std::string &value, ValueData *vd) {
        vd->content.push_back(',');
        vd->seps.push_back(static_cast<int>(vd->content.size()));
        vd->content.append(value);
    }
};

inline unsigned int StructuredIni::tab_size = 8;
