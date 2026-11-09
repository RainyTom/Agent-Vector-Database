#include "acorn_index.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numeric>
#include <queue>
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

struct CandidateGreater {
    bool operator()(const Candidate& a, const Candidate& b) const { return a.distance > b.distance; }
};

}  // namespace

bool AcornIndex::build(const Dataset& dataset, const AcornBuildConfig& config,
                       AcornBuildStats* stats, std::string* error) {
    const auto started = std::chrono::steady_clock::now();
    dimension_ = dataset.dim;
    object_count_ = dataset.num_objects();
    base_ = dataset.base.data();
    space_ = std::make_unique<RawL2Space>(size_t(dimension_));

    // ACORN's per-level capacities (ACORN.cpp set_default_probas, L153-172):
    //   level 0 : M_beta + 1.5*M
    //   above   : M * gamma
    level0_capacity_ = size_t(double(config.M_beta) + 1.5 * double(config.M));
    upper_capacity_ = config.M * config.gamma;
    const size_t candidate_target = 2 * config.M * config.gamma;  // level-0 collection

    // level assignment probability, identical to ACORN's default_probas
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

    adjacency_.assign(object_count_, {});
    level_.assign(object_count_, 0);
    entry_point_ = 0;
    max_level_ = 0;

    std::vector<uint8_t> visited(object_count_, 0);
    std::vector<uint32_t> touched;

    auto neighbor_count = [&](int level) -> size_t {
        return level == 0 ? level0_capacity_ : upper_capacity_;
    };

    // ACORN's predicate-agnostic pruning (ACORN.cpp shrink_neighbor_list, L530-590):
    // visit candidates from farthest to nearest, keep the first M_beta verbatim,
    // afterwards drop a candidate already present in neigh_of_neigh, otherwise keep
    // it and add the candidate plus all of its neighbours to neigh_of_neigh.
    auto shrink = [&](std::vector<Candidate>& input, size_t max_size, int level,
                      uint32_t source) {
        std::sort(input.begin(), input.end());   // ascending distance
        std::vector<Candidate> kept;
        kept.reserve(max_size);
        std::unordered_set<uint32_t> neigh_of_neigh;
        const size_t M_beta = config.M_beta;
        size_t examined = 0;
        // nearest first: ACORN's priority_queue<NodeDistFarther> holds the nearest on
        // top (its operator< compares d > other.d), so the walk is nearest -> farthest
        for (size_t i = 0; i < input.size(); ++i) {
            const Candidate candidate = input[i];
            if (candidate.id == source) continue;
            ++examined;
            if (examined > M_beta && neigh_of_neigh.count(candidate.id)) continue;
            kept.push_back(candidate);
            if (kept.size() >= max_size) break;
            neigh_of_neigh.insert(candidate.id);
            if (examined > M_beta) {
                const std::vector<uint32_t>& list =
                    adjacency_[size_t(candidate.id)][size_t(level)];
                for (uint32_t second : list) neigh_of_neigh.insert(second);
            }
            if (neigh_of_neigh.size() >= max_size) break;
        }
        input.swap(kept);
    };

    // beam search on one level, ACORN style: results bounded by efConstruction,
    // and at most M neighbours of each expanded node are examined.
    // construction beam search, mirroring ACORN's search_neighbors_to_add
    // (ACORN.cpp L703-780): the result list is bounded by efConstruction, the walk
    // stops once `target` (= 2*M*gamma at level 0) candidates are collected, and at
    // most M neighbours of each expanded node are examined.
    auto search_layer = [&](uint32_t query_id, uint32_t entry, int level, size_t target,
                            std::vector<Candidate>& out) {
        out.clear();
        touched.clear();
        std::vector<Candidate> results;    // max-heap by distance, capacity efConstruction
        std::vector<Candidate> candidates; // max-heap by distance, frontier
        const float* query = base_ + size_t(query_id) * size_t(dimension_);
        const size_t ef = std::max<size_t>(config.ef_construction, target);

        auto mark = [&](uint32_t id) {
            if (visited[size_t(id)]) return false;
            visited[size_t(id)] = 1;
            touched.push_back(id);
            return true;
        };
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
            // candidates is a min-heap on distance: back() is the nearest frontier node
            std::pop_heap(candidates.begin(), candidates.end(), CandidateGreater());
            const Candidate current = candidates.back();
            candidates.pop_back();
            // greedy stop: enough candidates collected, or (gamma == 1) the nearest
            // remaining candidate is farther than the worst collected one
            if (results.size() >= target ||
                (config.gamma == 1 && current.distance > results.front().distance)) {
                break;
            }
            const std::vector<uint32_t>& neighbours =
                adjacency_[size_t(current.id)][size_t(level)];
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
        std::vector<uint32_t>& list = adjacency_[size_t(from)][size_t(level)];
        if (std::find(list.begin(), list.end(), to) != list.end()) return;
        list.push_back(to);
        if (list.size() <= neighbor_count(level)) return;
        // prune the overflowing list with the same ACORN rule
        std::vector<Candidate> as_candidates;
        as_candidates.reserve(list.size());
        const float* base = vector_of(from);
        for (uint32_t id : list)
            as_candidates.push_back({space_->dist(base, vector_of(id)), id});
        shrink(as_candidates, neighbor_count(level), level, from);
        list.clear();
        for (const Candidate& kept : as_candidates) list.push_back(kept.id);
    };

    std::vector<uint32_t> order(object_count_);
    std::iota(order.begin(), order.end(), 0u);

    for (size_t index = 0; index < object_count_; ++index) {
        const uint32_t id = order[index];
        int level = 0;
        double draw = uniform(rng);
        while (level + 1 < int(probabilities.size()) && draw > probabilities[size_t(level)]) {
            draw -= probabilities[size_t(level)];
            ++level;
        }
        level_[size_t(id)] = level;
        adjacency_[size_t(id)].assign(size_t(level) + 1, {});

        if (index == 0) {
            entry_point_ = id;
            max_level_ = level;
            continue;
        }

        uint32_t ep = entry_point_;
        int ep_level = max_level_;
        for (int l = max_level_; l > level; --l) {
            // greedy descent, distance only
            bool improved = true;
            while (improved) {
                improved = false;
                if (ep_level < l) break;
                const std::vector<uint32_t>& neighbours = adjacency_[size_t(ep)][size_t(l)];
                size_t examined = 0;
                for (uint32_t neighbour : neighbours) {
                    if (examined >= config.M) break;
                    ++examined;
                    const float d = space_->dist(vector_of(id), vector_of(neighbour));
                    if (d < space_->dist(vector_of(id), vector_of(ep))) {
                        ep = neighbour;
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
            shrink(candidates, neighbor_count(l), l, id);
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

    if (stats) {
        stats->build_s = elapsed_s(started);
        stats->object_count = object_count_;
        stats->dimension = dimension_;
    }
    return true;
}

bool AcornIndex::save(const std::string& path, std::string* error) const {
    FILE* out = std::fopen(path.c_str(), "wb");
    if (!out) { if (error) *error = "cannot open " + path; return false; }
    const uint32_t magic = 0x41434F52u;  // "ACOR"
    std::fwrite(&magic, sizeof(magic), 1, out);
    int header[3] = {dimension_, int(object_count_), max_level_};
    std::fwrite(header, sizeof(header), 1, out);
    std::fwrite(&entry_point_, sizeof(entry_point_), 1, out);
    std::fwrite(level_.data(), sizeof(int), level_.size(), out);
    for (size_t node = 0; node < object_count_; ++node) {
        const uint32_t levels = uint32_t(adjacency_[node].size());
        std::fwrite(&levels, sizeof(levels), 1, out);
        for (const auto& list : adjacency_[node]) {
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
    if (std::fread(&magic, sizeof(magic), 1, in) != 1 || magic != 0x41434F52u)
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
    adjacency_.assign(object_count_, {});
    for (size_t node = 0; node < object_count_; ++node) {
        uint32_t levels = 0;
        if (std::fread(&levels, sizeof(levels), 1, in) != 1) return fail("truncated level count");
        adjacency_[node].resize(levels);
        for (uint32_t l = 0; l < levels; ++l) {
            uint32_t count = 0;
            if (std::fread(&count, sizeof(count), 1, in) != 1) return fail("truncated degree");
            adjacency_[node][l].resize(count);
            if (count && std::fread(adjacency_[node][l].data(), sizeof(uint32_t), count, in) != count)
                return fail("truncated neighbours");
        }
    }
    std::fclose(in);
    // The vectors are not stored; they are re-attached from the dataset by the caller.
    level0_capacity_ = 0;
    upper_capacity_ = 0;
    space_ = std::make_unique<RawL2Space>(size_t(dimension_));
    return true;
}
