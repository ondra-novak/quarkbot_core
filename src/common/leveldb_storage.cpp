#include "leveldb_storage.h"
#include "../trading_ifc/mq.h"

#include <unordered_map>

namespace trading_api {


void LvlDBStorage::begin_transaction()
{
    _txlevel++;
}

void LvlDBStorage::put_var(std::string_view name, std::string_view value)
{
    _batch.Put(build_key(RecordType::variable, name), {value.data(), value.size()});
    auto_commit();
}

const std::string &LvlDBStorage::build_key(std::string &buffer, RecordType type, const std::string_view &rest) const
{
    buffer.clear();
    buffer.append(_key_pfx);
    buffer.push_back(type);
    buffer.append(rest);
    return buffer;
}



const std::string &LvlDBStorage::build_key(RecordType type, const std::string_view &rest)
{
    return build_key(_buffer, type, rest);
}

const std::string &LvlDBStorage::build_fill_key(Timestamp tm, std::string_view id)
{
    return build_fill_key(_buffer, tm, id);
}

const std::string &LvlDBStorage::build_fill_key(std::string &buffer, Timestamp tm, std::string_view id) const
{
    build_key(buffer, RecordType::fill, "");
    auto tmn = tm.time_since_epoch().count();
    for (int i = 0; i < 8; ++i) {
        char c = static_cast<char>(tmn >> ((7-i)*8));
        buffer.push_back(c);
    }
    buffer.append(id);
    return buffer;
}

void LvlDBStorage::erase_var(std::string_view name)
{
    _batch.Delete(build_key(RecordType::variable, name));
}

void LvlDBStorage::put_order(const Order &ord)
{
    auto b = ord.to_binary();
    if (ord.done()) {
        _batch.Delete(build_key(RecordType::order,b.order_id));
    } else {
        _batch.Put(build_key(RecordType::order, b.order_id), b.order_content);
    }
    auto_commit();
}


using FillRecord = TupleBin<
    Timestamp, //time
    std::string, //id
    std::string,// label;
    std::string,// pos_id;
    IInstrument::Type, //instrument.type;
    Decimal,// multiplier;
    std::string, // instrument_id;
    std::string, // price_unit;
    Side,// side;
    Decimal,// amount;
    Decimal,// price;
    double// fees
>;

void LvlDBStorage::put_fill(const Fill &fill)
{
    std::string v = FillRecord::compose(fill.time, fill.id, fill.label, fill.pos_id,
                    fill.instrument.type, fill.instrument.multiplier,
                    fill.instrument.instrument_id, fill.instrument.price_unit,
                    fill.side, fill.amount, fill.price, fill.fees);
    _batch.Put(build_fill_key(fill.time, fill.id), v);
    auto_commit();
}

void LvlDBStorage::commit()
{
    if (--_txlevel <= 0) {
        if (!_batch_rollback) {
            leveldb::Status s = _db->Write(_write_opts, &_batch);
            if (!s.ok()) throw s;
        }
        _txlevel = 0;
        _batch.Clear();
        _batch_rollback = false;
    }
}

void LvlDBStorage::rollback() {
    _batch_rollback = true;
    commit();
}

bool LvlDBStorage::is_duplicate_fill(const Fill &fill) const
{
    std::string k;
    build_fill_key(k, fill.time, fill.id);
    auto s = _db->Get({},k,&k);
    return s.ok();
}

static Fill restore_fill(decltype(FillRecord::parse("")) &&parsed) {
        return Fill{
                std::move(std::get<0>(parsed)),
                std::move(std::get<1>(parsed)),
                std::move(std::get<2>(parsed)),
                std::move(std::get<3>(parsed)),
                {
                    std::move(std::get<4>(parsed)),
                    std::move(std::get<5>(parsed)),
                    std::move(std::get<6>(parsed)),
                    std::move(std::get<7>(parsed)),
                },
                std::move(std::get<8>(parsed)),
                std::move(std::get<9>(parsed)),
                std::move(std::get<10>(parsed)),
                std::move(std::get<11>(parsed))
            };
}

Fills LvlDBStorage::load_fills(std::size_t limit, std::string_view filter) const
{
    Fills fills;
    std::string k;
    build_fill_key(k, std::chrono::system_clock::time_point::max(), {});
    std::unique_ptr<leveldb::Iterator> iter(_db->NewIterator({}));
    iter->Seek(k);
    if (!iter->Valid()) iter->SeekToLast();
    build_key(k, RecordType::fill,{});
    while (limit> 0 && iter->Valid() && key_match_prefix(k, iter->key())) {
        auto parsed = FillRecord::parse(extract_slice(iter->value()));
        const std::string &label = std::get<2>(parsed);
        if (filter.empty() || std::string_view(label).substr(0, filter.size()) == filter) {
            --limit;
            fills.push_back(restore_fill(std::move(parsed)));
        }
        iter->Prev();
    }
    return fills;
}

Fills LvlDBStorage::load_fills(Timestamp limit, std::string_view filter) const
{
    Fills fills;
    std::string k;
    std::unique_ptr<leveldb::Iterator> iter(_db->NewIterator({}));
    iter->Seek(build_fill_key(k, limit, {}));
    build_key(k, RecordType::fill,{});
    while (iter->Valid() && key_match_prefix(k, iter->key())) {
        auto parsed = FillRecord::parse(extract_slice(iter->value()));
        const std::string &label = std::get<2>(parsed);
        if (filter.empty() || std::string_view(label).substr(0, filter.size()) == filter) {
            fills.push_back(restore_fill(std::move(parsed)));
        }
        iter->Next();
    }
    std::reverse(fills.begin(), fills.end());
    return fills;

}

std::vector<SerializedOrder> LvlDBStorage::load_open_orders() const
{
    std::vector<SerializedOrder> ret;
    std::unique_ptr<leveldb::Iterator> iter (_db->NewIterator({}));
    std::string s;
    iter->Seek(build_key(s,RecordType::order,""));
    while (iter->Valid() && key_match_prefix(s,iter->key()))  {
        ret.push_back({std::string(remove_key_prefix(iter->key())), std::string(extract_slice(iter->value()))});
        iter->Next();
    }
    return ret;
}

std::string LvlDBStorage::get_var(std::string_view var_name) const
{
    std::string s;
    build_key(s, RecordType::variable, var_name);
    if (_db->Get({},s,&s).ok()) return s;
    return {};
}

VarSet<std::string_view> LvlDBStorage::get_vars(std::string_view start, std::string_view end) const
{
    class Set: public IVarSet {
    public:

        Set(std::string start, std::string end, leveldb::DB *db, int prefix_size)
            :start(std::move(start))
            ,end(std::move(end))
            ,iter(db->NewIterator({}))
            ,prefix_size(prefix_size) {}

        virtual bool init() {
            iter->Seek(start);
            return is_valid();
        }
        virtual bool next() {
            iter->Next();
            return is_valid();
        }
        virtual std::pair<std::string_view, std::string_view> get() const {
            auto key = iter->key();
            key.remove_prefix(prefix_size);
            auto value = iter->value();
            return {{key.data(), key.size()}, {value.data(), value.size()}};
        }

        bool is_valid() const {
            return iter->Valid() && iter->key().compare(end) <= 0;
        }

    protected:
        std::string start;
        std::string end;
        std::unique_ptr<leveldb::Iterator> iter;
        int prefix_size;
    };


    std::string b;
    std::string e;
    build_key(b, RecordType::variable, start);
    build_key(e, RecordType::variable, end);
    return VarSet<>(std::make_unique<Set>(std::move(b), std::move(e), _db.get(), _key_pfx.size()+1));
}

VarSet<std::string_view> LvlDBStorage::get_vars(std::string_view prefix) const
{
    class Set: public IVarSet {
    public:

        Set(std::string pfx, leveldb::DB *db, int prefix_size)
            :pfx(std::move(pfx))
            ,iter(db->NewIterator({}))
            ,prefix_size(prefix_size) {}

        virtual bool init() {
            iter->Seek(pfx);
            return is_valid();
        }
        virtual bool next() {
            iter->Next();
            return is_valid();
        }
        virtual std::pair<std::string_view, std::string_view> get() const {
            auto key = iter->key();
            key.remove_prefix(prefix_size);
            auto value = iter->value();
            return {{key.data(), key.size()}, {value.data(), value.size()}};
        }

        bool is_valid() const {
            return iter->Valid() && iter->key().starts_with(pfx);
        }

    protected:
        std::string pfx;
        std::unique_ptr<leveldb::Iterator> iter;
        int prefix_size;
    };

    std::string pfx;
    build_key(pfx, RecordType::variable, prefix);
    return VarSet<>(std::make_unique<Set>(std::move(pfx), _db.get(), _key_pfx.size()+1));
}

struct PositionInfo {
    Side side = Side::undefined;      //current side
    Decimal pos = {};    //current position
    double sum = 0;
    Fill last_fill = {};
    double fees = 0;

