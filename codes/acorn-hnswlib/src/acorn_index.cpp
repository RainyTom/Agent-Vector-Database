#include "acorn_index.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numeric>
#include <random>
#include <unordered_set>

namespace {

double elapsed_s(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

struct Candidate {
    float distance;
    uint32_t id;
    bool operator<(const Candidate& other) const { return distance < other.distance; }
    bool operator>(const Candidate& other) const { return distance > other.distance; }
};

// min-heap on distance: back() is the nearest frontier node
struct CandidateGreater {
    bool operator()(const Candidate& a, const Candidate& b) const { return a.distance > b.distance; }
};

uint32_t read_u32(FILE* in) {
    uint32_t value = 0;
    std::fread(&value, sizeof(value), 1, in);
    return value;
}

}  // namespace

bool AcornIndex::build(const Dataset& dataset, const AcornBuildConfig& config,
                       AcornBuildStats* stats, std::string* error) {
    (void)error;
    const auto started = std::chrono::steady_clock::now();
    dimension_ = dataset.dim;
    object_count_ = dataset.num_objects();
    base_ = dataset.base.data();
    space_ = std::make_unique<RawL2Space>(size_t(dimension_));

    // ACORN per-level capacities (ACORN.cpp set_default_probas, L153-172):
    //   level 0 : M_beta + 1.5*M      above : M * gamma
    level0_capacity_ = size_t(double(config.M_beta) + 1.5 * double(config.M));
    upper_capacity_ = config.M * config.gamma;
    const size_t candidate_target = 2 * config.M * config.gamma;

    // level assignment, identical to ACORN's default probabilities
    const double level_mult = 1.0 / std::log(double(config.M));
    std::vector<double> probabilities;
    double sum = 0.0;
    for (int level = 0;; ++level) {
        const double proba = std::exp(-level / level_mult) * (1.0 - std::exp(-1.0 / level_mult));
        if (proba < 1e-9) break;
        probabilities.push_back(proba);
        sum += proba;
    }
    for (double& value : probabilities) value /= sum;

    std::mt19937 rng(config.seed);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);

    // mutable construction structures; level 0 is flattened at the end
    std::vector<std::vector<uint32_t>> level0{std::vector<std::vector<uint32_t>>(object_count_)};
    std::vector<std::vector<std::vector<uint32_t>>> upper{
        std::vector<std::vector<std::vector<uint32_t>>>(object_count_)};
    level_.assign(object_count_, 0);
    entry_point_ = 0;
    max_level_ = 0;

    auto neighbors_mut = [&](uint32_t id, int level) -> std::vector<uint32_t>& {
        if (level == 0) return level0[size_t(id)];
        std::vector<std::vector<uint32_t>>& per_node = upper[size_t(id)];
        if (per_node.size() < size_t(level)) per_node.resize(size_t(level));
        return per_node[size_t(level - 1)];
    };
    auto neighbors_ref = [&](uint32_t id, int level) -> const std::vector<uint32_t>& {
        static const std::vector<uint32_t> empty;
        if (level == 0) return level0[size_t(id)];
        const std::vector<std::vector<uint32_t>>& per_node = upper[size_t(id)];
        const size_t slot = size_t(level - 1);
        return slot < per_node.size() ? per_node[slot] : empty;
    };

    std::vector<uint8_t> visited(object_count_, 0);
    std::vector<uint32_t> touched;

    auto capacity_of = [&](int level) -> size_t {
        return level == 0 ? level0_capacity_ : upper_capacity_;
    };

    // ACORN's predicate-agnostic pruning (ACORN.cpp shrink_neighbor_list, L530-590).
    // The reference walks the queue from nearest to farthest; the first M_beta
    // candidates are kept verbatim, afterwards a candidate already covered as a
    // two-hop neighbour is dropped, otherwise it is kept together with its own
    // neighbours; the walk stops once |neigh_of_neigh| reaches max_size.
    auto shrink = [&](std::vector<Candidate>& input, size_t max_size, int level, uint32_t source) {
        std::sort(input.begin(), input.end());
        std::vector<Candidate> kept;
        kept.reserve(max_size);
        std::unordered_set<uint32_t> neigh_of_neigh;
        const size_t M_beta = config.M_beta;
        size_t examined = 0;
        for (size_t i = 0; i < input.size(); ++i) {
            const Candidate candidate = input[i];
            if (candidate.id == source) continue;
            ++examined;
            if (examined > M_beta && neigh_of_neigh.count(candidate.id)) continue;
            kept.push_back(candidate);
            if (kept.size() >= max_size) break;
            neigh_of_neigh.insert(candidate.id);
            if (examined > M_beta)
                for (uint32_t second : neighbors_ref(candidate.id, level)) neigh_of_neigh.insert(second);
            if (neigh_of_neigh.size() >= max_size) break;
        }
        input.swap(kept);
    };

