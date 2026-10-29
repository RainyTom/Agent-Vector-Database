# 2026-10-30 条件证书离线诊断复现

从仓库根目录执行。此记录对应已完成run1/run2；数据/大矩阵/二进制在忽略的work，不随Git上传。代码、配置、种子、输入哈希、全部小结果和失败说明上传。**本实验没有构建新ANN索引，也不是Python与C++速度比较。**

## 输入与约束

- WIKI `2026-10-29-exps/work/dynamic_triad/wiki-topology/wiki-topology-s20261029-q456-f7046c642b34/root-3626/native`，11869个初始对象，1024维。
- SIFT `2026-10-29-exps/work/dynamic_triad/crossover/datasets/sift-n12000-r1000-q64-d3-s20261029`，只用前12000个初始活对象、前8个query；不把1000 reserve当活对象。
- 脚本从既有native FP32向量、query、目录和owner文件读入；具体文件SHA见各run的provenance.json。上游数据不在Git，需要先按历史数据生成记录准备上述输入，不能用随机向量替换而仍称复现。
- K32/96、niter10、seed20261030；k=10；两basis、三scope、四predicate、两attribute control。FAISS只做k-means，ANN/原始距离不用FAISS。

## 环境及构建：实际执行

现有venv已含NumPy/Matplotlib，但初次运行缺FAISS binding。安装固定版本，没有升级NumPy：

```sh
uv pip install --python production/nested-attr-vector-search/work/venv/bin/python faiss-cpu==1.12.0
```

辅助程序为共享hnswlib distance封装的离线矩阵生成器，不是图构建/搜索原型：

```sh
EXP=researches/nested-attr-vector-search/08-dynamic-hybrid-vector-search/2026-10-30-exps
CODE=codes/directory-aware-vector-attr-search
mkdir -p "$EXP/work/conditional-geometry"
g++ -O3 -march=native -std=c++17 -Wall -Wextra \
  -I"$CODE/src" -I"$CODE/deps/hnswlib" \
  "$EXP/experiments/raw_l2_matrix.cpp" \
  -o "$EXP/work/conditional-geometry/raw_l2_matrix" -lpthread
```

构建成功，仅有hnswlib上游unused告警；本轮没有改动该依赖。probe启动时实际执行2×2已知距离矩阵smoke，期望[[0,25],[25,0]]，并检查同对象AND、单维见证松弛反例、indexed极值/gap等。

## 完整实跑

```sh
PY=production/nested-attr-vector-search/work/venv/bin/python
$PY "$EXP/experiments/conditional_geometry_probe.py" \
  --binary "$EXP/work/conditional-geometry/raw_l2_matrix" \
  --out "$EXP/data/conditional-geometry-run1" \
  --work "$EXP/work/conditional-geometry/run1"
```

run1的几何/工作计数有效，但predicate_python_s只计scope mask AND，普通比较在计时外。该错误未删除，见run1/measurement-note.json。修复为计入原普通属性比较后，以相同数据/种子/配置完整执行：

```sh
$PY "$EXP/experiments/conditional_geometry_probe.py" \
  --binary "$EXP/work/conditional-geometry/raw_l2_matrix" \
  --out "$EXP/data/conditional-geometry-run2" \
  --work "$EXP/work/conditional-geometry/run2"
```

run2完整结束，provenance记wall约21.7s；这是离线脚本整体时间，不是在线ANN时延，也不参与方案加速结论。每run包含9216 query条件记录（只有16个源query向量）、192 cold证书记录、384采样gap记录，FP32原L2190952次加4次smoke调用。脚本自行拒绝覆盖已有out/work。

当前源码是修正后的run2版本；run1脚本SHA保留在其provenance，旧计时行为和修正范围在measurement-note说明。今后重跑应更换新run名；不要删除旧目录，也不要将修正后的计时伪称复现了run1旧计时。

## 分析与图：实际执行

```sh
$PY "$EXP/experiments/analyze_conditional_geometry.py" \
  "$EXP/data/conditional-geometry-run2" \
  --reference "$EXP/data/conditional-geometry-run1"
$PY "$EXP/experiments/plot_conditional_geometry.py" \
  "$EXP/data/conditional-geometry-run2/analysis/pooled-information.csv" \
  "$EXP/figures/conditional-information-r64.png"
```

分析实际核对两run全部9216 query结果与384条非时间indexed工作记录一致，未把重复当统计样本。图已打开检查。grid保留所有K/basis/r/predicate组合；scope-only不混入主ordinary-predicate新增信息合计；空块与几何排除分开。

## 成本口径与复现边界

- FP32 squared-L2 ground truth由同一个C++内核生成；Python的FP64证书有保守guard，逐块核验该输入观测正确性，不声称通用浮点证明。
- sorted storage为64N×(8B投影值+4B ID)，不含F索引、PCA矩阵、block map、临时缓冲。逻辑probe字节不是DRAM bytes；所有Python时延不能与C++ANN时延作速度对比。
- indexed端点只实际查一次每F，并将其信息用于多个q作诊断；cold成本独立保存，不据此宣称跨query缓存收益。gap成本只测前2个q；不能把它说成8条query均完成成本计时。
- threshold来自filtered GT只用于信息上限；scope_radius来自无普通过滤的scope GT，只用于固定半径控制，不是在线filtered top-k合法tau。
- 属性更新、插入、删除、读写并发和在线NodeANN/hnswlib性能未运行；结论不涉及它们。大scope精确矩阵仅为GT/oracle，不是提出的查询算法。
- 文献JSONL用逐页读取/主键/关系端点/新原文SHA审计，结果在 `../data/literature-audit.json`；仓库引用的 `tools/research_index.py` 实际不存在，未伪称运行该工具。
