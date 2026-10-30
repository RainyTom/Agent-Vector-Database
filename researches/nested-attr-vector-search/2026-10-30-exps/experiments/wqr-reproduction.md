# WQR本地机制验证：真实执行记录

2026-10-30；在仓库根目录运行。不是WIKI/SIFT性能实验，也不是完整WQR线上实现。

## Python资格与动态目录

```sh
python3 researches/nested-attr-vector-search/2026-10-30-exps/experiments/wqr_semantics.py
```

已执行：120960个block mask与独立scalar判定比较，0 mismatch；initial/V/T/M/B/R/D/I-restore/I-reserve；135次额外合法Euler cut/link，每次核49对祖先关系。实现使用真实implicit treap split/merge/rank，不是MOVE后重算全树DFS再冒充O(log D)。有限检查不证明任意序列、并发或最坏复杂度。

输出：[wqr-semantics.json](../data/wqr-semantics.json)。

## C++真实HNSW路由

当前目标`codes/directory-aware-vector-attr-search/deps/hnswlib`目录为空。使用已有依赖副本并先核：

```sh
git -C codes/vector-join-reproduction/build/_deps/hnswlib-src rev-parse HEAD
git -C codes/vector-join-reproduction/build/_deps/hnswlib-src diff --name-only HEAD -- hnswlib
```

实际HEAD为`ca426729609b3221563047ceb269cc1213e0d376`；headers diff为空。没有将其他版本冒充指定revision，也未复制/修改第三方代码。

```sh
g++ -O2 -std=c++17 -Wall -Wextra \
  -Icodes/directory-aware-vector-attr-search/src \
  -Icodes/vector-join-reproduction/build/_deps/hnswlib-src \
  researches/nested-attr-vector-search/2026-10-30-exps/experiments/wqr_hnsw_probe.cpp \
  -o /tmp/wqr-hnsw-probe -lpthread
/tmp/wqr-hnsw-probe
```

编译/运行成功。第三方header有unused parameter/function和unsigned<0警告，保留于[wqr-hnsw-build.log](wqr-hnsw-build.log)，未压制、未据此修改依赖。未作时延测量。

输出：[wqr-hnsw-probe.json](../data/wqr-hnsw-probe.json)。正例BlockGate/WQR squared-L2分别900/1；反例WQR在L=1时1而GT=0，L=2恢复；源对象删除后proxy仍能服务；2 portals只给1块而8 portals给2块；空query仍做8次proxy距离并准备2块mask。

C++探针的资格为fixture布尔值，用于隔离真实HNSW结果准入与zone差异；完整类别/数值/tag/目录组合由Python有限语义程序验证。两者不能被描述成完整C++动态WQR已实现。

## 实施边界

正式WIKI/SIFT数组不在本机，本轮未运行端到端性能。服务器按[wqr-server-prompt](../wqr-server-prompt.md)实施全部正式路径。结果损失不因本地有限正例而被排除；代表混叠反例本身是本轮交付的一部分。
