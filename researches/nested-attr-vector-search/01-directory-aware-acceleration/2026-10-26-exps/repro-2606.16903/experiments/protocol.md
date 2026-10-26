# 实验协议(复现命令)

日期:2026-10-26。代码:`codes/directory-aware-vector-attr-search/`
(`src/repro.cpp`、`scripts/gen_workload.py`、`scripts/plot_repro.py`)。
数据 fixture:`codes/directory-aware-vector-attr-search/data/sift100k/`
(gitignore,由命令 1 重新生成)。

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
REPRO=researches/nested-attr-vector-search/01-directory-aware-acceleration/2026-10-26-exps/repro-2606.16903
cd codes/directory-aware-vector-attr-search
LD_LIBRARY_PATH=../../deps/PathSeer/faiss/build/faiss \
  ./build/repro --data data/sift100k --csv ../../$REPRO/data/results --mode all

# 4. 绘图(PNG+SVG 写入 $REPRO/figures)
cd ../..
production/nested-attr-vector-search/work/venv/bin/python \
  codes/directory-aware-vector-attr-search/scripts/plot_repro.py

# 可选自检:DSM 逐 op 重算 Inclusive 集合 audit(慢 ~3 倍)
DSM_AUDIT_EACH=1 ./build/repro --data data/sift100k \
  --csv experiments/audit --mode dsm
```

单次全量运行墙钟 ≈ 63s(WSL2 单线程计时)。

产物(本目录上一级,即 repro-2606.16903/ 下):
- `data/results_dsq.csv` — 每 (数据集, 设计, 执行器, 查询类型, scope 桶):
  nDCG@10、Recall@10、directory-only 延迟 mean/P90/P95/P99/P99.9(µs)、
  端到端延迟 mean(ms)。
- `data/results_dsm.csv` — 每 (设计, op):mean/P95/P99(ms)。
- `data/results_size.csv` — 目录模块构建时间(s)与序列化大小(MB);
  执行器构建/大小在 stdout。
- `figures/fig_repro_*.png|svg` — 由 `codes/directory-aware-vector-attr-search/
  scripts/plot_repro.py` 生成。

已知运行间方差:DSQ directory-only 均值 ±10% 以内(单线程、无 warmup、
op 间共享缓存);结论排序在多次运行中稳定。
