#include "memory_storage.h"
#include <quarkbot/mq.h>

#include "position_info.h"

namespace quarkbot {


void MemoryStorage::begin_transaction()
{
    _txlevel++;
}

void MemoryStorage::put_var(Timestamp,std::string_view name, std::string_view value)
{
    _tx.push_back(TxVar{std::string(name), std::string(value)});
    auto_commit();
}


void MemoryStorage::erase_var(Timestamp,std::string_view name)
{
    _tx.push_back(TxVar{std::string(name), std::nullopt});
    auto_commit();
}

void MemoryStorage::put_order(Timestamp,const Order &ord)
{
    auto b = ord.to_binary();
    _tx.push_back(TxOrder{ord.get_account(),std::move(b), ord.done()});
    auto_commit();
}



void MemoryStorage::put_fill(Timestamp,const Fill &fill)
{
    _tx.push_back(fill);
    auto_commit();
}

struct MemoryStorage::StoreAction {
    MemoryStorage *me;
    void operator()(TxOrder &ord) const {
        auto &lst = me->_orders[ord.acc];
        if (ord.erase) lst.erase(ord.ord.order_id);
        else lst[std::move(ord.ord.order_id)] = std::move(ord.ord.order_id);
    }
    void operator()(TxVar &var) const {
        if (var.value.has_value()) {
            me->_variables[std::move(var.key)] = std::move(*var.value);
        } else {
            me->_variables.erase(var.key);
        }
    }
    void operator()(Fill &fill) const {
        me->_fills.push_back(std::move(fill));
    }
};

void MemoryStorage::commit()
{
    if (--_txlevel <= 0) {
        if (!_batch_rollback) {
            std::lock_guard _(_mx);
            for (auto &x : _tx) {
                std::visit(StoreAction{this}, x);
            }
        _tx.clear();
        _txlevel = 0;
        _batch_rollback = false;
        }
    }
}

void MemoryStorage::rollback() {
    _batch_rollback = true;
    commit();
}

bool MemoryStorage::is_duplicate_fill(const Fill &fill) const
{
    std::shared_lock _(_mx);
    auto iter = _fills.rbegin();
    auto end = _fills.rend();
    while (iter != end) {
        if (iter->time < fill.time) return false;
        if (iter->id == fill.id) return true;
        ++iter;
    }
    return false;
}


Fills MemoryStorage::load_fills(std::size_t limit, std::string_view filter) const
{
    std::shared_lock _(_mx);
    Fills fills;
    auto iter = _fills.rbegin();
    auto end = _fills.rend();
    while (iter != end) {
        if (filter.empty() || std::string_view(iter->label).substr(0, filter.size()) == filter) {
            fills.push_back(*iter);
            if (fills.size() >= limit) break;
        }
        ++iter;
    }
    return fills;
}

Fills MemoryStorage::load_fills(Timestamp limit, std::string_view filter) const
{
    std::shared_lock _(_mx);
    Fills fills;
    auto iter = _fills.rbegin();
    auto end = _fills.rend();
    while (iter != end && iter->time >= limit) {
        if (filter.empty() || std::string_view(iter->label).substr(0, filter.size()) == filter) {
            fills.push_back(*iter);
        }
        ++iter;
    }
    return fills;

}

std::vector<SerializedOrder> MemoryStorage::load_open_orders(const Account &acc) const
{
    std::shared_lock _(_mx);
    std::vector<SerializedOrder> out;
    auto i1 = _orders.find(acc);
    if (i1 != _orders.end()) {
        for (const auto &[k,v]: i1->second) {
            out.push_back({k,v});
        }
    }
    return out;

}

std::string MemoryStorage::get_var(std::string_view var_name) const
{
    std::shared_lock _(_mx);
    auto iter = _variables.find(var_name);
    if (iter != _variables.end()) return iter->second;
    else return {};
}

class MemoryStorage::VarSetDef: public IVarSet {
public:
    VarSetDef(VarMap::const_iterator beg,VarMap::const_iterator end,std::shared_lock<std::shared_mutex> lk)
        :beg(std::move(beg))
        ,end(std::move(end))
        ,lk(std::move(lk)) {

    }

