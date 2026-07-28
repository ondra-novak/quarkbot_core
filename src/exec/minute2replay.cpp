
#include <iostream>
int main(int argc, char **argv) {


    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <symbol> <start_timestamp_sec> <spread>" << std::endl;
        return 1;
    }

    std::string_view symbol = argv[1];
    std::size_t start_timestamp = std::strtoull(argv[2], NULL, 10);;
    std::size_t timestamp = start_timestamp * 1000000;
    double spread = std::strtod(argv[3], nullptr);



    std::cout << "timestamp,symbol,event,price,quantity,side,flags" << std::endl;
    
    double prev_price =0;

    while (!!std::cin) {
        double price = 0;
        std::cin >> price;
        if (price > 0)  {
            double ask = price + spread;
            double bid = price - spread;
            double ask_size = 1000000/ask;
            double bid_size = 1000000/bid;
            double trade_size = 100000/price;
            std::cout << timestamp << "," << symbol << ",quote," << ask << "," << ask_size << ",ASK," << std::endl;
            std::cout << timestamp << "," << symbol << ",quote," << bid << "," << bid_size << ",BID," << std::endl;
            std::cout << timestamp << "," << symbol << ",trade," << price << "," << trade_size << ","<< (prev_price < price?"BUY":"SELL") << "," << std::endl;
            prev_price = price;            
            timestamp+=1000000;
        }
    }

}