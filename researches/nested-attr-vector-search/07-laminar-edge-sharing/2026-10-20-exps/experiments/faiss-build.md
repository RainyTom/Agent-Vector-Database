# 实验记录：PathSeer FAISS fork 构建与过滤接口

日期：2026-10-20。目的：为 ACORN / Filtered-HNSW / NaviX / PathSeer 基线准备可运行的 FAISS 基座。

## 1. 代码与数据

- 代码：`git clone --depth 1 https://github.com/lizhiyuell/PathSeer .tmp/PathSeer`（本地临时目录，不入库）。
- 数据：PathSeer 的 HuggingFace 数据集 `lizhiyuell/filtered_ANNS_dataset`（用于核验真实标签/选择率；本研究实验使用 SIFT100k + 自建 CCS 形状标签）。

## 2. 构建

```bash
cd .tmp/PathSeer/faiss
cmake -DFAISS_ENABLE_GPU=OFF -DFAISS_ENABLE_PYTHON=OFF -DBUILD_TESTING=OFF \
      -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-march=native" -B build
make -C build -j10 faiss
```

结果：`build/faiss/libfaiss.so` 构建成功（2026-10-20）。

两处构建修复（只作用于 `.tmp` 克隆，不修改研究仓库）：

1. CMake 4.2 拒绝捆绑的 `nlohmann_json` 旧 `cmake_minimum_required`；加 `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`。
2. `faiss/impl/NaviX.cpp` 使用了 `std::array` 但未包含 `<array>`；在文件顶部补 `#include <array>`。

## 3. 过滤接口核验

- ACORN：`SearchParametersACORN` 继承 `SearchParameters`，可设置 `sel`（`IDSelector*`）；`IndexACORNFlat(d, M, gamma, metadata, M_beta, METRIC_L2)` 构图。
- FAISS HNSW：`SearchParametersHNSW` 同样支持 `sel`。
- `IDSelectorBitmap(size_t n, const uint8_t* bitmap)` 位于 `faiss/impl/IDSelector.h`；`IDSelectorBitmapLatency` 是 benchmark 的薄封装。

结论：Exact/Subtree 都只需为每个查询生成一个可行位图，无需修改过滤算法或做祖先闭包编码。`IndexACORNFlat` 的 `metadata` 是构图用的每向量整数标签；若只用位图谓词，可用占位 metadata（图不做过滤感知剪枝），结果应标注为“filter-agnostic ACORN”，不能宣称完整复现。

## 4. 尚未完成

- 最小驱动：加载 `base.f32`/`test.f32`/位图/golden → 构建 `IndexACORNFlat` 与 `IndexHNSWFlat` → 每查询 `IDSelectorBitmap` → 记录吞吐/召回/索引字节/构建时间。
- 与 B0/B4 在同一 1000 条 Subtree 查询和同一 oracle 下比较。
- 位图构造时间与峰值内存单独计量。
