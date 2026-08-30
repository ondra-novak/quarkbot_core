#include "quarkbot/utils/cli_options.hpp"
#include <string>
#include <string_view>
#include "check.h"

struct TestTarget {
    std::string a1;
    std::wstring a2;
    bool  a3;
    int  a4;
    unsigned int  a5;
    cli::SInt  a6;
    cli::UInt  a7;
    double  a8;
    float  a9;
    std::optional<std::string>  a11;
    std::optional<cli::UInt>  a12;
    std::optional<cli::SInt>  a13;
    std::optional<double>  a14;
    std::optional<float>  a15;
};

constexpr auto testDef = std::array<cli::OptionDef<TestTarget>, 15>({
    {&TestTarget::a1,'a',"a1","a1","Argument a1"},
    {&TestTarget::a2,'b',"a2","a2","Argument a2"},
    {&TestTarget::a3,'c',"a3","a3","Argument a3"},
    {&TestTarget::a4,'d',"a4","a4","Argument a4"},
    {&TestTarget::a5,'e',"a5","a5","Argument a5"},
    {&TestTarget::a6,'f',"a6","a6","Argument a6"},
    {&TestTarget::a7,'g',"a7","a7","Argument a7"},
    {&TestTarget::a8,'h',"a8","a8","Argument a8"},
    {&TestTarget::a9,'i',"a9","a9","Argument a9"},
    {&TestTarget::a11,'k',"a11","a11","Argument a11"},
    {&TestTarget::a12,'l',"a12","a12","Argument a12"},
    {&TestTarget::a13,'m',"a13","a13","Argument a13"},
    {&TestTarget::a14,'n',"a14","a14","Argument a14"},
    {&TestTarget::a15,'o',"a15","a15","Argument a15"},
});

constexpr std::array<std::string_view, 13> args = {
    {"-a","abc", "-b=xyz","-cd:-42","--a5","58","--a6=-100",
        "-g44", "-h-1.25", "-i3.1415","--a11=", "--a12","474"}
};

constexpr int test_basic() {

    cli::CLIParser<TestTarget> parser(testDef);
    TestTarget target;
    auto err = parser.parse_options<char>(args, target);
    if (err.has_value()) return -1;
    if (target.a1 != "abc") return 1;
    if (target.a2 != L"xyz") return 2;
    if (!target.a3) return 3;
    if (target.a4 != -42) return 4;
    if (target.a5 != 58) return 5;
    if (target.a6 != -100) return 6;
    if (target.a7 != 44) return 7;
    if (target.a8 != -1.25) return 8;
    if (target.a9 < 3.14149f || target.a9 > 3.14151f) return 9;
    if (!target.a11 || target.a11.value() != "") return 10;
    if (!target.a12 || target.a12.value() != 474) return 11;

    return 0;
}

static_assert(test_basic() == 0);

struct TestFlagsTarget {
    bool a = false;
    bool b = false;
    bool c = false;
    bool d = false;
};

constexpr int test_flags() {
    std::array<cli::OptionDef<TestFlagsTarget>,4> def  ({
        {&TestFlagsTarget::a, 'a',"aaa"},
        {&TestFlagsTarget::b, 'b',"bbb"},
        {&TestFlagsTarget::c, 'c',"ccc"},
        {&TestFlagsTarget::d, 'd',"ddd"},        
    });

    TestFlagsTarget t;
    cli::CLIParser<TestFlagsTarget> parser(def);
    std::array<std::string_view, 1> case1 ({"-abc"});
    auto r = parser.parse_options<char>(case1, t);
    if (r) return -1;
    if (!t.a) return 1;
    if (!t.b) return 2;
    if (!t.c) return 3;
    if (t.d) return 4;

    t = {};
    std::array<std::string_view, 3> case2 ({"--aaa","--ddd","-aaa"});
    r = parser.parse_options<char>(case2, t);
    if (r) return -2;

    if (!t.a) return 5;
    if (!t.d) return 6;
    if (t.b) return 7;
    if (t.c) return 8;

    t = {};
    std::array<std::string_view, 2> case3 ({"-aaa","-bc"});
    r = parser.parse_options<char>(case3, t);
    if (r) return -2;

    if (!t.a) return 9;
    if (t.d) return 10;
    if (!t.c) return 11;
    if (!t.b) return 12;

    return 0;
    
};

static_assert(test_flags() == 0);

struct TestPathTarget {
    std::filesystem::path target;
    std::vector<std::filesystem::path> sources;
};

void test_path() {
    std::array<cli::OptionDef<TestPathTarget>,2> def  ({
        {&TestPathTarget::target, 't',"target","target_path"},
        {&TestPathTarget::sources,{},{},"sources..."}
    });

    TestPathTarget t;
    std::array<std::string_view, 4> case1({"f1","f2","f3","-t=dir"});
    cli::CLIParser<TestPathTarget> parser(def, std::filesystem::path("/foo"));
    auto r = parser.parse_options<char>(case1, t);
    CHECK(!r);
    CHECK_EQUAL(t.target.string(), "/foo/dir");
    CHECK_EQUAL(t.sources.size(), 3);
    CHECK_EQUAL(t.sources[0].string(), "/foo/f1");
    CHECK_EQUAL(t.sources[1].string(), "/foo/f2");
    CHECK_EQUAL(t.sources[2].string(), "/foo/f3");

    t = {};
    std::array<std::string_view, 4> case2({"-t=dir","f1","f2","f3"});
    r = parser.parse_options<char>(case2, t);
    CHECK(!r);
    CHECK_EQUAL(t.target.string(), "/foo/dir");
    CHECK_EQUAL(t.sources.size(), 3);
    CHECK_EQUAL(t.sources[0].string(), "/foo/f1");
    CHECK_EQUAL(t.sources[1].string(), "/foo/f2");
    CHECK_EQUAL(t.sources[2].string(), "/foo/f3");

}

int main() {
    cli::OptionParameterTarget<TestTarget> tst(&TestTarget::a2);
    CHECK_EQUAL(tst.index(),2);
    CHECK_EQUAL(testDef[0].target.index(),1);
    CHECK_EQUAL(test_basic(),0);
    CHECK_EQUAL(test_flags(),0);
    test_path();
}

