# 多代表路由直接前序

Unleashing Graph Partitioning for Large-Scale Nearest Neighbor Search，PVLDB18(6):1649–1662，2025。

§3–3.2、§4.3已给每shard多代表、代表全集HNSW、映射为shard probe order、shard内HNSW；它是WQR几何骨架的直接前序。kRt使用层级k-means中心，hRt另用LSH，后者的理论保证不能转给前者。

§4.4显示所测kRt路由更强；§4.5为单机顺序模拟分布式hosts，不当作本项目性能。本文相关章节未给目录/多属性完整资格到proxy支持域的准入接口，但不据此宣称领域首次。

本轮仅核§1–3.2、§4.3–4.5，未运行作者实现，完整hRt证明与全部实验待核。迁移设计见`researches/nested-attr-vector-search/2026-10-30-exps/wqr-index-spec.md`。
