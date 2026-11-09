#include "acorn_index.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace {

using namespace port;
using Graph = hnswlib::HierarchicalNSW<float>;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
constexpr size_t dimension = 128, nbase = 1000000, nqueries = 10000, sample = 120;
constexpr std::array<int, 17> widths{10, 60, 110, 160, 210, 260, 310, 360, 410, 460, 510,
                                     560, 610, 660, 710, 760, 800};

template <typename T>
std::vector<T> read(const fs::path& path, size_t count) {
    std::ifstream input(path, std::ios::binary);
    std::vector<T> data(count);
    if (!input.read(reinterpret_cast<char*>(data.data()), count * sizeof(T))) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    return data;
}

struct Workload {
    explicit Workload(const fs::path& input, size_t count, size_t query_count)
        : labels(read<uint8_t>(input / "train.labels", count)),
          predicates(read<uint8_t>(input / "test.labels", query_count)),
          queries(read<float>(input / "test.f32", query_count * dimension)) {
        for (size_t id = 0; id < labels.size(); ++id) postings.at(labels[id]).push_back(id);
        for (int p = 1; p <= 12; ++p) {
            if (postings[p].empty()) throw std::runtime_error("Empty predicate partition");
            budget[p] = std::min(count, (top_k * count + postings[p].size() - 1) / postings[p].size());
        }
        if (!postings[0].empty()) throw std::runtime_error("Invalid base label");
        for (auto p : predicates) if (p == 0 || p > 12) throw std::runtime_error("Invalid query predicate");
    }
    const float* query(size_t row) const { return queries.data() + row * dimension; }
    const std::vector<uint8_t> labels, predicates;
    const std::vector<float> queries;
    std::array<std::vector<Node>, 13> postings;
    std::array<size_t, 13> budget{};
};

double seconds(Clock::time_point begin) {
    return std::chrono::duration<double>(Clock::now() - begin).count();
}

std::array<std::vector<uint8_t>, 13> masks(const Workload& work) {
    std::array<std::vector<uint8_t>, 13> result;
    for (int p = 1; p <= 12; ++p) {
        result[p].resize(work.labels.size());
        for (auto id : work.postings[p]) result[p][id] = 1;
    }
    return result;
}

// Count actual function calls, including the entry point, instead of hnswlib's adjacency statistic.
class CountNative {
public:
    CountNative(Graph& graph, uint64_t* count)
        : graph_(graph), original_(graph.fstdistfunc_), parameter_(graph.dist_func_param_), count_(count) {
        graph.fstdistfunc_ = evaluate;
        graph.dist_func_param_ = this;
    }
    ~CountNative() {
        graph_.fstdistfunc_ = original_;
        graph_.dist_func_param_ = parameter_;
    }
    CountNative(const CountNative&) = delete;
    CountNative& operator=(const CountNative&) = delete;

private:
    static float evaluate(const void* query, const void* item, const void* parameter) {
        const auto& self = *static_cast<const CountNative*>(parameter);
        ++*self.count_;
        return self.original_(query, item, self.parameter_);
    }
    Graph& graph_;
    hnswlib::DISTFUNC<float> original_;
    void* parameter_;
    uint64_t* count_;
};

Result native_search(Graph& graph, const Workload& work, size_t row, bool post, uint64_t* calls = nullptr) {
    const auto predicate = work.predicates[row];
    const auto count = post ? work.budget[predicate] : top_k;
    const auto* query = work.query(row);
    const auto candidates = [&] {
        if (!calls) return graph.searchKnnCloserFirst(query, count);
        CountNative scope(graph, calls);
        return graph.searchKnnCloserFirst(query, count);
    }();
    Result result;
    size_t rank = 0;
    for (auto [distance, id] : candidates) {
        if (id >= work.labels.size()) throw std::runtime_error("Invalid native label");
        if (post && work.labels[id] != predicate) continue;
        result.ids[rank] = id;
        result.distances[rank] = distance;
        if (++rank == top_k) break;
    }
    return result;
}

