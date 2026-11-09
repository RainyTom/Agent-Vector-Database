#include "acorn_index.h"

namespace port {

void Acorn::build(const float* vectors, size_t count) {
    std::mt19937 level_rng(12345), shuffle_rng(789);
    const float scale = 1.0 / std::log(m_);
    std::vector<float> probabilities;
    for (int layer = 0; ; ++layer) {
        float probability = std::exp(double(-layer / scale)) * (1 - std::exp(double(-1 / scale)));
        if (probability < 1e-9) break;
        probabilities.push_back(probability);
    }
    std::vector<int> levels(count);
    std::vector<std::vector<Node>> groups(probabilities.size());
    for (Node id = 0; id < count; ++id) {
        double sample = level_rng() / float(level_rng.max());
        size_t layer = 0;
        for (; layer + 1 < probabilities.size() && sample >= probabilities[layer]; ++layer) {
            sample -= probabilities[layer];
        }
        levels[id] = layer;
        groups[layer].push_back(id);
    }
    initialize(vectors, levels);
    for (auto group = groups.rbegin(); group != groups.rend(); ++group) {
        for (size_t i = 0; i < group->size(); ++i) {
            std::swap((*group)[i], (*group)[i + shuffle_rng() % (group->size() - i)]);
            insert((*group)[i]);
        }
    }
}

FarHeap Acorn::construction_candidates(const void* query, Edge entry, int layer) {
    start_visit();
    NearHeap frontier;
    FarHeap results;
    frontier.push(entry);
    results.push(entry);
    visit(entry.node);
    const size_t target = layer == 0 ? 2 * m_ * gamma_ : capacity(layer);
    while (!frontier.empty()) {
        const auto next = frontier.top();
        if ((gamma_ == 1 && next.distance > results.top().distance) || results.size() >= target) break;
        frontier.pop();
        int iterations = 0;
        for (const auto id : links(next.node, layer)) {
            if (visited(id)) continue;
            visit(id);
            // Preserve both iteration increments in the released construction code.
            if (++iterations > m_) break;
            const Edge candidate{distance(query, id), id};
            if (results.size() < size_t(construction_) || candidate.distance < results.top().distance) {
                frontier.push(candidate);
                results.push(candidate);
                if (results.size() > size_t(construction_)) results.pop();
            }
            if (++iterations > m_) break;
        }
    }
    return results;
}

void Acorn::prune_bottom(FarHeap& candidates) {
    NearHeap ordered;
    while (!candidates.empty()) {
        ordered.push(candidates.top());
        candidates.pop();
    }
    std::unordered_set<Node> covered;
    int rank = 0;
    while (!ordered.empty()) {
        const Edge edge = ordered.top();
        ordered.pop();
        const bool tail = ++rank > prefix_;
        if (tail && covered.count(edge.node)) continue;
        candidates.push(edge);
        if (candidates.size() >= maxM0_) break;
        covered.insert(edge.node);
        if (tail) {
            for (auto dest : links(edge.node, 0)) covered.insert(dest);
        }
        if (covered.size() >= maxM0_) break;
    }
}

void Acorn::connect(Node from, Node to, int layer) {
    auto* list = get_linklist_at_level(from, layer);
    const size_t count = getListCount(list);
    if (count < capacity(layer)) {
        list[count + 1] = to;
        setListCount(list, count + 1);
        return;
    }
    const auto* query = getDataByInternalId(from);
    FarHeap candidates;
    candidates.push({distance(query, to), to});
    for (const auto id : links(from, layer)) candidates.push({distance(query, id), id});
    if (layer == 0) prune_bottom(candidates);
    size_t n = 0;
    while (!candidates.empty() && n < capacity(layer)) {
        list[++n] = candidates.top().node;
        candidates.pop();
    }
    setListCount(list, n);
}

void Acorn::insert(Node id) {
    if (enterpoint_node_ == missing) {
        set_entry(id);
        return;
    }
    const auto* query = getDataByInternalId(id);
    Edge entry{distance(query, enterpoint_node_), enterpoint_node_};
    int layer = maxlevel_;
    for (; layer > element_levels_[id]; --layer) {
        for (;;) {
            const Node previous = entry.node;
            const auto list = links(previous, layer);
            for (size_t i = 0; i < std::min(list.size(), size_t(m_)); ++i) {
                const float d = distance(query, list[i]);
                if (d < entry.distance) entry = {d, list[i]};
            }
            if (entry.node == previous) break;
        }
    }
    for (; layer >= 0; --layer) {
        auto candidates = construction_candidates(query, entry, layer);
        if (layer == 0) prune_bottom(candidates);
        std::vector<Node> targets;
        targets.reserve(candidates.size());
        while (!candidates.empty()) {
            targets.push_back(candidates.top().node);
            candidates.pop();
        }
        for (const Node to : targets) connect(id, to, layer);
        for (const Node to : targets) connect(to, id, layer);
    }
}

Result Acorn::search(const float* query, const uint8_t* mask, int ef, uint64_t* calls,
                     std::vector<Node>* trace) {
    const auto evaluate = [&](Node node) {
        if (calls) ++*calls;
        if (trace) trace->push_back(node);
        return distance(query, node);
    };
    if (enterpoint_node_ == missing) return {};
    Edge entry{evaluate(enterpoint_node_), enterpoint_node_};
    for (int layer = maxlevel_; layer > 0; --layer) {
        for (;;) {
            int found = 0;
            const Node previous = entry.node;
            const auto consider = [&](Node node) {
                if (!mask[node]) return false;
                ++found;
                const float d = evaluate(node);
                if (d < entry.distance || !mask[entry.node]) entry = {d, node};
                return found >= m_;
            };
            for (const auto node : links(previous, layer)) {
                if (consider(node)) break;
                if (gamma_ == 1) {
                    for (const auto second : links(node, layer)) {
                        if (consider(second)) break;
                    }
                }
            }
            if (entry.node == previous) break;
        }
    }

    start_visit();
    queue_.reset(std::max(ef, top_k));
    std::array<std::pair<float, Node>, top_k> best;
    size_t found_best = 0;
    const auto save = [&](Edge value) {
        const auto candidate = std::make_pair(value.distance, value.node);
        if (found_best == top_k) {
            if (candidate.first >= best.front().first) return;
            std::pop_heap(best.begin(), best.end());
            best.back() = candidate;
        } else {
            best[found_best++] = candidate;
        }
        std::push_heap(best.begin(), best.begin() + found_best);
    };
    queue_.push(entry);
    visit(entry.node);
    if (mask[entry.node]) save(entry);
    const auto explore = [&](Node node) {
        visit(node);
        const Edge candidate{evaluate(node), node};
        save(candidate);
        queue_.push(candidate);
    };
    while (!queue_.empty()) {
        const auto next = queue_.pop_nearest();
        if (queue_.count_below(next.distance) >= ef) break;
        int found = 0;
        bool expand = true;
        const auto list = links(next.node, 0);
        for (size_t j = 0; j < list.size(); ++j) {
            const auto node = list[j];
            found += bool(mask[node]);
            if (visited(node)) continue;
            if (mask[node]) {
                explore(node);
                if (found >= 2 * m_) break;
            }
            if (gamma_ == 1 || (j >= size_t(prefix_) && expand)) {
                for (const auto second : links(node, 0)) {
                    if (!mask[second]) continue;
                    ++found;
                    if (visited(second)) continue;
                    explore(second);
                    if (found >= 2 * m_) {
                        expand = false;
                        break;
                    }
                }
            }
        }
    }
    std::sort_heap(best.begin(), best.begin() + found_best);
    Result result;
    for (size_t i = 0; i < found_best; ++i) {
        result.ids[i] = getExternalLabel(best[i].second);
        result.distances[i] = best[i].first;
    }
    return result;
}

} // namespace port
