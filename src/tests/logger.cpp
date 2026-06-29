#include "impl/logger.hpp"
#include "ifc/log.hpp"
#include "check.h"
#include "utils/json.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>

using namespace quarkbot;


std::string prev_line;
LogLevel prev_log_level =LogLevel::disabled;
std::source_location prev_source_loc;


void test_logger(LogLevel level, const std::source_location *location, std::string_view content){
    prev_line = content;
    prev_log_level = level;
    prev_source_loc = *location;
}


struct TesterClass {
    static void test_log() {
        logOutput(LogLevel::debug, "{}={}", "y",[]{return "test";});;

    }

};

struct ParseLogRes {
    std::string context;
    std::string payload;
    unsigned int line = 0;
};

static auto rg = std::regex(R"regexp(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} T\d+ debug \[([^\]]+)\] (.*) \{.*[/\\]logger\.cpp:(\d+)\}$)regexp");

ParseLogRes extract_log(std::string_view line) {
    ParseLogRes out;
    auto b = std::regex_iterator(line.begin(), line.end(), rg);
    auto e = decltype(b)();
    std::vector<std::string> m;
    if (b != e) {
        for (const auto &mp:*b) {
            m.push_back(mp.str());
        }

        if (m.size()>1) out.context = m[1];
        if (m.size()>2) out.payload = m[2];
        if (m.size()>3) out.line = static_cast<unsigned int>(std::strtoul(m[3].c_str(),nullptr, 10));
    }
    return out;
    

}


int main() {

    Logger::instance.cur_level = LogLevel::debug;
    Logger::instance.log_sink = test_logger;

    logOutput(LogLevel::debug, "{} {}", "Hello", 42);
    CHECK_EQUAL(prev_line, "Hello 42");
    CHECK(prev_log_level == LogLevel::debug);
    CHECK(std::string_view(prev_source_loc.file_name()).ends_with("logger.cpp"));

    logOutput(LogLevel::debug, "{}", Json{10,20});
    CHECK_EQUAL(prev_line, "[10,20]");

    logOutput( LogLevel::debug,"{}", []{return 42;});
    CHECK_EQUAL(prev_line, "42");
    std::error_code ec;

    std::filesystem::path p = std::filesystem::temp_directory_path()/"quarkbot_test_log.log";
    std::filesystem::remove(p,ec);
    
    
    log_to_file(p);
//    log_to_stderr();
    logDebug("{}", Json{{"a",10},{"b",{1,2,3}}});
    logDebug("{}={}", "x",12);
    logDebug("new\nline");
    TesterClass::test_log();

    log_close();

    auto f = std::ifstream(p);
    CHECK(!!f);

    std::string ln;
    std::getline(f,ln);
    auto ex = extract_log(ln);
    CHECK_EQUAL(ex.context,"-");
    CHECK_EQUAL(ex.payload,"{\"a\":10,\"b\":[1,2,3]}");
    CHECK_EQUAL(ex.line,87);


    std::getline(f,ln);
    ex = extract_log(ln);
    CHECK_EQUAL(ex.context,"-");
    CHECK_EQUAL(ex.payload,"x=12");
    CHECK_EQUAL(ex.line,88);

    std::getline(f,ln);
    ex = extract_log(ln);
    CHECK_EQUAL(ex.payload,"new\x7fline");
    CHECK_EQUAL(ex.line,89);


    std::getline(f,ln);
    ex = extract_log(ln);
    CHECK_EQUAL(ex.context,"TesterClass");
    CHECK_EQUAL(ex.payload,"y=test");
    CHECK_EQUAL(ex.line,30);


    std::filesystem::remove(p,ec);
}
