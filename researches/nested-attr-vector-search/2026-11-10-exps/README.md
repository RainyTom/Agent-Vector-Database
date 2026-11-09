# 2026-11-10 DSM 实验：复现说明

目录作用域维护（DSM）实验的最小复现手册。所有数字由脚本产出，CSV/JSON 是唯一事实来源；本文档不重复数字，只说明怎么跑、参数是什么、列是什么意思、哪些项没测。

## 环境

| 项 | 值 |
|---|---|
| 编译器 | g++ 15.2.0，`-O3 -march=native` |
| hnswlib | `codes/third_party/hnswlib` 固定版本 `ca426729609b3221563047ceb269cc1213e0d376` |
| 距离 | 一律经 `codes/shared/raw_l2_space.h` → hnswlib `L2Space::get_dist_func()` |
| faiss | **全程不调用**（含官方 ACORN 基线，本轮已移除 faiss 依赖） |
| Python | 3.13，numpy / matplotlib（仅数据生成、调度与绘图） |
| CPU | Ryzen 7 5800X，16 逻辑核，WSL2 |

## 随机种子与参数

| 项 | 值 |
|---|---|
| 层级生成种子 | `42` |
| 叶子行数上限 | `50,000` |
| 偏斜树分支 | `6..40`，目标叶数 `100,000`，Zipf α=`1.2` |
| hnswlib 建图 | `M=32`、`ef_construction=40`、建图线程 16、随机种子 `42` |
| 检索 | `k=10`；ef 网格 `10,50,100,150,200,250,300,350,400,450,500,550,600,650,700,750,800` |
| 子树规模 | 请求 `1k,5k,10k,50k,100k,300k`（取树中最近规模，记录实际值） |
| DSM 重复 | **1 次/单元**（不做重复；`repeats` 列恒为 1） |

## 命令

### 0. 编译

```bash
cd /home/daihua/projs/2026040404-yiming/Agent-Vector-Database
bash codes/hnswlib-runner/build.sh
bash codes/ACORN/build.sh
bash codes/NodeANN/build.sh
```

### 1. 复现质量校验（Step 0）

```bash
./codes/hnswlib-runner/build/hnsw_runner build \
  --base datasets/sift1m/raw/sift_base.fvecs \
  --index work/hnsw-m32-efc40.bin --M 32 --efC 40 --threads 16 | tee work/step0_build.log

./codes/hnswlib-runner/build/hnsw_runner search \
  --query datasets/sift1m/raw/sift_query.fvecs \
  --gt datasets/sift1m/raw/sift_groundtruth.ivecs \
  --index work/hnsw-m32-efc40.bin \
  --efs 16,32,64,128,256 --k 10 --queries 10000 | tee work/step0_search.jsonl
```

判据：`ef=128` 的 `recall ≥ 0.90`。低于则先修实现，不要往下走。

### 2. 合成层级

```bash
H=researches/nested-attr-vector-search/2026-11-10-exps
python3 $H/experiments/build_hierarchy.py --out $H/data/hier
```

产出 `data/hier/{uniform,skewed}.npz` 与 `data/hier/trees.json`。

### 3. DSM 主表

```bash
H=researches/nested-attr-vector-search/2026-11-10-exps
for T in uniform skewed; do
  python3 $H/experiments/dsm_harness.py --hier $H/data/hier --tree $T \
    --sizes 1000,5000,10000,50000,100000,300000 \
    --out $H/data/results_dsm_$T.csv
done
```

### 4. 查询层 / 并发 / IndexFlat 对照 / 索引不变量

```bash
H=researches/nested-attr-vector-search/2026-11-10-exps
python3 $H/experiments/dsm_query_concurrency.py --hier $H/data/hier \
  --tree uniform --out-dir $H/data \
  --index work/hnsw-m32-efc40.bin --concurrency-seconds 15
```

### 5. 合成数据集（Pareto 输入）

均匀树转成统一数据集格式（`base.bin` 为裸 float32，需从 fvecs 剥掉 4 字节维度头）：

