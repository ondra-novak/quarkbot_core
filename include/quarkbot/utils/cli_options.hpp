#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <format>

#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>
namespace cli {

using UInt = std::size_t;
using SInt = std::ptrdiff_t;

template<typename T>
using OptionParameterTarget = std::variant<
    std::monostate,
    std::string T::*,
    std::wstring T::*,
    bool T::*,
    int T::*,
    unsigned int T::*,
    UInt T::*,
    SInt T::*,
    double T::*,
    float T::*,
    std::filesystem::path T::*,

    std::optional<std::string> T::*,
    std::optional<UInt> T::*,
    std::optional<SInt> T::*,
    std::optional<double> T::*,
    std::optional<float> T::*,
    std::optional<std::filesystem::path> T::*,

    std::vector<std::filesystem::path> T::*
>;

template<typename T>
constexpr bool is_multiple_path_option(const OptionParameterTarget<T> &opt) {
    return std::holds_alternative<std::vector<std::filesystem::path> T::*>(opt);
}

template<typename T>
constexpr bool has_option_argument(const OptionParameterTarget<T> &opt) {
    return !std::holds_alternative<bool T::*>(opt);
}

template<typename T>
struct OptionDef {
    OptionParameterTarget<T> target;
    ///sort flag - if zero, not defined (if no flag defined, position is used) 
    char sopt = 0;
    ///long flag - if empty, not defined (if no flag defined, position is used)
    std::string_view lopt = {};
    ///name of argumen - if not defined, generic name depend on type will be used <string> <uint> <int> <number> <path>. Boolean has no argument
    std::string_view arg_name = {};
    ///description of flag
    std::string_view description = {};
    ///true of mandatory (must be specified)
    bool mandatory = false;
};

enum class ParseErrorType {
    no_error,
    unknown_option,
    conversion_error,
    extra_content,
    requires_argument,
    option_is_mandatory,
    argument_is_mandatory
};

constexpr std::string_view to_string(ParseErrorType e) {
    switch (e) {
        case ParseErrorType::no_error: return "no error";
        case ParseErrorType::unknown_option: return "Unknown option";
        case ParseErrorType::conversion_error: return "Failed to convert option (check type)";
        case ParseErrorType::extra_content: return "Extra content on command line";
        case ParseErrorType::requires_argument: return "Requires an argument";
        case ParseErrorType::option_is_mandatory: return "Option is mandatory but missing";
        case ParseErrorType::argument_is_mandatory: return "Missing mandatory argument";
    };
    return "Undefined error";
}



template<typename T> inline constexpr std::string_view argument_name = "arg";
template<> inline constexpr std::string_view argument_name<std::string> = "string";
template<> inline constexpr std::string_view argument_name<std::wstring> = "string";
template<> inline constexpr std::string_view argument_name<bool> = "";
template<> inline constexpr std::string_view argument_name<int> = "int";
template<> inline constexpr std::string_view argument_name<uint> = "uint";
template<> inline constexpr std::string_view argument_name<SInt> = "int";
template<> inline constexpr std::string_view argument_name<UInt> = "uint";
template<> inline constexpr std::string_view argument_name<float> = "num";
template<> inline constexpr std::string_view argument_name<double> = "num";
template<> inline constexpr std::string_view argument_name<std::filesystem::path> = "path";
template<> inline constexpr std::string_view argument_name<std::optional<std::string>> = "string";
template<> inline constexpr std::string_view argument_name<std::optional<std::wstring>> = "string";
template<> inline constexpr std::string_view argument_name<std::optional<int>> = "int";
template<> inline constexpr std::string_view argument_name<std::optional<uint>> = "uint";
template<> inline constexpr std::string_view argument_name<std::optional<SInt>> = "int";
template<> inline constexpr std::string_view argument_name<std::optional<UInt>> = "uint";
template<> inline constexpr std::string_view argument_name<std::optional<float>> = "num";
template<> inline constexpr std::string_view argument_name<std::optional<double>> = "num";
template<> inline constexpr std::string_view argument_name<std::optional<std::filesystem::path>> = "path";
template<> inline constexpr std::string_view argument_name<std::vector<std::filesystem::path> > = "paths...";

template<typename M>
struct member_traits;

template<typename T, typename U>
struct member_traits<T U::*>
{
    using class_type = U;
    using value_type = T;
};

/*
template<typename T, typename U>
struct member_traits<std::optional<T U::*> >: member_traits<T U::*> {};
*/


template<typename _Target>
class CLIParser {
public:

