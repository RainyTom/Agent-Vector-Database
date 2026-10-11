#include "vector_join/vector_join.hpp"
#include "vector_join/simjoin_paper.hpp"
#include "vector_join/runtime.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <functional>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace py = pybind11;

namespace {

using Runner = std::function<vjoin::RunOutput(
    const vjoin::Matrix&, const vjoin::Matrix&, const vjoin::Config&, std::size_t)>;

vjoin::RunOutput run_files(const Runner& runner, const std::string& query_path,
                           const std::string& base_path, std::size_t dimensions,
                           const vjoin::Config& config, std::size_t query_limit,
                           std::size_t base_limit,
                           std::size_t ground_truth_limit) {
  const auto queries = vjoin::Matrix::load_raw(query_path, dimensions, query_limit);
  const auto base = vjoin::Matrix::load_raw(base_path, dimensions, base_limit);
  py::gil_scoped_release release;
  return runner(queries, base, config, ground_truth_limit);
}

void bind_runner(py::module_& module, const char* name, const Runner& runner) {
  module.def(
      name,
      [runner](const vjoin::Matrix& queries, const vjoin::Matrix& base,
               const vjoin::Config& config, std::size_t ground_truth_limit) {
        py::gil_scoped_release release;
        return runner(queries, base, config, ground_truth_limit);
      },
      py::arg("queries"), py::arg("base"), py::arg("config"),
      py::arg("ground_truth_limit") = 0);

  module.def(
      (std::string(name) + "_files").c_str(),
      [runner](const std::string& query_path, const std::string& base_path,
               std::size_t dimensions, const vjoin::Config& config,
               std::size_t query_limit, std::size_t base_limit,
               std::size_t ground_truth_limit) {
        return run_files(runner, query_path, base_path, dimensions, config,
                         query_limit, base_limit, ground_truth_limit);
      },
      py::arg("query_path"), py::arg("base_path"), py::arg("dimensions"),
      py::arg("config"), py::arg("query_limit") = 0,
      py::arg("base_limit") = 0, py::arg("ground_truth_limit") = 0);
}

}  // namespace

