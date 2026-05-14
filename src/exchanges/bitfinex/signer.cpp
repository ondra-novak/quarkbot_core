#include "signer.hpp"
#include <atomic>
#include <format>
#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace quarkbot {
namespace bitfinex {


    std::string Signer::signPayload(std::string_view payload) const {
        std::array<unsigned char, HMAC_MAX_MD_CBLOCK> digest;
        std::string out;

        unsigned int len = digest.size();
        HMAC(EVP_sha384(), _secret.data(), static_cast<int>(_secret.size()), 
            reinterpret_cast<const unsigned char *>(payload.data()),
            payload.size(), digest.data(), &len);
        out.resize(len*2);
        auto iter = out.begin();
        for (auto &x: digest) {
            auto h = x >> 4;
            auto l = x & 0xF;
            *iter++ = static_cast<char>(h<10?h+'0':h+'a'-10);
            *iter++ = static_cast<char>(l<10?l+'0':l+'a'-10);
        }
        return out;
    }

    Signer::RequestSigned Signer::signRequest(std::string_view api_path, std::string_view body) {
        auto n = _nonce.fetch_add(1, std::memory_order_relaxed);
        std::string payload = std::format("/api/v2/{}{}{}",api_path,n,body);
        return {signPayload(payload),n};
    }

    Signer::ChannelSigned Signer::signChannel() {
        auto n = _nonce.fetch_add(1, std::memory_order_relaxed);
        std::string payload = std::format("AUTH{}", n);
        return {{signPayload(payload),n},payload};
    }


}}