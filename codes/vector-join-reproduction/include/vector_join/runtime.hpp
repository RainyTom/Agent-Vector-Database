#pragma once

#include <sys/resource.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef VJ_MEMORY_LIMIT_BYTES
#define VJ_MEMORY_LIMIT_BYTES 2000000000ULL
#endif

namespace vjoin {
inline constexpr std::uint64_t memory_limit_bytes = VJ_MEMORY_LIMIT_BYTES;

// Enforced for CLI, Python bindings and direct C++ entry points. This limits
// address space, not just vector payload. Preserve stricter inherited limits.
inline void enforce_process_limit() {
  rlimit limit{};
  if (getrlimit(RLIMIT_AS, &limit) != 0) throw std::runtime_error("getrlimit failed");
  limit.rlim_max = std::min(limit.rlim_max, static_cast<rlim_t>(memory_limit_bytes));
  limit.rlim_cur = std::min(limit.rlim_cur, limit.rlim_max);
  if (setrlimit(RLIMIT_AS, &limit) != 0) throw std::runtime_error("setrlimit failed");
}

inline std::uint64_t peak_rss_bytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("getrusage failed");
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;  // Linux
}

inline std::uint64_t saturated_add(std::uint64_t a, std::uint64_t b) {
  const auto max = std::numeric_limits<std::uint64_t>::max();
  return b > max - a ? max : a + b;
}

inline std::string json_string(const std::string& value) {
  std::ostringstream out;
  out << '"';
  for (unsigned char ch : value) {
    switch (ch) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (ch < 0x20) out << "\\u" << std::hex << std::setw(4)
                           << std::setfill('0') << static_cast<unsigned>(ch) << std::dec;
        else out << ch;
    }
  }
  out << '"';
  return out.str();
}

inline std::string json_number(double value) {
  if (!std::isfinite(value)) return "null";
  std::ostringstream out;
  out << std::setprecision(17) << value;
  return out.str();
}
}  // namespace vjoin
