#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

namespace npi {
using Id = uint32_t;
struct Item {
    Id owner = 0;
    int64_t category = 0, numeric = 0;
    uint64_t tags = 0;
    bool alive = false;
};
struct Query {
    size_t qidx = 0;
    int recursive = 0;
    Id anchor = 0;
    int64_t category = -1, lo = -1, hi = -1;
    int tagmode = 0;
    uint64_t tags = 0;
    int combine = 0;
};

namespace filter_detail {
[[noreturn]] inline void fail(const std::string& message) {
    throw std::runtime_error("neighborhood_filter: " + message);
}
template<class T> inline T integer(std::istream& in, const std::string& context) {
    static_assert(std::is_integral<T>::value, "integer type required");
    std::string token;
    if (!(in >> token)) fail("missing integer in " + context);
    T value{};
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc() || parsed.ptr != token.data() + token.size())
        fail("invalid/out-of-range integer '" + token + "' in " + context);
    return value;
}
inline void end(std::istream& in, const std::string& context) {
    std::string extra;
    if (in >> extra) fail("extra token in " + context + ": " + extra);
    if (!in.eof()) fail("read failure in " + context);
}
inline std::ifstream text(const std::string& path) {
    std::ifstream in(path);
    if (!in) fail("cannot open " + path);
    return in;
}
inline std::vector<float> floats(const std::string& path) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                  "native IEEE FP32 required");
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) fail("cannot open " + path);
    const auto length = in.tellg();
    if (length < 0) fail("cannot determine size of " + path);
    const auto bytes = static_cast<uint64_t>(length);
    if (bytes % sizeof(float) || bytes > std::numeric_limits<size_t>::max() ||
        bytes > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        fail("invalid FP32 file size in " + path);
    std::vector<float> values;
    if (bytes / sizeof(float) > values.max_size()) fail("FP32 file too large: " + path);
    values.resize(static_cast<size_t>(bytes / sizeof(float)));
    in.seekg(0);
    if (!in || (bytes && !in.read(reinterpret_cast<char*>(values.data()),
                                static_cast<std::streamsize>(bytes))))
        fail("short FP32 read in " + path);
    for (float v : values) if (!std::isfinite(v)) fail("nonfinite FP32 in " + path);
    return values;
}
inline bool extra(const Item& item, const Query& q) {
    bool any = false, both = true, either = false;
    auto add = [&](bool v) { any = true; both &= v; either |= v; };
    if (q.category != -1) add(item.category == q.category);
    if (q.lo != -1 || q.hi != -1) add(item.numeric >= q.lo && item.numeric <= q.hi);
    switch (q.tagmode) {
    case 1: add((item.tags & q.tags) == q.tags); break;
    case 2: add((item.tags & q.tags) != 0); break;
    case 3: add(item.tags == q.tags); break;
    case 4: add((item.tags & q.tags) == item.tags); break;
    default: break;
    }
    return !any || (q.combine ? either : both);
}
} // namespace filter_detail

struct Dataset {
    size_t n = 0, dim = 0, nq = 0;
    std::vector<float> base, tests;
    std::vector<Item> items;
    std::vector<int> parent;
    std::vector<uint32_t> tin, tout;
    static Dataset load(std::string directory);
    void rank_tree();
    bool eligible(Id id, const Query& q) const;
    size_t scope_size(const Query& q) const;
};

namespace filter_detail {
inline void validate(const Query& q, const Dataset& d) {
    if (q.qidx >= d.nq || q.anchor >= d.parent.size() ||
        (q.recursive != 0 && q.recursive != 1) || q.tagmode < 0 || q.tagmode > 4 ||
        (q.combine != 0 && q.combine != 1) ||
        ((q.lo != -1 || q.hi != -1) && q.lo > q.hi))
        fail("invalid query fields");
}
inline bool scope_walk(Id owner, const Query& q, const Dataset& d) {
    if (owner >= d.parent.size() || q.anchor >= d.parent.size()) return false;
    if (!q.recursive) return owner == q.anchor;
    size_t current = owner;
    for (size_t hops = 0; hops < d.parent.size(); ++hops) {
        if (current == q.anchor) return true;
        const int next = d.parent[current];
        if (next < 0) return false;
        if (static_cast<size_t>(next) >= d.parent.size()) fail("invalid current parent topology");
        current = static_cast<size_t>(next);
    }
    fail("cycle in current parent topology");
}
} // namespace filter_detail