Result exact(const float* base, const std::vector<Node>& candidates, const float* query,
             hnswlib::DISTFUNC<float> l2, const void* dimension_param) {
    std::array<std::pair<float, Node>, top_k> best;
    size_t count = 0;
    for (const auto id : candidates) {
        const auto pair = std::make_pair(l2(query, base + id * dimension, dimension_param), id);
        if (count == top_k) {
            if (!(pair < best.front())) continue;
            std::pop_heap(best.begin(), best.end());
            best.back() = pair;
        } else {
            best[count++] = pair;
        }
        std::push_heap(best.begin(), best.begin() + count);
    }
    std::sort_heap(best.begin(), best.begin() + count);
    Result result;
    for (size_t i = 0; i < count; ++i) {
        result.ids[i] = best[i].second;
        result.distances[i] = best[i].first;
    }
    return result;
}

void check_result(const Result& result, const Workload& work, size_t row) {
    for (size_t i = 0; i < top_k; ++i) {
        const auto id = result.ids[i];
        if (id == -1) continue;
        if (id < 0 || size_t(id) >= work.labels.size() || work.labels[id] != work.predicates[row] ||
            std::find(result.ids.begin(), result.ids.begin() + i, id) != result.ids.begin() + i ||
            !std::isfinite(result.distances[i]) || (i && result.distances[i] < result.distances[i - 1])) {
            throw std::runtime_error("Invalid search result at row " + std::to_string(row));
        }
    }
}

void validate_graph(const Graph& graph, const std::vector<Node>* ids, size_t expected) {
    if (graph.cur_element_count != expected || graph.M_ != 32 || graph.ef_construction_ != 40 ||
        graph.maxM0_ != 64 || graph.enterpoint_node_ >= expected ||
        graph.element_levels_[graph.enterpoint_node_] != graph.maxlevel_) {
        throw std::runtime_error("Native graph metadata mismatch");
    }
    for (Node node = 0; node < expected; ++node) {
        if (graph.getExternalLabel(node) != (ids ? (*ids)[node] : node)) {
            throw std::runtime_error("Native index global ID mismatch");
        }
        for (int layer = 0; layer <= graph.element_levels_[node]; ++layer) {
            auto* list = graph.get_linklist_at_level(node, layer);
            const auto count = graph.getListCount(list);
            if (count > (layer == 0 ? 64 : 32)) throw std::runtime_error("Native capacity violation");
            for (int j = 1; j <= count; ++j) {
                const auto dest = list[j];
                if (dest >= expected || dest == node || graph.element_levels_[dest] < layer) {
                    throw std::runtime_error("Invalid native graph edge");
                }
            }
        }
    }
}

template <typename Create, typename Verify, typename Reload>
void persist(const fs::path& path, const Create& create, const Verify& verify, const Reload& reload) {
    const fs::path partial = path.string() + ".partial";
    if (fs::exists(path) || fs::exists(partial)) throw std::runtime_error("Index output exists");
    const auto begin = Clock::now();
    auto index = create();
    std::cout << "index_built " << path << " seconds=" << seconds(begin) << '\n';
    const auto expected = verify(*index);
    index->saveIndex(partial.string());
    index.reset();
    index = reload(partial.string());
    if (verify(*index) != expected) throw std::runtime_error("Index changed after reload");
    fs::rename(partial, path);
    std::cout << "reload_verified " << path << " bytes=" << fs::file_size(path) << '\n';
}

