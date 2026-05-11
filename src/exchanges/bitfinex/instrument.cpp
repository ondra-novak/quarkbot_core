#include "instrument.hpp"
#include "exchange.hpp"
#include "market_instrument.hpp"
#include "types.hpp"
#include <memory>
#include <mutex>
#include <stdexcept>

namespace quarkbot {
namespace bitfinex {

    std::shared_ptr<BFXInstrument::InstrumentInfoPublisher> BFXInstrument::get_or_create_info_publiser() {
        std::scoped_lock _(_mx);
        auto p = _info_publisher.lock();
        if (!p)  {
            p = std::make_shared<InstrumentInfoPublisher>();
            _info_publisher = p;
            p->publish(InstrumentInfo::from(_cur_info));
        }
        return p;
    }

    std::unique_ptr<IEventStreamBase> BFXInstrument::subscribe_stream_internal(std::string_view type, const StreamParams *params) {
        if (type == InstrumentInfo::type) {
            std::shared_ptr<InstrumentInfoPublisher> p = get_or_create_info_publiser();
            return  p->create_subscriber(p);            
        }
        return std::static_pointer_cast<Exchange>(_owner)->subscribe_market_stream(_cur_info.name, type, params);
    }

    void BFXInstrument::info_updated(const InstrumentInfo &info) {
        std::shared_ptr<InstrumentInfoPublisher> p;
        {
            std::scoped_lock _(_mx);
            p = _info_publisher.lock();
            info.apply(_cur_info);
        }
        if (p) p->publish(info);
    }

    awaitable<PTradableInstrument> BFXInstrument::create_tradable_instrument(PAccount ) {
        throw std::runtime_error("create_tradable_instrument: not implemented");
    }

}
}