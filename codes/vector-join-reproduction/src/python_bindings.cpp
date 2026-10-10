#include "vector_join/vector_join.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <functional>
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
  bind_runner(module, "run_simjoin_hard", vjoin::run_simjoin_hard);
  bind_runner(module, "run_fast_avj_soft", vjoin::run_fast_avj_soft);
  bind_runner(module, "run_fast_avj_merged", vjoin::run_fast_avj_merged);
  bind_runner(module, "run_fast_avj_adaptive", vjoin::run_fast_avj_adaptive);
}
