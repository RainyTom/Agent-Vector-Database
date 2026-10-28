# 2026-10-29：NodeANN-first 与 hnswlib 公平性重置

## 当日目标

根据审核意见，撤销“直接从 FAISS 全局图 + HLG 开始”的执行顺序，先在统一 hnswlib 后端下重建 NodeANN，测清其缺陷，再决定局部搜索、跨节点调度、量化或共享图的下一步。

## 决策

1. 全局精确扫描不作为主方案；仅允许小直接节点使用精确扫描，阈值按 d=128/1024 的独立 microbenchmark 冻结。
2. 检索索引统一 hnswlib；FAISS 仅用于离线 k-means。若量化，仅采用 IGP-style VQ + residual SQ。
3. 原始 FP32 squared-L2 统一调用固定 hnswlib revision 的 `L2Space::get_dist_func()`；GT、精确小节点、HNSW、centroid/radius、未来 MST/raw rerank 均不得自写另一套距离。
4. 主数据集为 WIKI 与 SIFT1M。SIFT1M 使用 4,096 固定叶子，构造深度 D4/D6/D12 × balanced/Zipf-skewed 共六个单绑定层级变体。
5. 旧 FAISS HNSW/Flat/IVF、run14 与旧 HLG 数字仅作历史证据；它们不能与新 hnswlib 结果直接比较。活动基线全部重跑。

完整设计、SIFT 参数、基线矩阵、阶段门与停止条件见 [NodeANN-first hnswlib 方案](nodeann-first-hnswlib-plan.md)。

## 已核实的实现事实

- 当前 `repro.cpp` 的全局 HNSW、局部 NodeANN、Flat、IVF 都是 FAISS；`build.sh` 同时链接 FAISS。
- 旧 HLG 工具直接读取 FAISS `IndexHNSWFlat` 邻接，MST/search 距离为自写 double 标量 `dist2`，不满足新的统一距离合同。
- 当前冻结 harness 硬编码 FAISS library path、现有 `repro` 命令与 NumPy float32 GT；不能在原 autoresearch 段内继续追加新后端结果。
- 当前工作区没有 hnswlib headers；仓库另一复现模块已预设 revision `ca426729609b3221563047ceb269cc1213e0d376`，新方向拟复用该固定版本。
- 该 revision 的 `L2Space` 会根据编译 ISA 与维度选择内部函数；SIFT d=128、WIKI d=1024 均整除 16。统一调用的是公共模块，不作为创新。
- 现有 SIFT1M exporter 只导出向量和随机谓词，不生成层级；现有 `gen_workload.py` 仅是固定 SIFT100k wiki-like/arxiv-like 生成器，因此六变体需要新的、独立 manifest 生成流程。

## 本轮未运行

未拉取依赖、未改源码、未建索引、未生成 SIFT1M 层级、未重算 GT、未跑性能。本文只有设计与代码现状核查，不产生新性能结论。

## 审核后实施顺序

1. 固定 hnswlib + 统一 RawL2Space；
2. 新 experiment segment/harness；
3. B0 纯 hnswlib NodeANN、B1 小节点 hybrid、B3 exact diagnostic oracle；
4. WIKI + SIFT1M 六变体完整重跑；
5. 用逐查询计数确认 D1–D6；
6. 依次消融 O1 global heap、O2 节点球下界、O3 global-cutoff-aware local HNSW；
7. 仅在证据触发时进入 IGP 量化或共享图/HLG。