PYBIND11_MODULE(vector_join_py, module) {
  vjoin::enforce_process_limit();
  module.attr("MEMORY_LIMIT_BYTES") = py::int_(vjoin::memory_limit_bytes);
  module.doc() = "Python bindings for the separated vector-join reproductions";

  py::class_<vjoin::Matrix>(module, "Matrix")
      .def(py::init([](const std::vector<std::vector<float>>& rows) {
        if (rows.empty() || rows.front().empty()) {
          throw std::invalid_argument("matrix must be non-empty");
        }
        vjoin::Matrix matrix;
        matrix.rows = rows.size();
        matrix.dim = rows.front().size();
        matrix.values.reserve(matrix.rows * matrix.dim);
        for (const auto& row : rows) {
          if (row.size() != matrix.dim) {
            throw std::invalid_argument("matrix rows must have equal dimensions");
          }
          matrix.values.insert(matrix.values.end(), row.begin(), row.end());
        }
        return matrix;
      }))
      .def_static("load_raw", &vjoin::Matrix::load_raw, py::arg("path"),
                  py::arg("dimensions"), py::arg("max_rows") = 0)
      .def_readonly("rows", &vjoin::Matrix::rows)
      .def_readonly("dimensions", &vjoin::Matrix::dim);

  py::class_<vjoin::Config>(module, "Config")
      .def(py::init<>())
      .def_readwrite("threshold", &vjoin::Config::threshold)
      .def_readwrite("graph_m", &vjoin::Config::graph_m)
      .def_readwrite("ef_construction", &vjoin::Config::ef_construction)
      .def_readwrite("greedy_beam", &vjoin::Config::greedy_beam)
      .def_readwrite("hybrid_beam", &vjoin::Config::hybrid_beam)
      .def_readwrite("convergence_threshold", &vjoin::Config::convergence_threshold)
      .def_readwrite("ood_ratio_threshold", &vjoin::Config::ood_ratio_threshold)
      .def_readwrite("seed", &vjoin::Config::seed);

  py::class_<vjoin::QueryResult>(module, "QueryResult")
      .def_readonly("matches", &vjoin::QueryResult::matches)
      .def_readonly("distance_computations", &vjoin::QueryResult::distance_computations)
      .def_readonly("greedy_distance_computations",
                    &vjoin::QueryResult::greedy_distance_computations)
      .def_readonly("expansion_distance_computations",
                    &vjoin::QueryResult::expansion_distance_computations)
      .def_readonly("hybrid_used", &vjoin::QueryResult::hybrid_used);

  py::class_<vjoin::Metrics>(module, "Metrics")
      .def_readonly("method", &vjoin::Metrics::method)
      .def_readonly("graph_backend", &vjoin::Metrics::graph_backend)
      .def_readonly("implementation_language",
                    &vjoin::Metrics::implementation_language)
      .def_readonly("distance_metric", &vjoin::Metrics::distance_metric)
      .def_readonly("base_rows", &vjoin::Metrics::base_rows)
      .def_readonly("query_rows", &vjoin::Metrics::query_rows)
      .def_readonly("dimensions", &vjoin::Metrics::dimensions)
      .def_readonly("threshold", &vjoin::Metrics::threshold)
      .def_readonly("graph_m", &vjoin::Metrics::graph_m)
      .def_readonly("ef_construction", &vjoin::Metrics::ef_construction)
      .def_readonly("greedy_beam", &vjoin::Metrics::greedy_beam)
      .def_readonly("hybrid_beam", &vjoin::Metrics::hybrid_beam)
      .def_readonly("convergence_threshold", &vjoin::Metrics::convergence_threshold)
      .def_readonly("ood_ratio_threshold", &vjoin::Metrics::ood_ratio_threshold)
      .def_readonly("seed", &vjoin::Metrics::seed)
      .def_readonly("build_ms", &vjoin::Metrics::build_ms)
      .def_readonly("order_ms", &vjoin::Metrics::order_ms)
      .def_readonly("join_ms", &vjoin::Metrics::join_ms)
      .def_readonly("result_pairs", &vjoin::Metrics::result_pairs)
      .def_readonly("distance_computations", &vjoin::Metrics::distance_computations)
      .def_readonly("greedy_distance_computations",
                    &vjoin::Metrics::greedy_distance_computations)
      .def_readonly("expansion_distance_computations",
                    &vjoin::Metrics::expansion_distance_computations)
      .def_readonly("hybrid_queries", &vjoin::Metrics::hybrid_queries)
      .def_readonly("evaluated_ground_truth_queries",
                    &vjoin::Metrics::evaluated_ground_truth_queries)
      .def_readonly("macro_recall", &vjoin::Metrics::macro_recall)
      .def_readonly("pair_recall", &vjoin::Metrics::pair_recall)
      .def_readonly("precision", &vjoin::Metrics::precision)
      .def("to_json", &vjoin::metrics_json);

  py::class_<vjoin::RunOutput>(module, "RunOutput")
      .def_readonly("metrics", &vjoin::RunOutput::metrics)
      .def_readonly("queries", &vjoin::RunOutput::queries);

  bind_runner(module, "run_independent", vjoin::run_independent);
  bind_runner(module, "run_hard_window_cache", vjoin::run_hard_window_cache);
  bind_runner(module, "run_fast_avj_soft", vjoin::run_fast_avj_soft);
  bind_runner(module, "run_fast_avj_merged", vjoin::run_fast_avj_merged);
  bind_runner(module, "run_fast_avj_adaptive", vjoin::run_fast_avj_adaptive);

  py::enum_<vjoin::simjoin::OrderMethod>(module, "SimJoinOrder")
      .value("BASE_MST", vjoin::simjoin::OrderMethod::kBaseMST)
      .value("WINDOW_ORDER_SORT", vjoin::simjoin::OrderMethod::kWindowOrderSort)
      .value("WINDOW_ORDER_EPSILON", vjoin::simjoin::OrderMethod::kWindowOrderEpsilon);

  py::class_<vjoin::simjoin::Config>(module, "SimJoinConfig")
      .def(py::init<>())
      .def_readwrite("epsilon", &vjoin::simjoin::Config::epsilon)
      .def_readwrite("window_size", &vjoin::simjoin::Config::window_size)
      .def_readwrite("order_search_window", &vjoin::simjoin::Config::order_search_window)
      .def_readwrite("order_repeats", &vjoin::simjoin::Config::order_repeats)
      .def_readwrite("k", &vjoin::simjoin::Config::k)
      .def_readwrite("order", &vjoin::simjoin::Config::order)
      .def_readwrite("self_join", &vjoin::simjoin::Config::self_join)
      .def_readwrite("graph_m", &vjoin::simjoin::Config::graph_m)
      .def_readwrite("ef_construction", &vjoin::simjoin::Config::ef_construction)
      .def_readwrite("seed", &vjoin::simjoin::Config::seed);

  py::class_<vjoin::simjoin::QueryResult>(module, "SimJoinQueryResult")
      .def_readonly("matches", &vjoin::simjoin::QueryResult::matches)
      .def_readonly("distance_computations",
                    &vjoin::simjoin::QueryResult::distance_computations);

  py::class_<vjoin::simjoin::Metrics>(module, "SimJoinMetrics")
      .def_readonly("method", &vjoin::simjoin::Metrics::method)
      .def_readonly("implementation_status",
                    &vjoin::simjoin::Metrics::implementation_status)
      .def_readonly("implementation_language",
                    &vjoin::simjoin::Metrics::implementation_language)
      .def_readonly("compiler", &vjoin::simjoin::Metrics::compiler)
      .def_readonly("graph_backend", &vjoin::simjoin::Metrics::graph_backend)
      .def_readonly("estimated_peak_bytes",
                    &vjoin::simjoin::Metrics::estimated_peak_bytes)
      .def_readonly("average_recall", &vjoin::simjoin::Metrics::average_recall)
      .def_readonly("average_precision", &vjoin::simjoin::Metrics::average_precision)
      .def("to_json", &vjoin::simjoin::metrics_json);

  py::class_<vjoin::simjoin::RunOutput>(module, "SimJoinRunOutput")
      .def_readonly("metrics", &vjoin::simjoin::RunOutput::metrics)
      .def_readonly("queries", &vjoin::simjoin::RunOutput::queries);

  module.def(
      "run_simjoin_files",
      [](const std::string& query_path, const std::string& base_path,
         std::size_t dimensions, const vjoin::simjoin::Config& config,
         std::size_t query_limit, std::size_t base_limit,
         std::size_t ground_truth_limit) {
        vjoin::enforce_process_limit();
        const auto base = vjoin::Matrix::load_raw(base_path, dimensions, base_limit);
        if (config.self_join && std::filesystem::equivalent(query_path, base_path) &&
            (query_limit == 0 || query_limit == base_limit)) {
          py::gil_scoped_release release;
          return vjoin::simjoin::run_hnsw(base, base, config, ground_truth_limit);
        }
        const auto queries = vjoin::Matrix::load_raw(query_path, dimensions, query_limit);
        py::gil_scoped_release release;
        return vjoin::simjoin::run_hnsw(queries, base, config, ground_truth_limit);
      },
      py::arg("query_path"), py::arg("base_path"), py::arg("dimensions"),
      py::arg("config"), py::arg("query_limit") = 0,
      py::arg("base_limit") = 0, py::arg("ground_truth_limit") = 0);

  module.def(
      "run_simjoin_graph_files",
      [](const std::string& query_path, const std::string& base_path,
         std::size_t dimensions, const std::string& query_graph_path,
         const std::string& base_graph_path, std::uint32_t query_entry,
         std::uint32_t base_entry, const std::string& graph_label,
         const std::string& graph_provenance,
         const vjoin::simjoin::Config& config, std::size_t query_limit,
         std::size_t base_limit, std::size_t ground_truth_limit) {
        vjoin::enforce_process_limit();
        const auto base = vjoin::Matrix::load_raw(base_path, dimensions, base_limit);
        const auto base_graph = vjoin::simjoin::load_graph(
            base_graph_path, base.rows, base_entry,
            "external-user-declared/" + graph_label,
            graph_provenance);
        if (config.self_join && std::filesystem::equivalent(query_path, base_path) &&
            (query_limit == 0 || query_limit == base_limit)) {
          py::gil_scoped_release release;
          return vjoin::simjoin::run(base, base, base_graph, base_graph, config,
                                     ground_truth_limit);
        }
        const auto queries = vjoin::Matrix::load_raw(query_path, dimensions, query_limit);
        const auto query_graph = vjoin::simjoin::load_graph(
            query_graph_path, queries.rows, query_entry,
            "external-user-declared/" + graph_label,
            graph_provenance);
        py::gil_scoped_release release;
        return vjoin::simjoin::run(queries, base, query_graph, base_graph, config,
                                   ground_truth_limit);
      },
      py::arg("query_path"), py::arg("base_path"), py::arg("dimensions"),
      py::arg("query_graph_path"), py::arg("base_graph_path"),
      py::arg("query_entry"), py::arg("base_entry"), py::arg("graph_label"),
      py::arg("graph_provenance"), py::arg("config"),
      py::arg("query_limit") = 0, py::arg("base_limit") = 0,
      py::arg("ground_truth_limit") = 0);
}
