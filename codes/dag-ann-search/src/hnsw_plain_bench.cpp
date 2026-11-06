// 参照基线：单张 hnswlib 全局图在**无谓词**下的每查询耗时。
// 目的：把"ACORN 的实现开销"与"单图本身慢"分开。
// 与 NodeANN / B-scan 同口径：PQ 还原 float32 + 项目固定标量距离（RawL2Space）+ 单线程查询。
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "hnswlib/hnswlib.h"
#include "raw_l2_space.h"

static bool readf(const std::string& p, std::vector<float>& v) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "missing %s\n", p.c_str()); return false; }
    std::fseek(f, 0, SEEK_END);
    const long b = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    v.resize(size_t(b) / sizeof(float));
    const bool ok = std::fread(v.data(), sizeof(float), v.size(), f) == v.size();
    std::fclose(f);
    return ok;
}

int main(int argc, char** argv) {
    std::string dir = "work/dag-ann", vec = "recon.bin";
    int M = 32, efC = 200, k = 50, nq_max = 200;
    std::vector<int> efs{10, 50, 100, 200};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto nx = [&]() { return std::string(argv[++i]); };
        if (a == "--dir") dir = nx();
        else if (a == "--vec") vec = nx();
        else if (a == "--M") M = std::stoi(nx());
        else if (a == "--efC") efC = std::stoi(nx());
        else if (a == "--k") k = std::stoi(nx());
        else if (a == "--queries") nq_max = std::stoi(nx());
        else if (a == "--efs") {
            efs.clear();
            std::stringstream ss(nx());
            std::string t;
            while (std::getline(ss, t, ',')) if (!t.empty()) efs.push_back(std::stoi(t));
        }
    }

    std::vector<float> base, queries;
    if (!readf(dir + "/" + vec, base) || !readf(dir + "/queries.bin", queries)) return 2;
    const size_t n = base.size() / 128;
    const int nq = std::min<int>(int(queries.size() / 128), nq_max);

    RawL2Space space(128);
    auto* idx = new hnswlib::HierarchicalNSW<float>(&space, n, M, efC, 100);

    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < n; ++i) idx->addPoint(base.data() + i * 128, i);
    const double build_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    for (int ef : efs) {
        idx->setEf(ef);
        const auto t1 = std::chrono::steady_clock::now();
        for (int qi = 0; qi < nq; ++qi) {
            auto r = idx->searchKnn(queries.data() + size_t(qi) * 128, k);
            volatile size_t z = r.size();
            (void)z;
        }
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        std::printf("{\"method\":\"hnswlib_plain\",\"n\":%zu,\"M\":%d,\"efC\":%d,\"ef\":%d,\"k\":%d,"
                    "\"queries\":%d,\"qps\":%.1f,\"us_per_query\":%.1f,\"build_s\":%.1f}\n",
                    n, M, efC, ef, k, nq, nq / s, 1e6 * s / nq, build_s);
        std::fflush(stdout);
    }
    delete idx;
    return 0;
}