```bash
python3 - <<'PY'
import numpy as np, os
D='datasets/synth-uniform'; H='researches/nested-attr-vector-search/2026-11-10-exps/data/hier'
os.makedirs(D, exist_ok=True)
p=np.load(f'{H}/uniform.npz', allow_pickle=True)
parent, leaves, leaf_of_row = p['parent'], p['leaves'], p['leaf_of_row']
depth=np.asarray(p['depth_of']); n=parent.size
children=[[] for _ in range(n)]
for i in range(1,n): children[int(parent[i])].append(i)
with open(f'{D}/edges.tsv','w') as f:
    for i in range(1,n): f.write(f'{i} {int(parent[i])}\n')
np.savetxt(f'{D}/objects.txt', leaf_of_row, fmt='%d')
with open('datasets/sift1m/raw/sift_base.fvecs','rb') as f, open(f'{D}/base.bin','wb') as o:
    for _ in range(1_000_000): f.read(4); o.write(f.read(512))
rows=np.zeros(n,dtype=np.int64); rows[:]=np.bincount(leaf_of_row,minlength=n)
for i in np.argsort(depth,kind='stable')[::-1]:
    if i: rows[int(parent[i])]+=rows[i]
rng=np.random.default_rng(42); scopes=[]
for lvl in range(4):
    idx=np.nonzero((depth==lvl)&(rows>0))[0]
    take=max(1,min(idx.size,int(round(200*idx.size/((rows>0)).sum()))))
    scopes += [int(v) for v in rng.choice(idx,size=take,replace=False)]
scopes=sorted(set(scopes))
while len(scopes)<200:
    c=int(rng.choice(np.nonzero((depth>=1)&(rows>0))[0]))
    if c not in scopes: scopes.append(c)
scopes=scopes[:200]
with open('datasets/sift1m/raw/sift_query.fvecs','rb') as f, open(f'{D}/queries.bin','wb') as o:
    for _ in range(200): f.read(4); o.write(f.read(512))
open(f'{D}/queries.tsv','w').write(''.join(f'1 {s}\n' for s in scopes))
base=np.memmap(f'{D}/base.bin',dtype=np.float32,mode='r').reshape(-1,128)
q=np.fromfile(f'{D}/queries.bin',dtype=np.float32).reshape(-1,128)
def clo(s):
    out=set(); st=[s]
    while st:
        c=st.pop()
        if c in out: continue
        out.add(c); st.extend(children[c])
    return out
gt=np.full((200,10),-1,dtype=np.int32)
for i,s in enumerate(scopes):
    ids=np.nonzero(np.isin(leaf_of_row,list(clo(s))))[0]
    d=((base[ids]-q[i])**2).sum(1); gt[i]=ids[np.argsort(d,kind='stable')[:10]]
gt.tofile(f'{D}/gt_10.txt')
print('ready', len(scopes))
PY
```

判据：`base.bin` 512,000,000 B、`queries.bin` 102,400 B、`gt_10.txt` 8,000 B。

### 6. 建索引（M=32、efC=40）

```bash
D=datasets/synth-uniform
./codes/NodeANN/build/nodeann build --data $D --index-dir work/synth-nodeann \
  --M 32 --ef-construction 40 | tee work/synth_nodeann_build.log

./codes/ACORN/build/acorn_main build --data $D --index work/synth-acorn.bin \
  --M 32 --gamma 12 --M-beta 64 --efC 40 | tee work/synth_acorn_build.log
```

### 7. 帕累托扫描

```bash
D=datasets/synth-uniform; H=researches/nested-attr-vector-search/2026-11-10-exps
EFS=10,50,100,150,200,250,300,350,400,450,500,550,600,650,700,750,800

./codes/NodeANN/build/nodeann search --data $D --index-dir work/synth-nodeann \
  --per-n 10 --k 10 --ef $EFS --queries 200 > $H/data/pareto_nodeann.jsonl

./codes/ACORN/build/acorn_main search --data $D --index work/synth-acorn.bin \
  --k 10 --efs $EFS --queries 200 --M-beta 64 --M 32 > $H/data/pareto_acorn.jsonl
```

