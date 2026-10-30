# 本轮知识探针复现

从仓库根目录运行。只有标准库数学例子与四对象hnswlib API执行，不进行速度比较，不下载真实向量，不启动数据库。

## 数学例子

```bash
python researches/vector-database-knowledge/2026-10-30-exps/experiments/semantic_probes.py
```

输出JSON；metric、projection、filtering、joint_predicate、recall、rerank、dag均由计算产生，并通过显式异常校验。不是ANN实现，不用Python运行时间与C++比性能。

## 固定hnswlib API

首次需要仓库已有submodule：

```bash
git submodule update --init codes/directory-aware-vector-attr-search/deps/hnswlib
git -C codes/directory-aware-vector-attr-search/deps/hnswlib rev-parse HEAD
```

本轮版本必须为 `ca426729609b3221563047ceb269cc1213e0d376`。

```bash
mkdir -p work/vector-database-knowledge
g++ -O2 -std=c++17 -Wall -Wextra \
  -Icodes/directory-aware-vector-attr-search/src \
  -Icodes/directory-aware-vector-attr-search/deps/hnswlib \
  researches/vector-database-knowledge/2026-10-30-exps/experiments/hnsw_lifecycle_probe.cpp \
  -lpthread -o work/vector-database-knowledge/hnsw_lifecycle_probe
work/vector-database-knowledge/hnsw_lifecycle_probe
```

实际调用HNSW建图、非连续外部label过滤、`markDelete`、不足k返回、`unmarkDelete`及payload读取；距离用本项目`RawL2Space`。query四维、四对象、ef16/M4/efConstruction20/seed100。对该例结果与平方距离做显式校验，不依赖可能被NDEBUG移除的assert。

编译成功时出现固定第三方头文件的unused/type-limits警告；保留原始stderr，未通过抑制警告改库。未证明物理擦除、持久化恢复、并发事务或一般有限ef召回。

## 已保存产物

- [数学结果](../data/semantic-probes.json)
- [真实HNSW API结果](../data/hnsw-lifecycle-probe.json)
- [命令和stdout/stderr](../data/run-log.json)
- [源码SHA与固定版本](../data/probe-manifest.json)

二进制留`work/`不提交；两个源文件是可重用的教学探针，不是在线产品或新算法测试集。后续需要修改时新增日期化证据，不覆盖本轮输出。
