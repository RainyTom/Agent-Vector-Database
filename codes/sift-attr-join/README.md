# sift-attr-join

层级品类 × facet 约束下的向量连接实验代码。问题定义见
`researches/nested-attr-vector-search/01-directory-aware-acceleration/final/problem-formulation.md`。

数据集由 `scripts/build_dataset.py` 生成到 `datasets/sift-attr/`（该目录被 `.gitignore` 排除）。

```bash
python3 codes/sift-attr-join/scripts/build_dataset.py --n 20000 --seed 20261105
```

当前只有数据生成；基线（B0 oracle / B2 posting 差集）与评测尚未实现。
