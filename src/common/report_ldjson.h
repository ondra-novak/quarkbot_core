#pragma once
#include "storage.h"

#include <json20.h>

namespace quarkbot {


class ReportLDJSONBase: public IStorage {
public:


    ReportLDJSONBase(std::unique_ptr<IStorage>storage, std::string strategy_name)
        :_storage(std::move(storage))
        ,_strategy_name(std::move(strategy_name)) {}

    ReportLDJSONBase(const ReportLDJSONBase &) =delete;
    ReportLDJSONBase &operator=(const ReportLDJSONBase &) =delete;

    virtual void rollback() override;
    virtual std::string get_var(std::string_view var_name) const override;
    virtual void begin_transaction() override;
    virtual void commit() override;
    virtual Fills load_fills(std::size_t limit,
            std::string_view filter) const override;
    virtual void put_var(Timestamp tm, std::string_view name, std::string_view value)
            override;
    virtual bool is_duplicate_fill(const Fill &fill) const override;
    virtual VarSet<std::string_view> get_vars(std::string_view prefix) const override;
    virtual VarSet<std::string_view> get_vars(std::string_view start, std::string_view end) const override;
    virtual void put_fill(Timestamp tm,const Fill &fill) override;
    virtual void erase_var(Timestamp tm,std::string_view name) override;
    virtual Fills load_fills(Timestamp limit,
            std::string_view filter) const override;
    virtual Positions load_positions(std::string_view filter) const
            override;
    virtual std::vector<SerializedOrder> load_open_orders(
            const Account &account) const override;
    virtual Trades load_closed(Timestamp limit,
            std::string_view filter) const override;
    virtual void put_order(Timestamp tm,const Order &ord) override;
    virtual void series_erase_points(std::string_view series_name, uint64_t index_and_less) override;
    virtual uint64_t series_add_point(std::string_view series_name, std::string_view point_data) override;
    virtual ValueStream<std::string_view> load_series(std::string_view name) const override;

protected:

    std::unique_ptr<IStorage>_storage;
    std::string _strategy_name;
    std::vector<json::value> _tx;
    void tx_beg();
    void tx_end();
    void tx_rollback();
    unsigned int _txcnt = 0;
    virtual void flush() = 0;

    json::value new_record(Timestamp tm, std::string_view type, json::value payload);
    static json::value order_to_json(const Order &ord);
};

template<std::invocable<std::string_view> Output>
class ReportLDJson: public ReportLDJSONBase {
public:

    ReportLDJson(std::unique_ptr<IStorage>storage, std::string strategy_name, Output output)
        :ReportLDJSONBase(std::move(storage), std::move(strategy_name)), _output(output) {}

protected:
    Output _output;
    std::vector<char> _buffer;
    json::serializer_t _srl;

    virtual void flush() {
        for (json::value_t &x: _tx) {
            _buffer.clear();
            _srl.serialize(x, [&](std::string_view str){
                auto sz = _buffer.size();
                _buffer.resize(sz + str.size());
                std::copy(str.begin(), str.end(), _buffer.data()+sz);
            });
            _output(std::string_view(_buffer.data(), _buffer.size()));
        }
    }
};

template<typename Lock = NoLock>
class ReportOutput_ostream {
public:
    ReportOutput_ostream(std::ostream &s):_s(s) {}
    ReportOutput_ostream(const ReportOutput_ostream &other):_s(other._s) {}
    ReportOutput_ostream &operator=(const ReportOutput_ostream &other) = delete;

    void operator()(const std::string_view &s) const {
        std::lock_guard _(_mx);
        _s << s << std::endl;
    }

protected:
    std::ostream &_s;
    Lock _mx;
};

template<typename Lock = NoLock>
using ReportLDJsonToStream = ReportLDJson<ReportOutput_ostream<Lock> >;

}