    template<typename _CharType>
    struct Error {
        ParseErrorType errtype;
        std::basic_string_view<_CharType> subject;
        std::ptrdiff_t nth_argument;
        std::ptrdiff_t nth_option;
    };

    using Def = OptionDef<_Target>;

    template<typename _CharType>
    using Result = std::optional<Error<_CharType> >;
    
    constexpr CLIParser(std::span<const Def> def):_defs(def) {}
    CLIParser(std::span<const Def> def, std::filesystem::path root_path):_defs(def),_root_path(root_path)  {}  

    template<typename _CharType>
    constexpr Result<_CharType> parse_options(std::span<const std::basic_string_view<_CharType> > options, _Target &target) const {
        using StrV = std::basic_string_view<_CharType>;
        using SResult = Result<_CharType>;

        std::basic_string<bool> used(_defs.size(), false);    //use string for small memory footprint
        auto next_positional = _defs.begin();
        bool adv_positional = false;
        
        _CharType separators[2] = {static_cast<_CharType>(':'),static_cast<_CharType>('=')};
        _CharType flag_prefix = static_cast<_CharType>('-');
        StrV str_separators(separators,2);
        StrV str_flag_prefix(&flag_prefix, 1);
        std::ptrdiff_t nth_arg = 0;
        std::ptrdiff_t nth_opt = -1;

        
        auto iter = options.begin();
        auto itbeg = iter;
        auto itend = options.end();

        auto mark_used = [&](std::span<const OptionDef<_Target> >::const_iterator iter) {
            nth_opt = std::distance(_defs.begin(), iter);
            auto pos =static_cast<std::size_t>(nth_opt);
            used[pos] =true;
        };



        auto positional_argument = [&](std::basic_string_view<_CharType> str) -> SResult {
            while (next_positional != _defs.end()) {
                if (next_positional->sopt == 0 && next_positional->lopt.empty() && next_positional->target.index()) {
                    bool r = assign_argument(target,*next_positional, str);
                    mark_used(next_positional);
                    nth_opt = std::distance(_defs.begin(), next_positional);
                    if (!r) {
                        return Error{ParseErrorType::conversion_error, str, nth_arg, nth_opt};
                    } else {                    
                        if (is_multiple_path_option(next_positional->target)) adv_positional = true;
                        else ++next_positional;
                        return {};
                    }
                } else {
                    ++next_positional;
                }
            }        
            return Error{ParseErrorType::extra_content, str, nth_arg,-1};
        };

        auto collect_all_arguments = [&](StrV flag, const OptionDef<_Target> &def) -> SResult {
            if (iter == itend || iter->starts_with(str_flag_prefix)) {
                return Error{ParseErrorType::requires_argument, flag, std::distance(itbeg,iter),nth_opt};
            }
            do {                
                assign_argument(target, def, *iter);
                ++iter;
            } while (is_multiple_path_option(def.target) && iter != itend && !iter->starts_with(str_flag_prefix));
            return {};
        };


        while (iter != itend) {
            nth_arg = std::distance(itbeg, iter);
            nth_opt = -1;
            std::basic_string_view<_CharType> opt = *(iter++);
            if (opt.empty()) continue; //rare, but can happen if options are manually set
            if (opt[0] == '-') {    //short or long flag
                opt.remove_prefix(1);
                if (opt.empty()) {
                    auto err = positional_argument(opt);
                    if (err) return err;
                } else if (opt[0] == '-') { //long flag

                    opt.remove_prefix(1);

                    if (adv_positional) 
                        ++next_positional;

                    if (opt.empty()) {
                        while (iter != itend) {
                            positional_argument(*iter);
                            ++iter;
                        }
                        break;
                    }                    

                    auto seppos = opt.find_first_of(str_separators);
                    StrV flg = {}, extra={};
                    bool has_extra = false;
                    if (seppos != opt.npos) {
                        flg = opt.substr(0,seppos);
                        extra = opt.substr(seppos+1);
                        has_extra = true;
                    } else {
                        flg = opt;                    
                    }
                    auto fopt = std::find_if(_defs.begin(), _defs.end(), [&](const OptionDef<_Target> &d) {
                        if (d.lopt.size() != flg.size()) return false;
                        return std::find_if(flg.begin(), flg.end(), [&,idx = 0U](_CharType z) mutable {
                            return z != static_cast<_CharType>(d.lopt[idx++]);
                        }) == flg.end();
                    });                
                    if (fopt == _defs.end()) {
                        return Error{ParseErrorType::unknown_option, flg, nth_arg, nth_opt};
                    }
                    mark_used(fopt);
                    if (std::holds_alternative<bool _Target::*>(fopt->target)) {
                        auto ptr =std::get<bool _Target::*>(fopt->target);
                        (target.*ptr) = true;
                        if (!extra.empty()) {
                            return Error{ParseErrorType::extra_content, extra, nth_arg,nth_opt};
                        }        
                        continue;        
                    } else if (has_extra) {
                        auto r = assign_argument(target, *fopt, extra);
                        if (!r) return Error(ParseErrorType::conversion_error, extra, nth_arg);
                        if (!is_multiple_path_option(fopt->target)) continue;
                    }
                    auto err =collect_all_arguments(flg, *fopt);
                    if (err) return err;
                    continue;
                } else { //short flag

                    if (adv_positional) 
                        ++next_positional;

                    auto chiter = opt.begin();
                    auto fopt = _defs.end();
                    while (chiter != opt.end()) {
                        auto ch = *chiter++;
                        fopt = std::find_if(_defs.begin(), _defs.end(), [&](const OptionDef<_Target> &d) {
                            return d.sopt == ch;
                        });
                        if (fopt == _defs.end()) {
                            return Error{ParseErrorType::unknown_option, StrV(&ch,1), nth_arg,{}};
                        }
                        mark_used(fopt);
                        if (!std::holds_alternative<bool _Target::*>(fopt->target)) {
                            break;
                        }
                        auto ptr =std::get<bool _Target::*>(fopt->target);
                        (target.*ptr) = true;
                        fopt = _defs.end();
                    }
                    if (fopt != _defs.end()) {
                        bool force_extra = false;
                        if (chiter != opt.end() && str_separators.find(*chiter) != str_separators.npos) {++chiter;force_extra = true;}
                        auto extra = opt.substr(static_cast<std::size_t>(std::distance(opt.begin(), chiter)));
                        if (!std::holds_alternative<bool _Target::*>(fopt->target)) {
                            if (!extra.empty() || force_extra) {
                                if (str_separators.find(extra[0]) != str_separators.npos) {
                                    extra.remove_prefix(1);
                                }
                                if (!assign_argument(target, *fopt, extra)) {
                                    return Error(ParseErrorType::conversion_error, extra, nth_arg);
                                }
                                if (!is_multiple_path_option(fopt->target)) continue;;
                            }
                            _CharType f = static_cast<_CharType>(fopt->sopt);
                            auto err =collect_all_arguments(StrV(&f,1), *fopt);
                            if (err) return err;
                            continue;
                        }  else {
                            if (!extra.empty()) {
                                return Error(ParseErrorType::extra_content, extra, nth_arg);
                            }
                        }
                    }                 
                }
            } else {
                auto r = positional_argument(opt);
                if (r) return r;
            }
        }

        for (std::size_t idx= 0; const OptionDef<_Target> &opt: _defs) {
            if (opt.mandatory && !used[idx]) {                
                if (opt.lopt.empty() && !opt.sopt) {
                    return Error<_CharType>{ParseErrorType::argument_is_mandatory, {}, static_cast<std::ptrdiff_t>(options.size()), static_cast<std::ptrdiff_t>(idx)};    
                }
                return Error<_CharType>{ParseErrorType::option_is_mandatory, {}, static_cast<std::ptrdiff_t>(options.size()),static_cast<std::ptrdiff_t>(idx)};
            }
            ++idx;
        }

        return {};
    }

        
    template<typename _CharType>
    constexpr Result<_CharType> parse_cmdline(int argc, _CharType **argv,  _Target &target) const {
        std::vector<std::basic_string_view<_CharType> > args;
        for (int i = 1; i < argc; ++i) {
            args.push_back(std::basic_string_view<_CharType>(argv[i]));
            return parse_options(args, target);
        }
    }
    constexpr void generate_help( std::vector<char> &out, std::size_t line_len = std::size_t(-1)) const {
        std::size_t lsize = 0;
        std::size_t ssize = 0;
        std::size_t argsize = 0;
        bool has_switches = false;

        for ( const OptionDef<_Target> &opt: _defs) {
            if (opt.sopt) {has_switches = true;ssize = 1;}
            if (!opt.lopt.empty()) {lsize = std::max(lsize, opt.lopt.size());has_switches = true;}
            auto argname  = get_arg_name(opt);
            argsize = std::max(argsize, argname.size());        
        }

        if (has_switches)  {
            auto sws =  std::string_view(" [options...]");
            out.insert(out.end(), sws.begin(), sws.end());            
        } 

        for ( const OptionDef<_Target> &opt: _defs) {
            if (opt.lopt.empty() && !opt.sopt) {
                auto iter = std::back_inserter(out);                
                *iter++=' ';
                if (!opt.mandatory) {
                    *iter++='[';
                }
                iter = std::format_to(iter, "{}", get_arg_name(opt));
                if (is_multiple_path_option(opt.target)) {
                    iter = std::format_to(iter,"...");
                }
                if (!opt.mandatory) {
                    *iter++=']';
                }
            }
        }
        
        out.push_back('\n');
        out.push_back('\n');

        if (lsize) lsize += 4;
        if (ssize) ssize = 3;
        if (argsize) argsize += 1;
        auto reqspace = lsize+ssize+argsize+2;
        if (reqspace >= line_len-8) line_len = reqspace+8;
        
        std::vector<char> line;
        for ( const OptionDef<_Target> &opt: _defs) {
            line.clear();
            auto iter =std::back_inserter(line);
            if (opt.sopt) {
                iter = std::format_to(iter, "-{} ", opt.sopt);
            } else if (ssize) {
                iter = std::fill_n(iter, ssize, ' ');
            }
            if (!opt.lopt.empty()) {
                iter = std::format_to(iter, "--{} ", opt.lopt);
            } else {
                std::fill_n(iter, 3, ' ');
            }
            if (lsize) {
                iter = std::fill_n(iter, lsize - opt.lopt.size() - 2, ' ');
            }
            auto argname  = get_arg_name(opt);
            iter = std::format_to(iter,"{}", argname);
            iter = std::fill_n(iter, argsize-argname.size(), ' ');
            iter = std::format_to(iter, "- {}", opt.description);
            if (opt.mandatory) iter = std::format_to(iter, " (mandatory)");

            while (line.size() > line_len) {
                auto subline = std::string_view(line.data(), line_len);
                auto nln = subline.find('\n');
                if (nln != subline.npos) {
                    subline = subline.substr(0,nln);                
                } else {
                    nln = subline. rfind(' ');
                    if (nln != subline.npos && nln > reqspace) {
                        subline = subline.substr(0, nln);
                    }
                }
                out.insert(out.end(), subline.begin(), subline.end());
                out.push_back('\n');
                std::fill_n(line.begin(),reqspace+1, ' ');
                line.erase(line.begin()+static_cast<std::ptrdiff_t>(reqspace+1), line.begin()+static_cast<std::ptrdiff_t>(subline.size()));
            }
            out.insert(out.end(), line.begin(), line.end());
            out.push_back('\n');
        }
    }

