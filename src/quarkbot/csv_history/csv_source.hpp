#pragma once

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <zlib.h>
namespace quarkbot {


    struct RawCSVSource {
        std::ifstream f;
        RawCSVSource(std::filesystem::path fname):f(fname) {
            if (!f) throw std::runtime_error(std::format("Failed to open {}", fname.string()));            
        }
        int operator()() {
            return f.get();
        };
    };

    struct CompressedCSVSource {       
        struct GZFDeleter {
            void operator()(gzFile f) {
                gzclose(f);
            }
        };
        struct Inner {
            std::unique_ptr<gzFile_s> _gzf;
            std::size_t used = 0;
            std::size_t pos = 0;
            std::array<char, 65536> buffer = {};
        };
        std::unique_ptr<Inner> inner;


        CompressedCSVSource(std::filesystem::path source_file) {
            #if defined(_WIN32)
                auto gzf = gzopen_w(source_file.c_str(), "r");
            #else
                auto gzf = gzopen(source_file.c_str(), "r");
            #endif
            if (gzf == nullptr) throw std::runtime_error(std::format("Failed to open gz file: {}", source_file.string()));
            inner = std::make_unique<Inner>(Inner{{gzf,{}}});
        }
        int operator()() {
            if (inner->pos >= inner->used) {
                int r = gzread(inner->_gzf.get(), inner->buffer.data(), static_cast<unsigned int>(inner->buffer.size()));    
                if (r > 0) {
                    inner->used = static_cast<std::size_t>(r);
                    inner->pos = 0;                    
                } else {
                    if (gzeof(inner->_gzf.get())) return EOF;
                    int errnum;
                    const char *err = gzerror(inner->_gzf.get(), &errnum);
                    throw std::runtime_error(std::format("GZ error: {} - {}", errnum, err));
                }
            }
            return inner->buffer[inner->pos++];
        }
    };


    template<typename _Callback> 
    auto prepare_csv_source(const std::filesystem::path &path, _Callback callback) {
        auto ext = path.extension().string();
        if (ext == ".gz") {
           return callback(CompressedCSVSource{path});
        } else {
           return callback(RawCSVSource{{path}});
        } 
    }


}