    std::optional<Trade> add_fill(const Fill &f) {
        std::optional<Trade> out;
        last_fill = f;
        double fp;
        if (f.instrument.type == Instrument::Type::inverted_contract) {
            fp = 1.0/f.price.as<double>();
        } else {
            fp =f.price.as<double>();
        }
        fees += f.fees;
        if (f.side == side) {
            pos += f.amount;
            sum += f.amount.as<double>() * fp;;
        } else {
            if (pos <= f.amount) {
                out.emplace(Trade {
                    f.time,f.id, f.label, f.pos_id, f.instrument,side, pos,
                    get_open_price(), f.price.as<double>(), fees
                });
                pos = f.amount - pos;
                side = f.side;
                sum = pos.as<double>() * fp;
                fees = 0;
            } else {
                out.emplace(Trade {
                    f.time,f.id, f.label, f.pos_id, f.instrument,side, f.amount,
                    get_open_price(), f.price.as<double>(), fees
                });
                auto newpos = pos + f.amount;
                double newavg = sum * newpos.as<double>() / pos.as<double>();
                pos -= f.amount;
                sum = newavg;
            }
        }
        return {};
    }

    double get_open_price() const {
        double avg  = sum / pos.as<double>();
        if (last_fill.instrument.type == Instrument::Type::inverted_contract) {
            return 1.0/avg;
        } else {
            return avg;
        }
    }
};

Positions LvlDBStorage::load_positions(std::string_view filter) const
{
    std::unordered_map<std::string, PositionInfo> positions;

    std::string k;
    std::unique_ptr<leveldb::Iterator> iter(_db->NewIterator({}));
    iter->Seek(build_key(k, RecordType::fill, {}));
    while (iter->Valid() && key_match_prefix(k, iter->key())) {
        Fill f = restore_fill(FillRecord::parse(extract_slice(iter->value())));
        auto &nfo = positions[f.pos_id];
        nfo.add_fill(f);
    }

    Positions res;
    for (const auto &[id, info]: positions) {
        if (info.pos == 0) {
            continue;
        }
        std::string_view f= info.last_fill.label;
        if (filter.empty() || f.substr(0,filter.size()) == filter) {
            res.push_back({
                info.last_fill.time,
                info.last_fill.id,
                info.last_fill.label,
                id,
                info.last_fill.instrument,
                info.side,
                info.pos,
                info.get_open_price(),
                info.fees,
            });
        }
    }

    return res;
}

Trades LvlDBStorage::load_closed(Timestamp limit, std::string_view filter) const
{
    Trades res;
    std::unordered_map<std::string, PositionInfo> positions;

    std::string k;
    std::unique_ptr<leveldb::Iterator> iter(_db->NewIterator({}));
    iter->Seek(build_key(k, RecordType::fill, {}));
    while (iter->Valid() && key_match_prefix(k, iter->key())) {
        Fill f = restore_fill(FillRecord::parse(extract_slice(iter->value())));
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

bool LvlDBStorage::key_match_prefix(const std::string_view & pfx, const leveldb::Slice & slice)
{
    std::string_view key (slice.data(), slice.size());
    return key.substr(0,pfx.size()) == pfx;
}


std::string_view LvlDBStorage::remove_key_prefix(const leveldb::Slice &slice) const
{
    return std::string_view(slice.data(), slice.size()).substr(_key_pfx.size()+1);
}
std::string_view LvlDBStorage::extract_slice(const leveldb::Slice &slice)
{
    return std::string_view(slice.data(), slice.size());
}
}
