#include "mq.h"

#include "leveldb_storage.h"
#include "position_info.h"

namespace quarkbot {

LvlDBStorage::LvlDBStorage(std::shared_ptr<leveldb::DB> db, std::string key_pfx)
:_db(std::move(db)),_key_pfx(std::move(key_pfx)) {}



void LvlDBStorage::begin_transaction()
{
    _txlevel++;
}

void LvlDBStorage::put_var(Timestamp,std::string_view name, std::string_view value)
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

const std::string& LvlDBStorage::build_series_key(const std::string_view &name, std::uint64_t index) {
    return build_series_key(_buffer, name, index);
}

const std::string& LvlDBStorage::build_series_key(std::string &buffer, const std::string_view &name, std::uint64_t index) const {
    build_key(buffer, RecordType::series, "");
    buffer.append(name);
    buffer.push_back('\0');
    for (int i = 0; i < 8; ++i) {
        char c = static_cast<char>(index >> ((7-i)*8));
        buffer.push_back(c);
    }
    return buffer;
}

const std::string& LvlDBStorage::build_series_key(std::string &buffer, const std::string_view &name) const {
    build_key(buffer, RecordType::series, "");
    buffer.append(name);
    buffer.push_back('\0');
    return buffer;
}
void LvlDBStorage::erase_var(Timestamp,std::string_view name)
{
    _batch.Delete(build_key(RecordType::variable, name));
}

using OrderKey = TupleBin<std::string_view, std::string_view>;
using OrderKeyPrefix = TupleBin<std::string_view>;

void LvlDBStorage::put_order(Timestamp,const Order &ord)
{
    auto b = ord.to_binary();
    std::string account_id = ord.get_account().get_id();
    std::string key = OrderKey::compose(account_id, b.order_id);
    if (ord.done()) {
        _batch.Delete(build_key(RecordType::order,key));
    } else {
        _batch.Put(build_key(RecordType::order, key), b.order_content);
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

void LvlDBStorage::put_fill(Timestamp,const Fill &fill)
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
        } else {
            while (_series_state_rollback_data.empty()) {
                const auto &item = _series_state_rollback_data.back();
                *item.first = item.second;
                _series_state_rollback_data.pop_back();
            }
        }

        _txlevel = 0;
        _batch.Clear();
        _series_state_rollback_data.clear();
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

std::vector<SerializedOrder> LvlDBStorage::load_open_orders(const Account &account) const
{
    std::vector<SerializedOrder> ret;
    std::unique_ptr<leveldb::Iterator> iter (_db->NewIterator({}));
    std::string s;
    build_key(s,RecordType::order,"");
    Serializer::to_binary(std::back_inserter(s), account.get_id());
    iter->Seek(s);
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


Positions LvlDBStorage::load_positions(std::string_view filter) const
{
    PositionInfoMap positions;

    std::string k;
    std::unique_ptr<leveldb::Iterator> iter(_db->NewIterator({}));
    iter->Seek(build_key(k, RecordType::fill, {}));
    while (iter->Valid() && key_match_prefix(k, iter->key())) {
        Fill f = restore_fill(FillRecord::parse(extract_slice(iter->value())));
        auto &nfo = positions[f.pos_id];
        nfo.add_fill(f);
    }

    return positions.export_open_positions(filter);
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

ValueStream<std::string_view> LvlDBStorage::load_series(std::string_view name) const {

    class Stream: public IValueStream {
    public:
        Stream(std::string pfx, leveldb::DB *db)
            :prefix(std::move(pfx))
            ,iter(db->NewIterator({})) {}
        virtual bool next() override {
            iter->Next();
            return is_valid();
        }
        virtual bool init() override {
            iter->Seek(prefix);
            return is_valid();
        }
        virtual std::string_view get() const override {
            return extract_slice(iter->value());
        }
        bool is_valid() const {
            return iter->Valid() && iter->key().starts_with(prefix);
        }
    protected:
        std::string prefix;
        std::unique_ptr<leveldb::Iterator> iter;
    };

    std::string pfx;
    build_series_key(pfx,name);
    return ValueStream<>(std::make_unique<Stream>(std::move(pfx), _db.get()));
}

LvlDBStorage::SeriesState &LvlDBStorage::get_series_state(const std::string &name) {
    auto iter = _series_state.find(name);
    if (iter == _series_state.end()) {
        auto r = _series_state.emplace(name, load_series_state_from_db(name));
        iter = r.first;
    }
    return iter->second; // @suppress("Returning the address of a local variable")
}

uint64_t LvlDBStorage::series_add_point(std::string_view series_name, std::string_view point_data) {
    SeriesState &st = get_series_state(std::string(series_name));
    _series_state_rollback_data.emplace_back(&st, st);
    auto idx = st.first_point++;
    build_series_key(series_name, idx);
    _batch.Put(_buffer, {point_data.data(), point_data.size()});
    return idx;
}

void LvlDBStorage::series_erase_points(std::string_view series_name, uint64_t index_and_less) {
    SeriesState &st = get_series_state(std::string(series_name));
    if (st.first_point < index_and_less) return;
    if (st.last_point > index_and_less) return;
    _series_state_rollback_data.emplace_back(&st, st);
    for (auto idx = st.last_point; idx <= index_and_less; ++idx) {
        build_series_key(series_name, idx);
        _batch.Delete(_buffer);
    }
    st.last_point = index_and_less+1;
}

static std::pair<std::string, std::string> findFirstAndLastKeyInRange(leveldb::DB* db, const std::string& range_start, const std::string& range_end) { // @suppress("Name convention for function")
    std::string first_key;
    std::string last_key;

    std::unique_ptr<leveldb::Iterator> it (db->NewIterator({}));

    it->Seek(range_start);   //seek at beginning of the range
    //test whether first item is in range
    if (it->Valid() && it->key().compare(range_end)<=0) {first_key = it->key().ToString();}
    it->Seek(range_end);   //seek after end of the range
    if (!it->Valid()) it->SeekToLast(); else it->Prev(); //go one item back
    //test, whether last item is in range.
    if (it->Valid() && it->key().compare(range_start)>=0) {last_key = it->key().ToString();}

    return std::make_pair(first_key, last_key);
}


LvlDBStorage::SeriesState LvlDBStorage::load_series_state_from_db(std::string_view name) const {
    std::string range_start;
    std::string range_end;
    build_series_key(range_start, name, 0);
    build_series_key(range_end, name, -1);
    auto r = findFirstAndLastKeyInRange(_db.get(), range_start, range_end);
    if (r.first.empty()) return {};

    auto parse_uint = [](std::string_view text){
        std::uint64_t r = 0;
        for (char c: text) {
            r = (r << 8) | static_cast<unsigned char>(c);
        }
        return r;
    };

    auto first_str = remove_key_prefix(r.first).substr(name.size()+1);
    auto last_str = remove_key_prefix(r.second).substr(name.size()+1);
    return {parse_uint(first_str),parse_uint(last_str)};
}



std::string_view LvlDBStorage::RecordType::to_string() const {
    switch (_val){
        case variable: return "var";
        case order: return "order";
        case fill: return "fill";
        case series: return "series";
        default: return std::string_view(reinterpret_cast<const char *>(&_val),1);
    }
}

}