inline void Dataset::rank_tree() {
    using filter_detail::fail;
    const size_t nodes = parent.size();
    if (!nodes || nodes > static_cast<size_t>(std::numeric_limits<int>::max()))
        fail("tree node count must be in [1, INT_MAX]");
    // Flat child links avoid recursion depth limits and one allocation per node.
    const uint32_t none = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> first(nodes, none), next(nodes, none);
    uint32_t root = none;
    for (size_t i = 0; i < nodes; ++i) {
        const int p = parent[i];
        if (p == -1) {
            if (root != none) fail("multiple tree roots");
            root = static_cast<uint32_t>(i);
        } else {
            if (p < 0 || static_cast<size_t>(p) >= nodes || static_cast<size_t>(p) == i)
                fail("invalid tree parent");
            next[i] = first[static_cast<size_t>(p)];
            first[static_cast<size_t>(p)] = static_cast<uint32_t>(i);
        }
    }
    if (root == none) fail("missing tree root");
    tin.assign(nodes, none);
    tout.assign(nodes, none);
    struct Frame { uint32_t node, child; };
    std::vector<Frame> stack;
    stack.push_back({root, first[root]});
    uint32_t clock = 0;
    tin[root] = clock++;
    while (!stack.empty()) {
        auto& frame = stack.back();
        if (frame.child == none) {
            tout[frame.node] = clock;
            stack.pop_back();
        } else {
            const uint32_t child = frame.child;
            frame.child = next[child];
            if (tin[child] != none) fail("cycle in tree");
            tin[child] = clock++;
            stack.push_back({child, first[child]});
        }
    }
    if (clock != nodes) fail("tree is disconnected or cyclic");
    for (const auto& item : items)
        if (item.owner >= nodes) fail("binding references nonexistent owner");
}

inline Dataset Dataset::load(std::string directory) {
    using namespace filter_detail;
    Dataset d;
    auto bindings = text(directory + "/bindings.txt");
    for (;;) {
        bindings >> std::ws;
        if (bindings.eof()) break;
        if (!bindings) fail("read failure in bindings.txt");
        if (d.items.size() == static_cast<size_t>(std::numeric_limits<Id>::max()))
            fail("binding count exceeds UINT32_MAX");
        Item item;
        item.owner = integer<Id>(bindings, "bindings.txt");
        d.items.push_back(item);
    }
    d.n = d.items.size();
    if (!d.n) fail("empty bindings.txt");
    d.base = floats(directory + "/base.f32");
    if (d.base.empty() || d.base.size() % d.n) fail("base.f32/bindings shape mismatch");
    d.dim = d.base.size() / d.n;
    d.tests = floats(directory + "/test.f32");
    if (d.tests.empty() || d.tests.size() % d.dim) fail("test.f32 dimension mismatch");
    d.nq = d.tests.size() / d.dim;
    const std::string meta_path = directory + "/meta.txt";
    std::error_code meta_error;
    const bool has_meta = std::filesystem::exists(meta_path, meta_error);
    if (meta_error) fail("cannot inspect " + meta_path + ": " + meta_error.message());
    if (has_meta) {
        auto meta = text(meta_path);
        const auto n = integer<size_t>(meta, "meta.txt");
        const auto dim = integer<size_t>(meta, "meta.txt");
        const auto nq = integer<size_t>(meta, "meta.txt");
        end(meta, "meta.txt");
        if (n != d.n || dim != d.dim || nq != d.nq) fail("meta.txt shape mismatch");
    }
    auto tree = text(directory + "/tree.txt");
    const size_t nodes = integer<size_t>(tree, "tree.txt");
    if (!nodes || nodes > static_cast<size_t>(std::numeric_limits<int>::max()))
        fail("tree node count must be in [1, INT_MAX]");
    d.parent.assign(nodes, -2);
    for (size_t i = 0; i < nodes; ++i) {
        const Id id = integer<Id>(tree, "tree.txt node");
        const int p = integer<int>(tree, "tree.txt parent");
        if (id >= nodes || d.parent[id] != -2 || p < -1 || p >= static_cast<int>(nodes))
            fail("invalid/duplicate tree row");
        d.parent[id] = p;
    }
    end(tree, "tree.txt");
    auto attrs = text(directory + "/attrs.txt");
    for (auto& item : d.items) {
        item.category = integer<int64_t>(attrs, "attrs.txt category");
        item.numeric = integer<int64_t>(attrs, "attrs.txt numeric");
        item.tags = integer<uint64_t>(attrs, "attrs.txt tags");
        const int alive = integer<int>(attrs, "attrs.txt alive");
        if (alive != 0 && alive != 1) fail("alive must be 0 or 1");
        item.alive = alive != 0;
    }
    end(attrs, "attrs.txt");
    d.rank_tree();
    return d;
}

