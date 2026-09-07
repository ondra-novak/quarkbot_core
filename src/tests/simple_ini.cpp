#include <print>
#include <quarkbot/utils/simple_ini.hpp>
#include <utility>
#include "check.h"


constexpr std::string_view simple_ini_example = ""
"include=path/file\n"
"[section1]\n"
"key=value\n"
"key=\n"
"=value\n"
"\n"
"[bad section\n"
"# comment\n"
"[bad section=2\n"
"    [spacer]\n"
" \"#key\"=\"value\"\n"
" \"bad key\" extra = 'value'\n"
" \"bad key = value\"\n"
"bad_value='value\n"
"bad_value 2=\"value\n"
;


void test1() {
    auto source = [s = simple_ini_example]() mutable {return std::exchange(s, "");};
    using Reader = IniReader<decltype(source), true>;
    Reader rd(std::move(source));
    Reader::Row row;
    
    bool b;
    b = rd.next(row);
    CHECK(b);
    CHECK_EQUAL(row.section,"");
    CHECK_EQUAL(row.key,"include");
    CHECK_EQUAL(row.value, "path/file");
    CHECK_EQUAL(row.comment, "");
    b = rd.next(row);
    CHECK(b);
    CHECK_EQUAL(row.section,"section1");
    CHECK_EQUAL(row.key,"key");
    CHECK_EQUAL(row.value, "value");
    CHECK_EQUAL(row.comment, "");
    b = rd.next(row);
    CHECK(b);
    CHECK_EQUAL(row.section,"section1");
    CHECK_EQUAL(row.key,"key");
    CHECK_EQUAL(row.value, "");
    CHECK_EQUAL(row.comment, "");
    b = rd.next(row);
    CHECK(b);
    CHECK_EQUAL(row.section,"section1");
    CHECK_EQUAL(row.key,"");
    CHECK_EQUAL(row.value, "value");
    CHECK_EQUAL(row.comment, "");
    b = rd.next(row);
    CHECK(b);
    CHECK_EQUAL(row.section,"section1");
    CHECK_EQUAL(row.key,"[bad section");
    CHECK_EQUAL(row.value, "");
    CHECK_EQUAL(row.comment, "");
    b = rd.next(row);
    CHECK(b);
    CHECK_EQUAL(row.section,"section1");
    CHECK_EQUAL(row.key,"");
    CHECK_EQUAL(row.value, "");
    CHECK_EQUAL(row.comment, "# comment");
    b = rd.next(row);
    CHECK(b);
    CHECK_EQUAL(row.section,"section1");
    CHECK_EQUAL(row.key,"[bad section");
    CHECK_EQUAL(row.value, "2");
    CHECK_EQUAL(row.comment, "");
    b = rd.next(row);
    CHECK(b);
    CHECK_EQUAL(row.section,"spacer");
    CHECK_EQUAL(row.key,"#key");
    CHECK_EQUAL(row.value, "value");
    CHECK_EQUAL(row.comment, "");
    b = rd.next(row);
    CHECK(b);
    CHECK_EQUAL(row.section,"spacer");
    CHECK_EQUAL(row.key,"\"bad key\" extra");
    CHECK_EQUAL(row.value,"value");
    CHECK_EQUAL(row.comment, "");
    b = rd.next(row);
    CHECK(b);
    CHECK_EQUAL(row.section,"spacer");
    CHECK_EQUAL(row.key,"\"bad key");
    CHECK_EQUAL(row.value, "value\"");
    CHECK_EQUAL(row.comment, "");
    b = rd.next(row);
    CHECK(b);
    CHECK_EQUAL(row.section,"spacer");
    CHECK_EQUAL(row.key,"bad_value");
    CHECK_EQUAL(row.value, "'value");
    CHECK_EQUAL(row.comment, "");
    b = rd.next(row);
    CHECK(b);
    CHECK_EQUAL(row.section,"spacer");
    CHECK_EQUAL(row.key,"bad_value 2");
    CHECK_EQUAL(row.value, "\"value");
    CHECK_EQUAL(row.comment, "");
    b = rd.next(row);
    CHECK(!b);
}


int main() {
    test1();
}