    constexpr std::string generate_help( std::size_t line_len = std::size_t(-1)) const {
        std::vector<char> out;
        generate_help(out, line_len);
        return {out.begin(), out.end()};
    }

    std::string nth_option_to_string(std::ptrdiff_t nth_argument) const {
        std::size_t n = static_cast<std::size_t>(nth_argument);
        if (n >= _defs.size()) return {};
        const auto  &opt = _defs[n];
        if (opt.sopt) return std::format("-{}", static_cast<char>(opt.sopt));
        if (!opt.lopt.empty()) return std::format("--{}", opt.lopt);
        return std::string(get_arg_name(opt));
    }

    std::string nth_option_to_arg_name(std::size_t nth_argument) const {
        if (nth_argument >= _defs.size()) return {};
        const auto  &opt = _defs[nth_argument];
        return get_arg_name(opt);
    }


protected:
    std::span<const Def> _defs;

    std::optional<std::filesystem::path> _root_path;


    template<typename T> requires (std::is_arithmetic_v<T> && !std::is_same_v<T,bool>) 
    static constexpr bool do_assign_argument(T &target_val, std::basic_string_view<char> str) {        
        if consteval {
            if constexpr(std::is_floating_point_v<T>) {
                target_val = 0;
                auto fraction = static_cast<T>(0.1);
                bool fractional = false;
                bool negative = false;

                for (char c : str) {
                    if (c == '-') {
                        negative = true;
                    } else if (c == '.') {
                        fractional = true;
                    } else if (c >= '0' && c <='9') {
                        const auto digit = static_cast<unsigned>(c - '0');

                        if (!fractional)
                            target_val = target_val * static_cast<T>(10) + static_cast<T>(digit);
                        else {
                            target_val += static_cast<T>(digit) * fraction;
                            fraction *= static_cast<T>(0.1);
                        }
                    } else {
                        return false;
                    }
                }

                if (negative) target_val = -target_val;
                return true;
            }
        }
        auto res =std::from_chars(str.data(), str.data()+str.size(), target_val);
        if (res.ec != std::errc{} || res.ptr != str.data()+str.size()) return false;
        return true;
    }

