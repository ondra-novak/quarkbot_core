#include <quarkbot/strategy_api.h>
#include <quarkbot/module.h>
#include <quarkbot/tickdata.h>
#include <quarkbot/ta/sma.h>


using namespace quarkbot;

class Example: public Strategy {
    Persistent<SMA<Decimal> > _sma;

public:
    virtual ConfigSchema get_config_schema() const override;
    virtual coro main() override {
    }




};


EXPORT_STRATEGY(Example);




















/*
void Example::on_start() {
    auto vs = get_vars<int>("dummy");
    for (const auto &[key, value]: vs) {
        key;
        value;
    }

}
*/

ConfigSchema Example::get_config_schema() const {
    using namespace quarkbot::params;
    return {
        Group("gr1",{
            Text("text_example"),
            TextInput("text_area_example", "defval"),
            Select("s2",{
                {"opt1","label1"},
                {"opt2","label2"},
            }),
            Number("any",100),
        }),
        Group({
            Number("n1", 0.0, {.min=0.0,.max=100.0, .step=1.0}),
            Slider("n2", 0.0, {.min=0.0,.max=100.0, .step=1.0, .log_scale = true}),
            CheckBox("chk1", false),
            Select("s1",{
                    {"opt1","label1"},
                    {"opt2","label2"},
                    {"opt3","label3"}
            }, ""),
            TextArea("txt1",10,"hello world!",65536,{
                    .show_if = {{"chk1"}}
            }),
            Section("not_seen",{})
        },{
          .show_if = {
                  {"s2",{"opt1"}}
          }
        }),
        Section("ext1", {
                Compound({
                    Date("date1", {2020,10,12}, {.min={2000,1,1}}),
                    Time("time1", {12,5,30}, {}),
                    TimeZoneSelect("tz1"),
                    Section("not seen",{}),
                    Group("not seen",{})
                })
        }),
        Section("ext2", {}),
        Section("ext3", {}, shown),
    };
}


