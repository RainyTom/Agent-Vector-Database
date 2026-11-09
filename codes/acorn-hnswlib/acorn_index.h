#pragma once

#include <hnswlib/hnswlib.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>

namespace port {

using Node = hnswlib::tableint;
using Id = int64_t;
constexpr Node missing = hnswlib::kInvalidInternalId;
constexpr int top_k = 20;
constexpr float infinity = std::numeric_limits<float>::infinity();

struct Result {
    std::array<Id, top_k> ids;
    std::array<float, top_k> distances;
    Result() {
        ids.fill(-1);
        distances.fill(infinity);
    }
    bool operator==(const Result& other) const {
        return ids == other.ids && distances == other.distances;
    }
};

struct Edge {
    float distance;
    Node node;
};
struct FartherFirst {
    bool operator()(Edge a, Edge b) const { return a.distance < b.distance; }
};
struct CloserFirst {
    bool operator()(Edge a, Edge b) const { return a.distance > b.distance; }
};
using FarHeap = std::priority_queue<Edge, std::vector<Edge>, FartherFirst>;
using NearHeap = std::priority_queue<Edge, std::vector<Edge>, CloserFirst>;

// The author's bounded search queue retains popped distances for the stopping rule.
class SearchQueue {
public:
    void reset(size_t capacity) {
        edges_.resize(capacity + 1);
        slots_ = active_ = 0;
    }
    bool empty() const { return active_ == 0; }
    void push(Edge value) {
        if (slots_ == edges_.size() - 1) {
            if (value.distance >= edges_[1].distance) return;
            const Edge last = edges_[slots_--];
            size_t i = 1;
            for (size_t child = 2; child <= slots_; child = i * 2) {
                if (child < slots_ && greater(edges_[child + 1], edges_[child])) ++child;
                if (greater(last, edges_[child])) break;
                edges_[i] = edges_[child];
                i = child;
            }
            edges_[i] = last;
            --active_;
        }
        size_t i = ++slots_;
        while (i > 1 && greater(value, edges_[i / 2])) {
            edges_[i] = edges_[i / 2];
            i /= 2;
        }
        edges_[i] = value;
        ++active_;
    }
    Edge pop_nearest() {
        size_t best = 0;
        for (size_t i = slots_; i > 0; --i) {
            if (edges_[i].node != missing &&
                (best == 0 || edges_[i].distance < edges_[best].distance)) best = i;
        }
        if (best == 0) throw std::logic_error("ACORN queue has no active node");
        const auto result = edges_[best];
        edges_[best].node = missing;
        --active_;
        return result;
    }
    int count_below(float distance) const {
        int count = 0;
        for (size_t i = 1; i <= slots_; ++i) count += edges_[i].distance < distance;
        return count;
    }

private:
    static bool greater(Edge a, Edge b) {
        // A removed node has the author's signed sentinel ID -1.
        const auto signed_id = [](Node id) { return id == missing ? Id{-1} : static_cast<Id>(id); };
        return a.distance > b.distance || (a.distance == b.distance && signed_id(a.node) > signed_id(b.node));
    }
    std::vector<Edge> edges_;
    size_t slots_ = 0;
    int active_ = 0;
};

// ACORN policies over hnswlib's native vector/adjacency storage and index codec.
// Fixed-data, single-threaded build and search only.
class Acorn : public hnswlib::HierarchicalNSW<float> {
public:
    Acorn(hnswlib::L2Space& space, int gamma, int prefix, int m = 32, int construction = 40)
        : HierarchicalNSW(&space), gamma_(gamma), prefix_(prefix), m_(m), construction_(construction) {
        data_size_ = space.get_data_size();
        fstdistfunc_ = space.get_dist_func();
        dist_func_param_ = space.get_dist_func_param();
    }