    static constexpr bool do_assign_argument(std::string &target_val, std::basic_string_view<char> str) {    
        target_val.clear();
        target_val.append(str);
        return true;
    }

    template<typename _CharType> requires(sizeof(_CharType) != 1)
    static constexpr bool do_assign_argument(std::string &target_val, std::basic_string_view<_CharType> str) {    
        target_val.clear();
        for (auto c: str) {
            std::size_t cu = static_cast<std::size_t>(c);
            if (cu < 0x80) target_val.push_back(static_cast<char>(cu));
            else {
                auto len = cu < 0x800?1:cu < 0x10000?2:cu<0x110000?3:0;
                if (len == 0) return false;
                auto pfx = static_cast<unsigned char>(0xF0 << (3-len));
                for (int i = 0; i <= len; ++i) {
                    target_val.push_back(static_cast<char>(pfx | (cu >> (7 * (i - len )))));
                    pfx = 0x80;
                }
            }
        }
        return true;
    }

    template<typename _CharType> requires(sizeof(_CharType) != 1)
    static constexpr bool do_assign_argument(std::wstring &target_val, std::basic_string_view<_CharType> str) {    
        target_val.clear();
        for (auto c: str) target_val.push_back(static_cast<wchar_t>(c));
        return true;
    }

    template<typename _CharType> 
    static constexpr bool do_assign_argument(bool &target_val, std::basic_string_view<_CharType> ) {    
        target_val = true;
        return true;
    }