inline bool Dataset::eligible(Id id, const Query& q) const {
    if (id >= items.size()) return false;
    const auto& item = items[id];
    return item.alive && filter_detail::scope_walk(item.owner, q, *this) &&
           filter_detail::extra(item, q);
}
inline size_t Dataset::scope_size(const Query& q) const {
    filter_detail::validate(q, *this);
    size_t count = 0;
    for (const auto& item : items)
        if (item.alive && filter_detail::scope_walk(item.owner, q, *this)) ++count;
    return count;
}
inline std::vector<Query> load_queries(std::string path, const Dataset& d) {
    using namespace filter_detail;
    auto in = text(path);
    const size_t count = integer<size_t>(in, path);
    std::vector<Query> queries;
    if (count > queries.max_size()) fail("query count too large");
    // Do not reserve an untrusted count before checking that its records exist.
    for (size_t i = 0; i < count; ++i) {
        std::string op;
        if (!(in >> op) || op != "Q") fail("query-only trace requires Q records");
        Query q;
        q.qidx = integer<size_t>(in, path);
        q.recursive = integer<int>(in, path);
        q.anchor = integer<Id>(in, path);
        q.category = integer<int64_t>(in, path);
        q.lo = integer<int64_t>(in, path);
        q.hi = integer<int64_t>(in, path);
        q.tagmode = integer<int>(in, path);
        q.tags = integer<uint64_t>(in, path);
        q.combine = integer<int>(in, path);
        validate(q, d);
        queries.push_back(q);
    }
    end(in, path);
    return queries;
}

struct QualStats {
    uint64_t point_tests = 0, point_cache_hits = 0, pages_prepared = 0,
             mask_words = 0, owner_tests = 0, owner_cache_hits = 0;
};

// Immutable dataset attributes/topology must outlive this qualifier. Rebuilding
// after a dataset mutation is intentional: this is the frozen E1/E2 interface.
class Qualifier {
    using Mask = std::array<uint64_t, 4>;
    struct Planes {
        Mask live{};
        std::array<Mask, 64> category{}, numeric{}, tags{};
    };
    const Dataset& data_;
    std::vector<Planes> planes_;
    std::vector<Mask> pages_;
    std::vector<uint64_t> page_epoch_, point_epoch_, owner_epoch_;
    std::vector<uint8_t> point_value_, owner_value_;
    Query query_{};
    QualStats* stats_ = nullptr;
    uint64_t epoch_ = 0;