    void initialize(const float* vectors, const std::vector<int>& levels) {
        if (size() || levels.empty()) throw std::logic_error("Expected a new, nonempty ACORN index");
        max_elements_ = levels.size();
        M_ = m_;
        maxM_ = m_ * gamma_;
        maxM0_ = prefix_ + 3 * m_ / 2;
        ef_construction_ = construction_;
        mult_ = 1.0 / std::log(m_);
        size_links_level0_ = (maxM0_ + 1) * sizeof(Node);
        size_links_per_element_ = (maxM_ + 1) * sizeof(Node);
        offsetData_ = size_links_level0_;
        label_offset_ = offsetData_ + data_size_;
        size_data_per_element_ = label_offset_ + sizeof(hnswlib::labeltype);
        data_level0_memory_ = static_cast<char*>(std::malloc(max_elements_ * size_data_per_element_));
        linkLists_ = static_cast<char**>(std::calloc(max_elements_, sizeof(char*)));
        if (!data_level0_memory_ || !linkLists_) throw std::bad_alloc();
        element_levels_ = levels;
        for (Node id = 0; id < max_elements_; ++id) {
            if (levels[id]) {
                linkLists_[id] = static_cast<char*>(std::calloc(levels[id], size_links_per_element_));
                if (!linkLists_[id]) throw std::bad_alloc();
            }
            ++cur_element_count;
            std::memset(get_linklist0(id), 0, size_links_level0_);
            setExternalLabel(id, id);
            std::memcpy(getDataByInternalId(id), vectors + id * (data_size_ / sizeof(float)), data_size_);
        }
        enterpoint_node_ = missing;
        maxlevel_ = -1;
        visits_.resize(size());
    }

    size_t size() const { return cur_element_count; }
    void set_entry(Node node) {
        enterpoint_node_ = node;
        maxlevel_ = element_levels_[node];
    }
    void set_links(Node owner, int layer, const std::vector<Node>& edges) {
        if (edges.size() > capacity(layer)) throw std::logic_error("Too many ACORN edges");
        auto* list = get_linklist_at_level(owner, layer);
        std::copy(edges.begin(), edges.end(), list + 1);
        setListCount(list, edges.size());
    }
    void load(const std::string& path, hnswlib::L2Space& space) {
        loadIndex(path, &space);
        if (maxM_ != size_t(m_ * gamma_) || maxM0_ != size_t(prefix_ + 3 * m_ / 2) ||
            ef_construction_ != size_t(construction_)) throw std::runtime_error("Incorrect ACORN index configuration");
        visits_.resize(size());
    }
    void validate() const {
        if (enterpoint_node_ >= size() || element_levels_[enterpoint_node_] != maxlevel_) {
            throw std::runtime_error("Invalid ACORN entry");
        }
        for (Node id = 0; id < size(); ++id) {
            if (getExternalLabel(id) != id || element_levels_[id] > maxlevel_) {
                throw std::runtime_error("Invalid ACORN ID or level");
            }
            for (int layer = 0; layer <= element_levels_[id]; ++layer) {
                const auto list = links(id, layer);
                if (list.size() > capacity(layer)) throw std::runtime_error("ACORN degree exceeds capacity");
                for (auto dest : list) {
                    if (dest >= size() || id == dest || element_levels_[dest] < layer) {
                        throw std::runtime_error("Invalid ACORN edge");
                    }
                }
            }
        }
    }

    void build(const float* vectors, size_t count);
    Result search(const float* query, const uint8_t* mask, int ef, uint64_t* calls = nullptr,
                  std::vector<Node>* trace = nullptr);

private:
    struct Links {
        const Node* first;
        size_t count;
        const Node* begin() const { return first; }
        const Node* end() const { return first + count; }
        size_t size() const { return count; }
        Node operator[](size_t offset) const { return first[offset]; }
    };
    Links links(Node owner, int layer) const {
        auto* list = get_linklist_at_level(owner, layer);
        return {list + 1, getListCount(list)};
    }
    size_t capacity(int layer) const { return layer == 0 ? maxM0_ : maxM_; }
    float distance(const void* query, Node id) const {
        return fstdistfunc_(query, getDataByInternalId(id), dist_func_param_);
    }
    bool visited(Node id) const { return visits_[id] == epoch_; }
    void visit(Node id) { visits_[id] = epoch_; }
    void start_visit() {
        if (++epoch_ == 0) {
            std::fill(visits_.begin(), visits_.end(), 0);
            ++epoch_;
        }
    }
    void insert(Node id);
    FarHeap construction_candidates(const void* query, Edge entry, int layer);
    void prune_bottom(FarHeap& candidates);
    void connect(Node from, Node to, int layer);

    int gamma_, prefix_, m_, construction_;
    std::vector<uint32_t> visits_;
    uint32_t epoch_ = 0;
    SearchQueue queue_;
};

} // namespace port