void build_main(const fs::path& input, const fs::path& output, size_t count, const std::string& mode) {
    const Workload work(input, count, sample);
    const auto vectors = read<float>(input / "train.f32", count * dimension);
    const auto filter = masks(work);
    hnswlib::L2Space space(dimension);
    for (int gamma : {12, 1}) {
        if (mode == "hnsw" || (mode == "gamma" && gamma == 1) || (mode == "one" && gamma == 12)) continue;
        const int prefix = gamma == 12 ? 64 : 32;
        const auto make = [&] { return std::make_unique<Acorn>(space, gamma, prefix); };
        persist(output / (gamma == 12 ? "acorn-gamma.index" : "acorn-one.index"), [&] {
            auto index = make();
            index->build(vectors.data(), count);
            return index;
        }, [&](Acorn& index) {
            index.validate();
            std::vector<Result> results;
            for (size_t row = 0; row < sample; ++row) {
                auto r = index.search(work.query(row), filter[work.predicates[row]].data(), 60);
                check_result(r, work, row);
                results.push_back(r);
            }
            return results;
        }, [&](const std::string& file) {
            auto index = make();
            index->load(file, space);
            return index;
        });
    }
    if (mode == "gamma" || mode == "one") return;
    persist(output / "hnsw.index", [&] {
        auto index = std::make_unique<Graph>(&space, count, 32, 40, 100);
        for (Node id = 0; id < count; ++id) index->addPoint(vectors.data() + id * dimension, id);
        return index;
    }, [&](Graph& index) {
        validate_graph(index, nullptr, count);
        index.setEf(60);
        std::vector<Result> results;
        for (size_t row = 0; row < sample; ++row) {
            auto r = native_search(index, work, row, true);
            check_result(r, work, row);
            results.push_back(r);
        }
        return results;
    }, [&](const std::string& path) { return std::make_unique<Graph>(&space, path); });

    if (count != nbase) {
        const auto l2 = space.get_dist_func();
        for (size_t row = 0; row < sample; ++row) {
            const auto& candidates = work.postings[work.predicates[row]];
            const auto result = exact(vectors.data(), candidates, work.query(row), l2, space.get_dist_func_param());
            std::vector<std::pair<double, Node>> reference;
            for (const auto id : candidates) {
                double distance = 0;
                for (size_t d = 0; d < dimension; ++d) {
                    const double delta = work.query(row)[d] - vectors[id * dimension + d];
                    distance += delta * delta;
                }
                reference.emplace_back(distance, id);
            }
            std::sort(reference.begin(), reference.end());
            for (size_t rank = 0; rank < top_k; ++rank) {
                if (result.ids[rank] != reference[rank].second || result.distances[rank] != reference[rank].first) {
                    throw std::runtime_error("Independent exact prefilter check failed");
                }
            }
        }
        std::cout << "exact_prefilter_checks=" << sample << '\n';
    }
}

void build_oracle(const fs::path& input, const fs::path& output, size_t count) {
    const Workload work(input, count, sample);
    const auto vectors = read<float>(input / "train.f32", count * dimension);
    hnswlib::L2Space space(dimension);
    size_t total = 0;
    for (int p = 1; p <= 12; ++p) {
        const auto& ids = work.postings[p];
        persist(output / ("oracle-" + std::to_string(p) + ".index"), [&] {
            auto index = std::make_unique<Graph>(&space, ids.size(), 32, 40, 100);
            for (auto id : ids) index->addPoint(vectors.data() + id * dimension, id);
            return index;
        }, [&](Graph& index) {
            validate_graph(index, &ids, ids.size());
            index.setEf(60);
            std::vector<Result> results;
            for (size_t row = 0; row < sample; ++row) {
                if (work.predicates[row] != p) continue;
                auto r = native_search(index, work, row, false);
                check_result(r, work, row);
                results.push_back(r);
            }
            return results;
        }, [&](const std::string& path) { return std::make_unique<Graph>(&space, path); });
        total += ids.size();
        std::cout << "partition=" << p << " rows=" << ids.size() << '\n';
    }
    if (total != count) throw std::runtime_error("Oracle coverage mismatch");
    std::cout << "partition_global_id_coverage=" << total << '\n';
}

class Measure {
public:
    Measure(const Workload& work, const fs::path& truth_path, const fs::path& out, bool counting)
        : work_(work), truth_(read<Id>(truth_path, nqueries * top_k)),
          ids_(nqueries), calls_(nqueries), counting_(counting) {
        for (const auto name : {"points.csv", "neighbors.i64", "distance-counts.u64", "effective-ef.csv"}) {
            if (fs::exists(out / name)) throw std::runtime_error("Measurement output exists");
        }
        points_.open(out / "points.csv");
        neighbors_.open(out / "neighbors.i64", std::ios::binary);
        effective_.open(out / "effective-ef.csv");
        if (counting) counts_.open(out / "distance-counts.u64", std::ios::binary);
        if (!points_ || !neighbors_ || !effective_ || (counting && !counts_)) {
            throw std::runtime_error("Could not create output");
        }
        points_ << std::fixed << std::setprecision(9)
                << "point,method,ef,queries,seconds,qps,recall_at_10,hits,missing,underfilled,invalid,duplicates";
        if (counting) points_ << ",distance_calls,mean_distance_calls";
        points_ << '\n';
        effective_ << "point,method,ef,predicate,candidate_budget,effective_ef\n";
    }