    auto mark = [&](uint32_t id) {
        if (visited[size_t(id)]) return false;
        visited[size_t(id)] = 1;
        touched.push_back(id);
        return true;
    };

    // construction beam, mirroring ACORN's search_neighbors_to_add (L703-780):
    // result list bounded by efConstruction, walk stops once `target` candidates
    // were collected, at most M neighbours of each expanded node are examined.
    auto search_layer = [&](uint32_t query_id, uint32_t entry, int level, size_t target,
                            std::vector<Candidate>& out) {
        out.clear();
        touched.clear();
        std::vector<Candidate> results;     // max-heap by distance
        std::vector<Candidate> candidates;  // min-heap by distance
        const float* query = vector_of(query_id);
        const size_t ef = std::max<size_t>(config.ef_construction, target);

        auto push_result = [&](float d, uint32_t id) {
            if (results.size() < ef) {
                results.push_back({d, id});
                std::push_heap(results.begin(), results.end());
            } else if (d < results.front().distance) {
                std::pop_heap(results.begin(), results.end());
                results.back() = {d, id};
                std::push_heap(results.begin(), results.end());
            }
            candidates.push_back({d, id});
            std::push_heap(candidates.begin(), candidates.end(), CandidateGreater());
        };

        const float entry_distance = space_->dist(query, vector_of(entry));
        mark(entry);
        push_result(entry_distance, entry);

        while (!candidates.empty()) {
            std::pop_heap(candidates.begin(), candidates.end(), CandidateGreater());
            const Candidate current = candidates.back();
            candidates.pop_back();
            if (results.size() >= target ||
                (config.gamma == 1 && current.distance > results.front().distance)) {
                break;
            }
            const std::vector<uint32_t>& neighbours = neighbors_ref(current.id, level);
            size_t examined = 0;
            for (uint32_t neighbour : neighbours) {
                if (examined >= config.M) break;
                ++examined;
                if (!mark(neighbour)) continue;
                const float d = space_->dist(query, vector_of(neighbour));
                if (results.size() < ef || results.front().distance > d) push_result(d, neighbour);
            }
        }

        std::sort(results.begin(), results.end());
        out = results;
        for (uint32_t id : touched) visited[size_t(id)] = 0;
    };

    auto add_link = [&](uint32_t from, uint32_t to, int level) {
        std::vector<uint32_t>& list = neighbors_mut(from, level);
        if (std::find(list.begin(), list.end(), to) != list.end()) return;
        list.push_back(to);
        if (list.size() <= capacity_of(level)) return;
        std::vector<Candidate> as_candidates;
        as_candidates.reserve(list.size());
        const float* base = vector_of(from);
        for (uint32_t id : list)
            as_candidates.push_back({space_->dist(base, vector_of(id)), id});
        shrink(as_candidates, capacity_of(level), level, from);
        list.clear();
        for (const Candidate& kept : as_candidates) list.push_back(kept.id);
    };

    for (size_t index = 0; index < object_count_; ++index) {
        const uint32_t id = uint32_t(index);
        int level = 0;
        double draw = uniform(rng);
        while (level + 1 < int(probabilities.size()) && draw > probabilities[size_t(level)]) {
            draw -= probabilities[size_t(level)];
            ++level;
        }
        level_[size_t(id)] = level;
        if (level > 0) upper[size_t(id)].assign(size_t(level), {});

        if (index == 0) {
            entry_point_ = id;
            max_level_ = level;
            continue;
        }

        uint32_t ep = entry_point_;
        for (int l = max_level_; l > level; --l) {
            bool improved = true;
            while (improved) {
                improved = false;
                float ep_distance = space_->dist(vector_of(id), vector_of(ep));
                size_t examined = 0;
                for (uint32_t neighbour : neighbors_ref(ep, l)) {
                    if (examined >= config.M) break;
                    ++examined;
                    const float d = space_->dist(vector_of(id), vector_of(neighbour));
                    if (d < ep_distance) {
                        ep = neighbour;
                        ep_distance = d;
                        improved = true;
                    }
                }
            }
        }

        std::vector<Candidate> candidates;
        for (int l = std::min(level, max_level_); l >= 0; --l) {
            const size_t target = (l == 0) ? candidate_target : upper_capacity_;
            search_layer(id, ep, l, target, candidates);
            if (candidates.empty()) break;
            shrink(candidates, capacity_of(l), l, id);
            for (const Candidate& neighbour : candidates) {
                add_link(id, neighbour.id, l);
                add_link(neighbour.id, id, l);
            }
            ep = candidates.front().id;
        }
        if (level > max_level_) {
            max_level_ = level;
            entry_point_ = id;
        }
    }

