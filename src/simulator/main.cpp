#include <iostream>
#include <fstream>
#include <sstream>
#include <common/structured_ini.h>
#include <common/basic_exchange.h>
#include <common/basic_config.h>
#include <common/basic_log.h>
#include "sim_exchange.h"


using namespace quarkbot;

std::shared_ptr<StructuredIni> load_config(const std::string &fname) {
    auto ini = std::make_shared<StructuredIni>();
    ini->parse_file(std::filesystem::path(fname));
    return ini;
}



int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <config.conf>" << std::endl;
        return 1;
    }
    try {
        std::string cfgfname = argv[1];
        auto cfg = load_config(cfgfname);

        auto logservice = std::make_shared<BasicLog>(std::cerr, ILog::Serverity::debug);
        auto context = std::make_shared<BasicExchangeContext>("simulator", Network(), Log(logservice));
        context->init(std::make_unique<SimExchange>(), Config(std::make_shared<BasicConfig>(cfg,(*cfg)["simulator"])));


    } catch (const std::exception &e) {
        std::cerr << "FATAL:" << e.what() << std::endl;
    }


}
