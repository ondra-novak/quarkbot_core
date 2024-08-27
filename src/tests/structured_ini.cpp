#include "../common/structured_ini.h"
#include "check.h"

#include <algorithm>
constexpr std::string_view test1_data = R"ini(

key1=value1 
key2=value2
;comment
#comment 
[sect1]  #comment

key3=value3 #comment
key4=value4 ;comment
multiline= \ ;comment
 line1 \     ;comment
 line2       ;comment
multiline_q = "Lorem ipsum dolor sit amet, consectetuer adipiscing elit. \
               Mauris suscipit, ligula sit amet pharetra semper, nibh ante \
               cursus purus, vel sagittis velit mauris vel metus. Cras pede \
               libero, dapibus nec, pretium sit amet, tempor quis. Aenean \
               fermentum risus id tortor. In dapibus augue non sapien. \
               Fusce tellus. Maecenas lorem. Ut enim ad minima veniam, \
               quis nostrum exercitationem ullam corporis suscipit \    ; comment
               laboriosam, nisi ut aliquid ex ea commodi consequatur? \
               Aliquam erat volutpat. Cum sociis natoque penatibus et \
               magnis dis parturient montes, nascetur ridiculus mus. \
               Vivamus porttitor turpis ac leo. Duis aute irure dolor \
               in reprehenderit in voluptate velit esse cillum dolore \
                eu fugiat nulla pariatur. Fusce wisi. Duis risus."


{ext1}

[sect2]
  key5 = value5 

    {ext2}
    
    [sect3]
    key6 = value6
        [sect4]
        key7 = value7
    [sect5]
        key8 = value8

  key9=value9
 key10 = value10

[sect6]

    {ext3}

{ext4}

q1=" hello "
"key=key" = "value\nvalue"
"key=key"key = value"value"value;
key_bin="\x52\xC1\x8F"
array=1,2,3, 4, "5"
      6,7,8
      9
      10
      "konec,zvonec"


)ini";



void test1() {
    StructuredIni ini;
    std::vector<StructuredIni::ExternalRef> extref;
    std::istringstream fin((std::string(test1_data)));
    ini.parse(fin, std::move(extref));
    auto root = ini.root();
    auto sec1 = root.section("sect1");
    auto sec2 = root.section("sect2");
    auto sec3 = sec2.section("sect3");
    auto sec4 = sec3.section("sect4");
    auto sec5 = sec2.section("sect5");
    auto sec6 = root.section("sect6");
    auto sec7 = root.section("sect7");
    CHECK(sec1.defined());
    CHECK(sec2.defined());
    CHECK(sec3.defined());
    CHECK(sec4.defined());
    CHECK(sec5.defined());
    CHECK(sec6.defined());
    CHECK(!sec7.defined());
    CHECK_EQUAL(std::string_view(root.get("key1")), "value1");
    CHECK_EQUAL(std::string_view(root.get("key2")), "value2");
    CHECK_EQUAL(std::string_view(sec1.get("multiline")), "line1 line2");
    CHECK_EQUAL(std::string_view(sec1.get("multiline_q")), "Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Mauris suscipit, ligula sit amet pharetra semper, nibh ante cursus purus, vel sagittis velit mauris vel metus. Cras pede libero, dapibus nec, pretium sit amet, tempor quis. Aenean fermentum risus id tortor. In dapibus augue non sapien. Fusce tellus. Maecenas lorem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Aliquam erat volutpat. Cum sociis natoque penatibus et magnis dis parturient montes, nascetur ridiculus mus. Vivamus porttitor turpis ac leo. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Fusce wisi. Duis risus.");
    CHECK_EXCEPTION(StructuredIni::NotFound, root.get("key3"));
    CHECK_EXCEPTION(StructuredIni::NotFound, root.get("key4"));
    CHECK_EQUAL(std::string_view(sec1.get("key3")), "value3");
    CHECK_EQUAL(std::string_view(sec1.get("key4")), "value4");
    CHECK_EQUAL(std::string_view(sec2.get("key5")), "value5");
    CHECK_EQUAL(std::string_view(sec3.get("key6")), "value6");
    CHECK_EQUAL(std::string_view(sec4.get("key7")), "value7");
    CHECK_EQUAL(std::string_view(sec5.get("key8")), "value8");
    CHECK_EQUAL(std::string_view(sec2.get("key9")), "value9");
    CHECK_EQUAL(std::string_view(sec2.get("key10")), "value10");
    CHECK_EQUAL(std::string_view(sec6.get("q1")), " hello ");
    CHECK_EQUAL(std::string_view(sec6.get("key=key")), "value\nvalue");
    CHECK_EQUAL(std::string_view(sec6.get("key=keykey")), "value\"value\"value");
    CHECK_EQUAL(std::string_view(sec6.get("key_bin")), "\x52\xC1\x8F");
    CHECK_EQUAL(std::string_view(sec6.get("array")), "1,2,3,4,5,6,7,8,9,10,konec,zvonec");
    {
        std::string_view data[] = {"1","2","3","4","5","6","7","8","9","10","konec,zvonec"};
        auto srciter = std::begin(data);
        for (auto val: sec6["array"]) {
            CHECK_EQUAL(std::string_view(val), *srciter);
            ++srciter;
        }
    }
    CHECK_EQUAL(extref.size(),4);
    CHECK_EQUAL(extref[0].name, "ext1");
    CHECK_EQUAL(extref[0].root->key_value["key3"].content, "value3");
    CHECK_EQUAL(extref[1].root->key_value["key5"].content, "value5");
    CHECK_EQUAL(extref[2].root->key_value["q1"].content," hello ");
    CHECK_EQUAL(extref[3].root, extref[2].root);
}


constexpr std::pair<std::string_view, std::string_view> test_data2[] = {
        {"root","{ref1}\n[sect1]\n{sub/ref2}\n[sect2]\n{ref3}\n\t[sect3]\n\t\t{ref4}\n{ref5}"},
        {"ref1","key1=value1\n{ref6}"},
        {"ref2","key2=value2\n[sect_sub]\nkey_sub=value_sub\n"},
        {"ref3","key3=value3\n[sect_sub2]\nkey_sub2=value_sub2\n"},
        {"ref4","key4=value4\n[sect_sub3]\nkey_sub3=value_sub3\n"},
        {"ref5","key5=value5\n[sect_sub4]\nkey_sub4=value_sub4\n"},
        {"ref6","key6=value6\n[sect_sub5]\nkey_sub5=value_sub5\n"},
};

void test2(){
    StructuredIni ini;
    ini.parse_file("/test/root",
            [f = std::optional<std::istringstream>()](std::filesystem::path p) mutable -> std::istream &{
        f.reset();
        auto iter = std::find_if(std::begin(test_data2), std::end(test_data2), [&](const auto &pf){
           return pf.first == p.filename();
        });
        f.emplace(std::string(iter->second));
        return *f;
    }, 1024);

    auto root = ini.root();
    auto sect1 = root.section("sect1");
    CHECK_EQUAL(std::filesystem::path(sect1["key2"]), std::filesystem::path("/test/sub/value2"));

}

int main() {
    test1();
    test2();

}