### 8. 出图

```bash
H=researches/nested-attr-vector-search/2026-11-10-exps
python3 $H/experiments/plot_dsm.py --data $H/data --figures $H/figures \
  --pareto-nodeann $H/data/pareto_nodeann.jsonl \
  --pareto-acorn $H/data/pareto_acorn.jsonl
```

产出 5 张 PNG（dpi=150）：`fig_dsm_latency_vs_size.png`、`fig_move_bytes.png`、
`fig_mask_build_vs_N.png`、`fig_violation_vs_concurrency.png`、
`fig_pareto_nodeann_acorn.png`。

## 数据文件与列含义

### `results_dsm_{uniform,skewed}.csv`（各 72 行）

| 列 | 含义 |
|---|---|
| `tree` | `uniform` / `skewed` |
| `representation` | `rowpath` / `nodebitmap` / `trie` |
| `primitive` | `move` / `merge` / `split` / `delete` |
| `subtree_size` | 请求的规模标签 |
| `actual_subtree_size` | 树里实际命中的子树行数（树只有它自己的规模） |
| `rows_updated` | 被重写的向量行数 |
| `bytes_written` | 元数据层写入字节 |
| `log_entries` | 变更日志条目数 |
| `latency_p50_ms` / `latency_p99_ms` | 单次实测（`repeats=1`，因此两者相等） |
| `theoretical_rows` / `theoretical_bytes` | 模型值（§1.4 的公式） |
| `repeats` | 恒为 1 |

### `results_query.csv`

`representation, descendants, N, mask_build_ms, mask_nonzero`。`N` 为 `cache` 的行表示「MOVE 后重建被失效祖先掩码的总耗时」，此时 `mask_nonzero` 记录失效条数（= |A|）。

### `results_concurrency.csv`

`atomic, qps, queries, violations, violation_rate, window_ms`。

### `results_flat.csv`

IndexFlat（numpy 暴力）对照组：`method, queries, mask_rows, p50_ms, dists_per_q`。

### `index_invariants.json`

索引层不变量：`sha256_before/after`、`index_sha_unchanged`、`ntotal_unchanged`、`search_identical`、`index_bytes_written`、`mask_equal_before_after`、`recall_before/after`。

## NOT_MEASURED 清单

| 项 | 原因 |
|---|---|
| 非原子实现的非零违例率 | MOVE 不改变作用域成员集，「既不在旧也不在新」按定义为 0；需要换成 SPLIT / SUBTREE-DELETE 并把缓存陈旧态计入，本轮未做 |
| 多父 DAG 的 DSM 代价 | 两棵合成树均为单父；多父位图交（$B_{S_1}\wedge B_{S_2}$）只有定义没有测量 |
| 每属性一索引的更新代价（NodeANN 侧） | 需测量「改属性 → 删旧分区 + 插新分区」的索引写入量，本轮只测查询侧 |
| 均匀树 5k/50k/300k 规模 | 均匀树只存在 1k/10k/100k/1M；用最近规模替代并记录在 `actual_subtree_size` |

## 已知测量口径问题（引用数字前必读）

1. **`repeats=1`**：`latency_p50_ms` 与 `latency_p99_ms` 是同一个单次测量值，不是分布。报告中须写「单次实测」，不得写成「中位数」。
2. **`nodebitmap` 实测字节常小于理论 $|A|\cdot\lceil N/8\rceil$**：祖先链中若某节点没有子节点（例如 S 的父节点直接是叶），该节点不维护位图，重算时被跳过。引用时须同时给出实测与理论两列，并说明偏差来源。
3. **`mask_nonzero` 随 `N` 缩小**：它统计的是 `mask[:N]` 内的非零数，用于验证随 $N$ 的线性关系，不代表作用域全量大小（全量为 `N=1e6` 那一行的值）。
4. **索引不变量那一组的 recall**：必须用**作用域内**真值（脚本已改为现算 `gt_scope.ivecs`），否则会得到 0.03 这类无意义数字。
