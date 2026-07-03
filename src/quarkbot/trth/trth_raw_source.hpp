#pragma once

#include <quarkbot/utils/csv_reader.h>
#include <functional>
#include <filesystem>



class TRTHRawSource {
public:

struct Data {
    
    std::string RIC;
    std::string Alias_Underlying_RIC;
    std::string Domain;
    std::string Date_Time;
    std::string GMT_Offset;
    std::string Type;
    std::string Price;
    std::string Volume;
    std::string Bid_Price;
    std::string Bid_Size;
    std::string Ask_Price;
    std::string Ask_Size;
    std::string Qualifiers;
    std::string Trade_Price_Currency;
    std::string Change_Type;
    std::string Old_Value;
    std::string New_Value;

};


    TRTHRawSource(std::filesystem::path source_file);

    bool read(Data &data);
    bool all_columns_mapped() const {return colmap.allMapped;}


protected:    
    struct CSVSource {
        std::function<std::string_view()> block_reader;
        std::string_view cur_line = {};
        int operator()();        
    };

    CSVReader<CSVSource> csv;
    CSVFieldIndexMapping<TRTHRawSource::Data> colmap;

    static CSVSource init_source(std::filesystem::path source_file);
};
