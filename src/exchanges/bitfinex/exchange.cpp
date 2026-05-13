#include "exchange.hpp"
#include "libs/network/rest.hpp"
#include "streaming.hpp"


namespace quarkbot {
namespace bitfinex {


Exchange::Exchange(NetworkContext sslctx, PExecutionWorker worker)
    :_stream_manager(std::make_shared<StreamManager>(sslctx,worker))              
    ,_instr_map(sslctx)
    ,_worker(worker) 
 {


 }

 void Exchange::report_price(const std::string &id, Decimal price) {
    _instr_map.report_price(id, price);
 }

std::unique_ptr<IEventStreamBase> Exchange::subscribe_market_stream(std::string symbol, StreamTypeItem::Type type, const StreamParams *params) {
    return _stream_manager->subscribe(symbol, type, params, weak_from_this());    
}

PAccount Exchange::create_account([[maybe_unused]]const std::string &name, [[maybe_unused]]const std::string &credentials) {
    return nullptr ;//todo

}
std::vector<PMarketInstrument> Exchange::get_market_instruments() {
    return _instr_map.get_all_instruments(shared_from_this());
}
PMarketInstrument Exchange::create_instrument(std::string_view id, InstrumentType type) {
    return _instr_map.create_instrument(id, type, shared_from_this());
}
std::vector<UnderlyingCurrency> Exchange::get_all_currencies() {
    return _instr_map.get_all_currencies(shared_from_this());
}
std::string_view Exchange::get_name() const{
    return "Bitfinex";

}


}
}