    template <typename Search>
    void run(const std::string& method, const Search& setup, bool exact_point = false) {
        const auto measure = [&](int ef) {
            return setup(ef);
        };
        if (exact_point) {
            measure(0);
        } else if (!counting_) {
            for (auto ef : widths) measure(ef);
        } else {
            constexpr int target = nqueries * top_k * 4 / 5;
            int low = widths[0], high = widths[1];
            int low_hits = measure(low), high_hits = measure(high);
            if (low_hits < target) {
                for (size_t i = 2; high_hits < target && i < widths.size(); ++i) {
                    low = high;
                    high = widths[i];
                    high_hits = measure(high);
                }
                while (high_hits > target && high - low > 1) {
                    const int middle = low + (high - low) / 2;
                    const int hits = measure(middle);
                    if (hits < target) low = middle;
                    else {
                        high = middle;
                        high_hits = hits;
                    }
                }
            }
        }
        std::cout << "method_finished " << method << '\n';
    }

    template <typename Search>
    int point(const std::string& method, int ef, const Search& search) {
        std::fill(calls_.begin(), calls_.end(), 0);
        if (counting_) {
            for (size_t row = 0; row < sample; ++row) {
                uint64_t count = 0;
                const auto measured = search(row, &count);
                if (!count || !(measured == search(row, nullptr))) throw std::runtime_error("Counting changed search");
            }
        }
        std::cout << "point_start method=" << method << " ef=" << ef << '\n';
        const auto start = Clock::now();
        for (size_t row = 0; row < nqueries; ++row) {
            ids_[row] = search(row, counting_ ? &calls_[row] : nullptr);
        }
        const double elapsed = seconds(start);
        int hits = 0, missing = 0, underfilled = 0;
        uint64_t total_calls = 0;
        for (size_t row = 0; row < nqueries; ++row) {
            check_result(ids_[row], work_, row);
            const auto* truth = truth_.data() + row * top_k;
            const auto& ids = ids_[row].ids;
            if (ef == 0 && !std::equal(ids.begin(), ids.end(), truth)) {
                throw std::runtime_error("Exact prefilter disagrees with truth");
            }
            const int absent = std::count(ids.begin(), ids.end(), -1);
            missing += absent;
            underfilled += absent != 0;
            for (auto id : ids) hits += id >= 0 && std::find(truth, truth + top_k, id) != truth + top_k;
            if (counting_ && calls_[row] == 0) throw std::runtime_error("Distance counter did not fire");
            total_calls += calls_[row];
            neighbors_.write(reinterpret_cast<const char*>(ids.data()), top_k * sizeof(Id));
        }
        const double recall = double(hits) / (nqueries * top_k);
        points_ << point_ << ',' << method << ',' << ef << ',' << nqueries << ',' << elapsed
                << ',' << nqueries / elapsed << ',' << recall << ',' << hits << ',' << missing
                << ',' << underfilled << ",0,0";
        if (counting_) {
            counts_.write(reinterpret_cast<const char*>(calls_.data()), calls_.size() * sizeof(uint64_t));
            points_ << ',' << total_calls << ',' << double(total_calls) / nqueries;
            counts_.flush();
        }
        points_ << '\n';
        if (method == "HNSW-postfilter") {
            for (int p = 1; p <= 12; ++p) {
                const auto budget = work_.budget[p];
                effective_ << point_ << ',' << method << ',' << ef << ',' << p << ',' << budget << ','
                           << std::max(size_t(ef), budget) << '\n';
            }
        }
        ++point_;
        points_.flush();
        neighbors_.flush();
        effective_.flush();
        if (!points_ || !neighbors_ || !effective_ || (counting_ && !counts_)) {
            throw std::runtime_error("Could not write output");
        }
        std::cout << "point_done method=" << method << " ef=" << ef << " recall=" << recall
                  << " qps=" << nqueries / elapsed << " mean_calls=" << double(total_calls) / nqueries << '\n';
        return hits;
    }

private:
    const Workload& work_;
    const std::vector<Id> truth_;
    std::vector<Result> ids_;
    std::vector<uint64_t> calls_;
    bool counting_;
    int point_ = 0;
    std::ofstream points_, neighbors_, effective_, counts_;
};