    // flatten level 0 into CSR
    level0_offset_.assign(object_count_ + 1, 0);
    for (size_t node = 0; node < object_count_; ++node)
        level0_offset_[node + 1] = level0_offset_[node] + uint32_t(level0[node].size());
    level0_target_.resize(level0_offset_.back());
    for (size_t node = 0; node < object_count_; ++node) {
        const std::vector<uint32_t>& list = level0[node];
        std::memcpy(level0_target_.data() + level0_offset_[node], list.data(),
                    list.size() * sizeof(uint32_t));
    }
    upper_adjacency_ = std::move(upper);
    level0.clear();
    level0.shrink_to_fit();

    if (stats) {
        stats->build_s = elapsed_s(started);
        stats->object_count = object_count_;
        stats->dimension = dimension_;
        stats->level0_degree = object_count_ ? double(level0_target_.size()) / double(object_count_) : 0.0;
    }
    return true;
}

bool AcornIndex::save(const std::string& path, std::string* error) const {
    FILE* out = std::fopen(path.c_str(), "wb");
    if (!out) { if (error) *error = "cannot open " + path; return false; }
    const uint32_t magic = 0x41435253u;  // "ACRS" (CSR layout)
    std::fwrite(&magic, sizeof(magic), 1, out);
    int header[3] = {dimension_, int(object_count_), max_level_};
    std::fwrite(header, sizeof(header), 1, out);
    std::fwrite(&entry_point_, sizeof(entry_point_), 1, out);
    std::fwrite(level_.data(), sizeof(int), level_.size(), out);
    // level 0: CSR
    std::fwrite(level0_offset_.data(), sizeof(uint32_t), level0_offset_.size(), out);
    std::fwrite(level0_target_.data(), sizeof(uint32_t), level0_target_.size(), out);
    // levels >= 1
    for (size_t node = 0; node < object_count_; ++node) {
        const uint32_t levels = uint32_t(upper_adjacency_[node].size());
        std::fwrite(&levels, sizeof(levels), 1, out);
        for (const auto& list : upper_adjacency_[node]) {
            const uint32_t count = uint32_t(list.size());
            std::fwrite(&count, sizeof(count), 1, out);
            if (count) std::fwrite(list.data(), sizeof(uint32_t), count, out);
        }
    }
    const bool ok = std::ferror(out) == 0;
    std::fclose(out);
    if (!ok && error) *error = "write error on " + path;
    return ok;
}

bool AcornIndex::load(const std::string& path, int dimension, std::string* error) {
    FILE* in = std::fopen(path.c_str(), "rb");
    if (!in) { if (error) *error = "cannot open " + path; return false; }
    auto fail = [&](const char* message) {
        if (error) *error = std::string(message) + " in " + path;
        std::fclose(in);
        return false;
    };
    uint32_t magic = 0;
    if (std::fread(&magic, sizeof(magic), 1, in) != 1 || magic != 0x41435253u)
        return fail("bad magic");
    int header[3] = {0, 0, 0};
    if (std::fread(header, sizeof(header), 1, in) != 1) return fail("truncated header");
    if (header[0] != dimension) return fail("dimension mismatch");
    dimension_ = header[0];
    object_count_ = size_t(header[1]);
    max_level_ = header[2];
    if (std::fread(&entry_point_, sizeof(entry_point_), 1, in) != 1) return fail("truncated entry");
    level_.resize(object_count_);
    if (std::fread(level_.data(), sizeof(int), object_count_, in) != object_count_)
        return fail("truncated levels");
    level0_offset_.resize(object_count_ + 1);
    if (std::fread(level0_offset_.data(), sizeof(uint32_t), level0_offset_.size(), in) !=
        level0_offset_.size())
        return fail("truncated offsets");
    level0_target_.resize(level0_offset_.back());
    if (!level0_target_.empty() &&
        std::fread(level0_target_.data(), sizeof(uint32_t), level0_target_.size(), in) !=
            level0_target_.size())
        return fail("truncated targets");
    upper_adjacency_.assign(object_count_, {});
    for (size_t node = 0; node < object_count_; ++node) {
        uint32_t levels = 0;
        if (std::fread(&levels, sizeof(levels), 1, in) != 1) return fail("truncated level count");
        upper_adjacency_[node].resize(levels);
        for (uint32_t l = 0; l < levels; ++l) {
            uint32_t count = 0;
            if (std::fread(&count, sizeof(count), 1, in) != 1) return fail("truncated degree");
            upper_adjacency_[node][l].resize(count);
            if (count && std::fread(upper_adjacency_[node][l].data(), sizeof(uint32_t), count, in) != count)
                return fail("truncated neighbours");
        }
    }
    std::fclose(in);
    space_ = std::make_unique<RawL2Space>(size_t(dimension_));
    return true;
}
