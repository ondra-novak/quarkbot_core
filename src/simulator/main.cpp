#include <iostream>
#include <fstream>
#include <sstream>
#include <json20.h>


std::string readStreamToString(std::istream& inputStream) {
    // Vytvoření stringstreamu pro načtení dat ze vstupního proudu
    std::stringstream stringStream;
    stringStream << inputStream.rdbuf(); // Načtení dat ze vstupního proudu do stringstreamu
    return stringStream.str(); // Vrácení obsahu stringstreamu jako std::string
}


static json::value_t load_config(const std::string &fname) {
    std::ifstream f(fname);
    if (!f) throw std::runtime_error("Failed to open config file");
    auto str = readStreamToString(f);
    return json::value_t::from_json(str);
}


int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <config.json>" << std::endl;
        return 1;
    }
    try {

        std::string cfgfname = argv[1];
        auto cfg = load_config(cfgfname);


    } catch (const std::exception &e) {
        std::cerr << "FATAL:" << e.what() << std::endl;
    }


}