void measure(const fs::path& input, const fs::path& truth, const fs::path& main,
             const fs::path& oracle, const fs::path& output, bool counting) {
    const Workload work(input, nbase, nqueries);
    Measure run(work, truth, output, counting);
    hnswlib::L2Space space(dimension);
    const auto filter_start = Clock::now();
    const auto filter = masks(work);
    std::cout << "acorn_masks_seconds=" << seconds(filter_start) << '\n';
    for (int gamma : {12, 1}) {
        const bool wide = gamma == 12;
        const std::string name = wide ? "ACORN-gamma" : "ACORN-1";
        Acorn index(space, gamma, wide ? 64 : 32);
        index.load((main / (wide ? "acorn-gamma.index" : "acorn-one.index")).string(), space);
        if (index.size() != nbase) throw std::runtime_error("ACORN index size differs");
        run.run(name, [&](int ef) {
            return run.point(name, ef, [&](size_t row, uint64_t* calls) {
                return index.search(work.query(row), filter[work.predicates[row]].data(), ef, calls);
            });
        });
    }
    {
        const std::string name = "HNSW-postfilter";
        Graph index(&space, (main / "hnsw.index").string());
        if (index.cur_element_count != nbase) throw std::runtime_error("HNSW index size differs");
        run.run(name, [&](int ef) {
            index.setEf(ef);
            return run.point(name, ef, [&](size_t row, uint64_t* calls) {
                return native_search(index, work, row, true, calls);
            });
        });
    }
    if (counting) {
        const std::string name = "Oracle-Partition";
        std::array<std::unique_ptr<Graph>, 13> indexes;
        for (int p = 1; p <= 12; ++p) {
            indexes[p] = std::make_unique<Graph>(&space, (oracle / ("oracle-" + std::to_string(p) + ".index")).string());
            if (indexes[p]->cur_element_count != work.postings[p].size()) {
                throw std::runtime_error("Oracle index size differs");
            }
        }
        run.run(name, [&](int ef) {
            for (int p = 1; p <= 12; ++p) indexes[p]->setEf(ef);
            return run.point(name, ef, [&](size_t row, uint64_t* calls) {
                return native_search(*indexes[work.predicates[row]], work, row, false, calls);
            });
        });
    } else {
        const auto base = read<float>(input / "train.f32", nbase * dimension);
        const std::string name = "exact-prefilter";
        run.run(name, [&](int ef) {
            return run.point(name, ef, [&](size_t row, uint64_t*) {
                return exact(base.data(), work.postings[work.predicates[row]], work.query(row),
                             space.get_dist_func(), space.get_dist_func_param());
            });
        }, true);
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf << std::fixed << std::setprecision(6);
        if (argc < 2) throw std::runtime_error("Missing command");
        const std::string mode = argv[1];
        if ((mode == "gamma" || mode == "one" || mode == "hnsw" || mode == "build-main") &&
            (argc == 4 || argc == 5)) {
            build_main(argv[2], argv[3], argc == 5 ? std::stoul(argv[4]) : nbase, mode);
        } else if (mode == "build-oracle" && (argc == 4 || argc == 5)) {
            build_oracle(argv[2], argv[3], argc == 5 ? std::stoul(argv[4]) : nbase);
        } else if (mode == "figure" && argc == 6) {
            measure(argv[2], argv[3], argv[4], {}, argv[5], false);
        } else if (mode == "table" && argc == 7) {
            measure(argv[2], argv[3], argv[4], argv[5], argv[6], true);
        } else {
            throw std::runtime_error("Invalid command arguments");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