    virtual bool init() {
        cur = beg;
        return cur != end;
    }
    virtual bool next() {
        ++cur;
        return cur != end;
    }
    virtual std::pair<std::string_view, std::string_view> get() const {
        return {std::string_view(cur->first), std::string_view(cur->second)};
    }

protected:

    VarMap::const_iterator beg;
    VarMap::const_iterator cur;
    VarMap::const_iterator end;
    std::shared_lock<std::shared_mutex> lk;
};

VarSet<std::string_view> MemoryStorage::get_vars(std::string_view start, std::string_view end) const
{
    std::shared_lock<std::shared_mutex> lk(_mx);
    return VarSet<std::string_view>(
            std::make_unique<VarSetDef>(
                    _variables.lower_bound(start),
                    _variables.upper_bound(end), std::move(lk)));

}

VarSet<std::string_view> MemoryStorage::get_vars(std::string_view prefix) const
{
    std::shared_lock<std::shared_mutex> lk(_mx);
    if (prefix.empty()) {
        return VarSet<std::string_view>(
                std::make_unique<VarSetDef>(
                        _variables.begin(), _variables.end(), std::move(lk)));
    } else {
        std::string s(prefix);
        while (!s.empty()) {
            auto c = static_cast<unsigned char>(s.back());
            s.pop_back();
            ++c;
            if (c) {
                s.push_back(static_cast<char>(c));
                break;
            }
        }
        return VarSet<std::string_view>(
                std::make_unique<VarSetDef>(
                        _variables.lower_bound(prefix),
                        _variables.lower_bound(s), std::move(lk)));

    }
}

Positions MemoryStorage::load_positions(std::string_view filter) const
{
    PositionInfoMap positions;
    std::shared_lock<std::shared_mutex> lk(_mx);
    for (const auto &f: _fills) {
        auto &nfo = positions[f.pos_id];
        nfo.add_fill(f);
    }

    return positions.export_open_positions(filter);
}

Trades MemoryStorage::load_closed(Timestamp limit, std::string_view filter) const
{
    Trades res;
    PositionInfoMap positions;
    std::shared_lock<std::shared_mutex> lk(_mx);

    for (const auto &f: _fills) {
        auto &nfo = positions[f.pos_id];
        auto trd = nfo.add_fill(f);
        if (trd.has_value()) {
            std::string_view label= trd->label;
            if (filter.empty() || label.substr(0,filter.size()) == filter) {
                if (trd->last_update_time >= limit) {
                    res.push_back(std::move(*trd));
                }
            }
        }
    }
    return res;
}

void MemoryStorage::series_erase_points(std::string_view series_name, uint64_t index_and_less) {
    auto iter = _series.find(std::string(series_name));
    if (iter != _series.end()) {
        Series &s = iter->second;
        std::uint64_t ql = s.index - index_and_less - 1;
        while (s.data.size() > ql) s.data.pop_front();
    }
}

uint64_t MemoryStorage::series_add_point(std::string_view series_name, std::string_view point_data) {
    Series &s = _series[std::string(series_name)];
    std::uint64_t r = s.index++;
    s.data.emplace_back(point_data);
    return r;
}


class MemoryStorage::ValueSetDef: public IValueStream {
public:

    ValueSetDef(std::vector<std::string> data):_data(data) {}

    virtual bool next() override {++pos;return pos < _data.size();}
    virtual bool init() override {pos =0;return pos < _data.size();}
    virtual std::string_view get() const override {return _data[pos];}

protected:
    std::vector<std::string> _data;
    std::size_t pos = 0;

};

ValueStream<std::string_view> MemoryStorage::load_series(std::string_view series_name) const {
    auto iter = _series.find(std::string(series_name));
    if (iter != _series.end()) {
        const Series &s = iter->second;
        return ValueStream<std::string_view>(std::make_unique<ValueSetDef>(
                std::vector<std::string>(s.data.begin(), s.data.end())));
    } else {
        return ValueStream<std::string_view>(std::make_unique<ValueSetDef>(
                std::vector<std::string>()));
    }

}

}

