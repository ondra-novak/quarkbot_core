#include "trth_raw_source.hpp"


#include <stdexcept>
#include <zlib.h>
#include <array>



TRTHRawSource::TRTHRawSource(std::filesystem::path source_file)
:csv(init_source(std::move(source_file)))
{
    colmap = csv.mapColumns<TRTHRawSource::Data>({
        {"#RIC", &TRTHRawSource::Data::RIC},
        {"Alias Underlying RIC", &TRTHRawSource::Data::Alias_Underlying_RIC},
        {"Domain", &TRTHRawSource::Data::Domain},
        {"Date-Time", &TRTHRawSource::Data::Date_Time},
        {"GMT Offset", &TRTHRawSource::Data::GMT_Offset},
        {"Type", &TRTHRawSource::Data::Type},
        {"Price", &TRTHRawSource::Data::Price},
        {"Volume", &TRTHRawSource::Data::Volume},
        {"Bid Price", &TRTHRawSource::Data::Bid_Price},
        {"Bid Size", &TRTHRawSource::Data::Bid_Size},
        {"Ask Price", &TRTHRawSource::Data::Ask_Price},
        {"Ask Size", &TRTHRawSource::Data::Ask_Size},
        {"Qualifiers", &TRTHRawSource::Data::Qualifiers},
        {"Trade PriceCurrency", &TRTHRawSource::Data::Trade_Price_Currency},
        {"Change Type", &TRTHRawSource::Data::Change_Type},
        {"Old Value", &TRTHRawSource::Data::Old_Value},
        {"New Value", &TRTHRawSource::Data::New_Value}
    });
}

bool TRTHRawSource::read(Data &data){
    return csv.readRow(colmap, data);

 }

TRTHRawSource::CSVSource TRTHRawSource::init_source(std::filesystem::path source_file) {
    #if defined(_WIN32)
        auto gzf = gzopen_w(source_file.c_str(), "r");
    #else
        auto gzf = gzopen(source_file.c_str(), "r");
    #endif
    if (gzf == nullptr) throw std::runtime_error(std::format("Failed to open gz file: {}", source_file.string()));
    auto shared_gzf = std::shared_ptr<struct gzFile_s>(gzf, [](gzFile f){gzclose(f);});
    return CSVSource{
        [shared_gzf, buff = std::array<char, 65536>()]() mutable -> std::string_view {
            int r = gzread(shared_gzf.get(), buff.data(), static_cast<unsigned int>(buff.size()));
            if (r > 0) return {buff.data(), static_cast<std::size_t>(r)};
            if (r == 0 && gzeof(shared_gzf.get())) return {};
            int errnum;
            const char *err = gzerror(shared_gzf.get(), &errnum);
            throw std::runtime_error(std::format("GZ error: {} - {}", errnum, err));
        },
    };
}

int TRTHRawSource::CSVSource::operator()() {
    if (cur_line.empty()) cur_line = block_reader();
    if (cur_line.empty()) return -1;
    unsigned char c = static_cast<unsigned char>(cur_line.front());
    cur_line.remove_prefix(1);
    return static_cast<int>(c);
}