    template<typename _CharType> 
    constexpr bool do_assign_argument(std::filesystem::path &target_val, std::basic_string_view<_CharType> str) const {            
        if (_root_path) target_val = _root_path.value() / str;
        else target_val = std::filesystem::path(str);
        return true;
    }


    template<typename _CharType> 
    constexpr bool do_assign_argument(std::vector<std::filesystem::path> &target_val, std::basic_string_view<_CharType> str) const {    
        if (_root_path) target_val.push_back(_root_path.value()/str);
        else target_val.push_back(std::filesystem::path(str));
        return true;
    }

    template<typename T, typename _CharType> requires (std::is_arithmetic_v<T> && sizeof(_CharType)>1) 
    static constexpr bool do_assign_argument(T &target_val, std::basic_string_view<_CharType> str) {    
        std::string buff;
        return do_assign_argument(buff, str) && do_assign_argument(target_val, buff);
        
    }

    static constexpr bool do_assign_argument(std::wstring &target_val, std::string_view str) {    
        target_val.clear();
        wchar_t acc = 0;
        int len = 0;
        for (auto c: str) {
            unsigned char cu = static_cast<unsigned char>(c);
            if ((cu & 0xC0) == 0x80) {
                if (!len) return false;
                acc = (acc << 7) | cu;
                --len;
                if (!len) target_val.push_back(acc);
            } else {
                if (len) return false;            
                if ((cu & 0x80) == 0) target_val.push_back(static_cast<wchar_t>(cu));            
                else if ((cu & 0xE0) == 0xC0) {len=1; acc = cu & 0x01F;}
                else if ((cu & 0xF0) == 0xE0) {len=2; acc = cu & 0x0F;}
                else if ((cu & 0xF8) == 0xF0) {len=3; acc = cu & 0x07;}
                else if ((cu & 0xFE) == 0xF8) {len=4; acc = cu & 0x03;}
                else return false;
            }
        }
        return len == 0;
    }

    template<typename _CharType, typename T>
    constexpr bool do_assign_argument(std::optional<T> &target_val, std::basic_string_view<_CharType> str) const{
        target_val.emplace();
        return do_assign_argument(target_val.value(), str);
    }


    template<typename _CharType>
    constexpr bool assign_argument(_Target &trg, const OptionDef<_Target> &def, std::basic_string_view<_CharType> str) const {
        return std::visit([&](auto ptr){
            if constexpr(std::is_same_v<decltype(ptr), std::monostate>) {
                return false;
            } else {
                return do_assign_argument((trg.*ptr), str);
            }
        }, def.target);
    }

    static constexpr std::string_view get_arg_name(const OptionDef<_Target> &opt) {
        if (!opt.arg_name.empty()) return opt.arg_name;
        return std::visit([&]<typename X>(X){       
            if constexpr(std::is_same_v<X, std::monostate>) {
                return std::string_view("ERROR!");
            } else {
                return argument_name<typename member_traits<X>::value_type>;
            }
        }, opt.target);
}

/*static_assert(argument_name<typename member_traits<std::optional<int Error<char>::*> >::value_type> == "arg");
static_assert(std::is_void_v<typename member_traits<std::optional<int Error<char>::*> >::value_type>);*/

};




}

