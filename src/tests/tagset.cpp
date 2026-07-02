#include <quarkbot/utils/tagset.hpp>


constexpr auto colors_def = TagSet::Domain({"red","blue","green","white","yellow", "purple"});
constexpr auto colors = TagSet(colors_def,0);



static_assert(TagSet("blue", colors).get_raw() == 2);
static_assert(TagSet("green", colors).get_raw() == 4);
static_assert((TagSet("green", colors)|"blue").get_raw() == 6);
static_assert(!(TagSet("green", colors)|"blue").contains("red"));
static_assert((TagSet("red", colors)|"yellow").contains("red"));
static_assert(!(TagSet("red", colors)|TagSet("white",colors)).contains(TagSet("red", colors)));
static_assert((TagSet("red", colors)|TagSet("white",colors)).contains_one_of(TagSet("red", colors)));
static_assert((TagSet("red", colors)|TagSet("white",colors)).to_string() == "red,white");

int main() {
    return 0;
}