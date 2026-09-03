#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <zlib.h>
namespace quarkbot {


    ///Reads an uncompressed file character by character (source for CSVReader)
    struct RawCSVSource {
        std::ifstream f;
        RawCSVSource(const std::filesystem::path &fname):f(fname) {
            if (!f) throw std::runtime_error(std::format("Failed to open {}", fname.string()));
        }
        int operator()() {
            return f.get();
        };
    };

    ///Reads a gzip compressed file character by character (source for CSVReader)
    struct CompressedCSVSource {
        struct GZFDeleter {
            void operator()(gzFile f) const {
                gzclose(f);
            }
        };
        struct Inner {
            ///note - the deleter must be specified, gzFile_s is an opaque type owned by zlib
            std::unique_ptr<gzFile_s, GZFDeleter> _gzf;
            std::size_t used = 0;
            std::size_t pos = 0;
            std::array<char, 65536> buffer = {};
        };
        std::unique_ptr<Inner> inner;


        CompressedCSVSource(const std::filesystem::path &source_file) {
            #if defined(_WIN32)
                auto gzf = gzopen_w(source_file.c_str(), "rb");
            #else
                auto gzf = gzopen(source_file.c_str(), "rb");
            #endif
            if (gzf == nullptr) throw std::runtime_error(std::format("Failed to open gz file: {}", source_file.string()));
            inner = std::make_unique<Inner>();
            inner->_gzf.reset(gzf);
        }
        int operator()() {
            if (inner->pos >= inner->used) {
                int r = gzread(inner->_gzf.get(), inner->buffer.data(), static_cast<unsigned int>(inner->buffer.size()));
                if (r > 0) {
                    inner->used = static_cast<std::size_t>(r);
                    inner->pos = 0;
                } else {
                    if (r == 0 || gzeof(inner->_gzf.get())) return EOF;
                    int errnum;
                    const char *err = gzerror(inner->_gzf.get(), &errnum);
                    throw std::runtime_error(std::format("GZ error: {} - {}", errnum, err));
                }
            }
            //CSVReader expects characters read as unsigned, EOF (-1) is the only negative value
            return static_cast<unsigned char>(inner->buffer[inner->pos++]);
        }
    };


    ///Opens the file as .gz or as plain text (by extension) and passes the source to the callback
    template<typename _Callback>
    auto prepare_csv_source(const std::filesystem::path &path, _Callback callback) {
        auto ext = path.extension().string();
        if (ext == ".gz") {
           return callback(CompressedCSVSource(path));
        } else {
           return callback(RawCSVSource(path));
        }
    }


}
