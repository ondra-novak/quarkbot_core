#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
namespace quarkbot {
namespace bitfinex {

    class Signer {
    public:
        Signer (std::string api_key,std::string secret)
            :_api_key(std::move(api_key))
            ,_secret(std::move(secret))
            ,_nonce(static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(
                    std::chrono::system_clock::now()) - 1778762645L)) {}

        struct RequestSigned {
            std::string sign_text;
            std::uint64_t nonce;
        };
        struct ChannelSigned: RequestSigned {
            std::string authPayload;
        };

        std::string signPayload(std::string_view payload) const;
        RequestSigned signRequest(std::string_view api_path, std::string_view body);
        ChannelSigned signChannel();
        const std::string &get_api_key() const;
        

    protected:
        std::string _api_key;
        std::string _secret;
        std::atomic<std::uint64_t> _nonce; 

    };

}
}