// Tests a new source-derived property on synthetic data, NOT the reported server failures.
// No core changes. E < ef plus connected, scope-bounded backbone implies full expansion.
#include "closure_index.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        npi::Dataset data;
        data.n = 48; data.dim = 2; data.nq = 1;
        data.parent = {-1, 0, 0, 1, 1};
        for (size_t i = 0; i < data.n; ++i) {
            data.base.push_back(float(i) / 2);
            data.base.push_back(float(i * 7 % 11));
            data.items.push_back(npi::Item{npi::Id(i % 5), i % 13 == 0 ? 1 : 0,
                                           int64_t(i), uint64_t(i % 8), true});
        }
        data.tests = {1.25f, 2.0f}; data.rank_tree();
        closure::Index index(data, 8);
        RawL2Space raw(data.dim);
        const std::array<closure::Mode, 5> modes = {closure::Mode::Unpruned,
            closure::Mode::Geometric, closure::Mode::Interval, closure::Mode::Hierarchy,
            closure::Mode::Hierarchy};
        const std::array<const char*, 5> names = {"U", "G", "R", "H", "H0"};
        std::cout << "{\"complete\":true,\"kind\":\"synthetic_E_lt_ef_property_not_server_reproduction\",\"rows\":[";
        bool first = true;
        for (int category : {1, 99}) {
            npi::Query query;
            query.anchor = 0; query.recursive = 1; query.category = category;
            std::vector<closure::Neighbor> truth;
            for (size_t i = 0; i < data.n; ++i)
                if (data.items[i].category == category)
                    truth.emplace_back(raw.dist(data.tests.data(), data.base.data() + i * data.dim), npi::Id(i));
            const size_t eligible = truth.size();
            std::sort(truth.begin(), truth.end());
            if (truth.size() > 3) truth.resize(3);
            require(eligible < 8, "probe requires E < ef");
            for (size_t m = 0; m < modes.size(); ++m) {
                closure::Work work;
                const auto result = index.search(query, modes[m], 8, 3, work, m != 4);
                require(result == truth, "finite exhaustive result differs from raw exact GT");
                require(work.expanded == data.n && work.distance_calls == data.n,
                        "E < ef did not exhaust connected scope");
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"arm\":\"" << names[m] << "\",\"scope_card\":" << data.n
                          << ",\"eligible_card\":" << eligible << ",\"ef\":8,\"k\":3,\"expanded\":"
                          << work.expanded << ",\"distance_calls\":" << work.distance_calls
                          << ",\"retained\":" << work.retained << ",\"pruned\":" << work.pruned
                          << ",\"returned\":" << result.size() << '}';
            }
        }
        std::cout << "],\"limits\":[\"Proves a finite instance of a source-derived invariant; no real-data prevalence measured.\","
                  << "\"No timing, new ANN algorithm, general quality or dynamic claim.\"]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
