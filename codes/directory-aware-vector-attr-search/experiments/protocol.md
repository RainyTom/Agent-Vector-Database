# 实验协议(复现命令)

日期:2026-10-26。代码:`../src/repro.cpp`、`../scripts/gen_workload.py`。
数据:`../data/sift100k/`(gitignore,由命令 1 重新生成)。

```bash
# 仓库根目录
cd /home/yiming/projects/scholar-workspace/vector-database

# 1. 生成 SIFT100k + 合成层级属性(seed=20261026,确定性)
production/nested-attr-vector-search/work/venv/bin/python \
  codes/directory-aware-vector-attr-search/scripts/gen_workload.py \
  --out codes/directory-aware-vector-attr-search/data/sift100k

# 2. 构建(CRoaring v4.3.1 静态库需已构建于 deps/croaring/build;
#    FAISS 使用 deps/PathSeer/faiss 构建产物)
codes/directory-aware-vector-attr-search/build.sh

# 3. DSQ + DSM 全量实验(HNSW efSearch=2048,IVF nprobe=256)
cd codes/directory-aware-vector-attr-search
LD_LIBRARY_PATH=../../deps/PathSeer/faiss/build/faiss \
  ./build/repro --data data/sift100k --csv experiments/results --mode all

# 可选自检:DSM 逐 op 重算 Inclusive 集合 audit(慢 ~3 倍)
DSM_AUDIT_EACH=1 ./build/repro --data data/sift100k \
  --csv experiments/audit --mode dsm
```

单次全量运行墙钟 ≈ 63s(WSL2 单线程计时)。

产物:
- `results_dsq.csv` — 每 (数据集, 设计, 执行器, 查询类型, scope 桶):
  nDCG@10、Recall@10、directory-only 延迟 mean/P90/P95/P99/P99.9(µs)、
  端到端延迟 mean(ms)。
- `results_dsm.csv` — 每 (设计, op):mean/P95/P99(ms)。
- `results_size.csv` — 目录模块构建时间(s)与序列化大小(MB);
  执行器构建/大小在 stdout。

已知运行间方差:DSQ directory-only 均值 ±10% 以内(单线程、无 warmup、
op 间共享缓存);结论排序在多次运行中稳定。
