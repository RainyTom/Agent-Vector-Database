# 本地资格感知执行复现

工作目录：仓库根。日期2026-10-31。合成输入只验证执行，不用于真实数据性能主张。

## 构建与最终回归

```bash
bash codes/directory-aware-vector-attr-search/build_closure.sh
bash codes/directory-aware-vector-attr-search/build_dynamic_triad.sh
python -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p test_eligibility.py
python -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p test_dynamic_triad.py
python -m unittest discover -s codes/directory-aware-vector-attr-search/tests -p test_closure.py
```

最终分别2/3/3项通过；完整状态与首次失败见上级报告。实际编译使用-O3 -march=native -std=c++17，固定hnswlib ca426729609b3221563047ceb269cc1213e0d376。

## 合成输入与原协议manifest

本地使用现有 `tests/test_closure.py::fixture` 创建80对象/4维数据与180Q mixed trace。等价复现：

```python
import sys
from pathlib import Path
root = Path.cwd()
code = root / 'codes/directory-aware-vector-attr-search'
sys.path.insert(0, str(code / 'tests'))
from test_closure import fixture
work = root / 'work/eligibility-execution-local'
work.mkdir(exist_ok=False)
data, trace = fixture(work)
```

```bash
python codes/directory-aware-vector-attr-search/scripts/run_closure.py \
  --dataset toy=$PWD/work/eligibility-execution-local/data \
  --trace toy=$PWD/work/eligibility-execution-local/mixed.trace \
  --out work/eligibility-execution-local/original-static \
  --max-queries 180 --efs 128 --repeats 1 --smoke

python codes/directory-aware-vector-attr-search/scripts/run_eligibility.py \
  --previous-raw work/eligibility-execution-local/original-static \
  --out work/eligibility-execution-local/static-final --smoke

python codes/directory-aware-vector-attr-search/scripts/run_eligibility.py \
  --previous-raw work/eligibility-execution-local/original-static \
  --out work/eligibility-execution-local/maintenance-final --stage maintenance --smoke
```

分别57/24次计时，完整manifest/source/binary/input SHA与原始CSV在上述目录。若目录已存在，换新目录，不能覆写。生产服务器使用真实原manifest且禁止--smoke；见服务器提示词。

## 冻结off行为对照

实际提取历史源，不读取/改动历史真实数据raw：

```bash
git archive --format=tar --output=work/eligibility-execution-local/frozen-7654f1e.tar \
  7654f1e codes/directory-aware-vector-attr-search/src
tar -xf work/eligibility-execution-local/frozen-7654f1e.tar -C work/eligibility-execution-local

g++ -O3 -march=native -std=c++17 \
  -o work/eligibility-execution-local/frozen-nodeann \
  work/eligibility-execution-local/codes/directory-aware-vector-attr-search/src/dynamic_triad.cpp \
  -Icodes/directory-aware-vector-attr-search/deps/hnswlib -lpthread
g++ -O3 -march=native -std=c++17 \
  -o work/eligibility-execution-local/frozen-closure \
  work/eligibility-execution-local/codes/directory-aware-vector-attr-search/src/closure_research.cpp \
  -Icodes/directory-aware-vector-attr-search/deps/hnswlib -lpthread
```

分别对两套二进制执行A scan512、A scan0、U、H × ef15/20，data为上面的data，trace为mixed.trace，默认off。独立输出到 `off-{frozen,current}-{arm}-{scan}-{ef}`。逐query比较result_ids/gt_ids/distance_calls/ann_hops/scope_card/eligible_card/fanout/scan_nodes；U/H另比expanded/pool_candidates/retained/pruned。8组、1440条配对记录零差异。不是计时一致性测试，也不是所有数据集等价性的证明。

## 小汇总

读取最终runs.json及其prefix指向的query/update CSV，合计错误字段、执行路径、图/扫描距离分账与实际compaction。最终81次计时、14028条query记录，错误合计0、距离分账错误0、维护compaction记录6（含重复）。数据与比较字段见 `../data/eligibility-local-check.json`。原始work、依赖、向量与二进制不提交。