    bool owner(Id id) {
        if (owner_epoch_[id] == epoch_) {
            ++stats_->owner_cache_hits;
            return owner_value_[id] != 0;
        }
        ++stats_->owner_tests;
        const bool value = query_.recursive
            ? data_.tin[query_.anchor] <= data_.tin[id] &&
              data_.tin[id] < data_.tout[query_.anchor]
            : id == query_.anchor;
        owner_epoch_[id] = epoch_;
        owner_value_[id] = static_cast<uint8_t>(value);
        return value;
    }
    static uint64_t equal(const std::array<Mask, 64>& planes, size_t word,
                          uint64_t value) {
        uint64_t result = ~uint64_t(0);
        for (int bit = 63; bit >= 0; --bit)
            result &= ((value >> bit) & 1) ? planes[bit][word] : ~planes[bit][word];
        return result;
    }
    // Unsigned lexicographic comparison after XORing the numeric sign bit.
    static uint64_t less(const std::array<Mask, 64>& planes, size_t word,
                         uint64_t value, bool inclusive = false) {
        uint64_t result = 0, eq = ~uint64_t(0);
        for (int bit = 63; bit >= 0; --bit) {
            const uint64_t plane = planes[bit][word];
            if ((value >> bit) & 1) {
                result |= eq & ~plane;
                eq &= plane;
            } else eq &= ~plane;
        }
        return inclusive ? result | eq : result;
    }
    void active() const {
        if (!stats_) filter_detail::fail("Qualifier::begin required before access");
    }
public:
    explicit Qualifier(const Dataset& data) : data_(data) {
        if (data.n != data.items.size() || data.n > std::numeric_limits<Id>::max() ||
            data.tin.size() != data.parent.size() || data.tout.size() != data.parent.size())
            filter_detail::fail("inconsistent or unranked dataset");
        const size_t count = data.n / 256 + (data.n % 256 != 0);
        planes_.resize(count);
        pages_.resize(count);
        page_epoch_.resize(count);
        point_epoch_.resize(data.n);
        point_value_.resize(data.n);
        owner_epoch_.resize(data.parent.size());
        owner_value_.resize(data.parent.size());
        for (size_t id = 0; id < data.n; ++id) {
            const auto& item = data.items[id];
            if (item.owner >= data.parent.size()) filter_detail::fail("invalid item owner");
            auto& page = planes_[id / 256];
            const size_t word = (id % 256) / 64;
            const uint64_t mask = uint64_t(1) << (id % 64);
            if (item.alive) page.live[word] |= mask;
            const uint64_t cat = static_cast<uint64_t>(item.category);
            const uint64_t num = static_cast<uint64_t>(item.numeric) ^ (uint64_t(1) << 63);
            for (size_t bit = 0; bit < 64; ++bit) {
                if ((cat >> bit) & 1) page.category[bit][word] |= mask;
                if ((num >> bit) & 1) page.numeric[bit][word] |= mask;
                if ((item.tags >> bit) & 1) page.tags[bit][word] |= mask;
            }
        }
    }
    void begin(const Query& query, QualStats& stats) {
        filter_detail::validate(query, data_);
        query_ = query;
        stats_ = &stats;
        if (++epoch_ == 0) {
            std::fill(page_epoch_.begin(), page_epoch_.end(), 0);
            std::fill(point_epoch_.begin(), point_epoch_.end(), 0);
            std::fill(owner_epoch_.begin(), owner_epoch_.end(), 0);
            epoch_ = 1;
        }
    }
    bool point(Id id) {
        active();
        if (id >= data_.n) filter_detail::fail("point ID out of range");
        if (point_epoch_[id] == epoch_) {
            ++stats_->point_cache_hits;
            return point_value_[id] != 0;
        }
        ++stats_->point_tests;
        const auto& item = data_.items[id];
        const bool value = item.alive && owner(item.owner) &&
                           filter_detail::extra(item, query_);
        point_epoch_[id] = epoch_;
        point_value_[id] = static_cast<uint8_t>(value);
        return value;
    }
    bool page_ready(uint32_t page_id) const {
        return epoch_ != 0 && page_id < page_epoch_.size() &&
               page_epoch_[page_id] == epoch_;
    }
    const std::array<uint64_t, 4>& page(uint32_t page_id) {
        active();
        if (page_id >= planes_.size()) filter_detail::fail("page ID out of range");
        if (page_epoch_[page_id] == epoch_) return pages_[page_id];
        const auto& planes = planes_[page_id];
        auto& result = pages_[page_id];
        Mask scope{};
        const size_t first = static_cast<size_t>(page_id) * 256;
        const size_t end = first + std::min(size_t(256), data_.n - first);
        for (size_t id = first; id < end; ++id) {
            if (data_.items[id].alive && owner(data_.items[id].owner))
                scope[(id % 256) / 64] |= uint64_t(1) << (id % 64);
        }
        for (size_t word = 0; word < 4; ++word) {
            bool any = false;
            uint64_t extra = query_.combine ? 0 : ~uint64_t(0);
            auto add = [&](uint64_t mask) {
                any = true;
                if (query_.combine) extra |= mask;
                else extra &= mask;
            };
            if (query_.category != -1)
                add(equal(planes.category, word, static_cast<uint64_t>(query_.category)));
            if (query_.lo != -1 || query_.hi != -1) {
                const uint64_t lo = static_cast<uint64_t>(query_.lo) ^ (uint64_t(1) << 63);
                const uint64_t hi = static_cast<uint64_t>(query_.hi) ^ (uint64_t(1) << 63);
                add(~less(planes.numeric, word, lo) &
                    less(planes.numeric, word, hi, true));
            }
            if (query_.tagmode) {
                uint64_t tags = query_.tagmode == 2 ? 0 : ~uint64_t(0);
                for (size_t bit = 0; bit < 64; ++bit) {
                    const bool set = ((query_.tags >> bit) & 1) != 0;
                    const uint64_t plane = planes.tags[bit][word];
                    if (query_.tagmode == 1 && set) tags &= plane;
                    else if (query_.tagmode == 2 && set) tags |= plane;
                    else if (query_.tagmode == 3) tags &= set ? plane : ~plane;
                    else if (query_.tagmode == 4 && !set) tags &= ~plane;
                }
                add(tags);
            }
            result[word] = planes.live[word] & scope[word] & (any ? extra : ~uint64_t(0));
        }
        page_epoch_[page_id] = epoch_;
        ++stats_->pages_prepared;
        stats_->mask_words += 4;
        return result;
    }
    size_t bytes() const {
        return sizeof(*this) + planes_.capacity() * sizeof(Planes) +
            pages_.capacity() * sizeof(Mask) +
            (page_epoch_.capacity() + point_epoch_.capacity() + owner_epoch_.capacity()) * sizeof(uint64_t) +
            (point_value_.capacity() + owner_value_.capacity()) * sizeof(uint8_t);
    }
};
} // namespace npi
