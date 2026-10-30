# 8649e55不可变源码故障复现

这是实现审计，不是WQR修复或性能benchmark。没有重跑用户报告的8对象原fixture。

## 源码取得与完整性

读取GitHub commit API `https://api.github.com/repos/RainyTom/Agent-Vector-Database/commits/8649e55`。本会话完整reader输出为artifact://168。Eval解析JSON，对两个status=added文件，删除patch首行hunk header、去除各行开头的单个`+`并补末尾换行；对`b'blob '+ASCII字节数+b'\0'+源字节`计算SHA1，与API的Git blob SHA逐项一致。用write工具保存至work/wqr-review-8649e55/，没有修改源文件。该目录不提交；跨会话按固定commit重新取得原始文件，不能依赖artifact永久存在。

两个源文件SHA见[`../data/wqr-8649e55-review.json`](../data/wqr-8649e55-review.json)。公共include为本地codes/directory-aware-vector-attr-search/src/raw_l2_space.h；hnswlib使用已有codes/vector-join-reproduction/build/_deps/hnswlib-src副本（此前已核固定revision）。本轮没有重新安装依赖或修改基础图源码。

## Sanitizer启动限制

先以`-fsanitize=address,undefined`构建诊断，随后以相同选项增加`-no-pie`构建尝试；两次都在进入main前提示：

```text
Shadow memory range interleaves with an existing memory mapping.
ASan cannot proceed correctly. ABORTING.
```

这些不是WQR逻辑崩溃，也不能当作ASan已完成检查。下面实际行为证据改用UBSan及libstdc++边界断言取得。

## 实际成功构建与safe模式运行

在仓库根目录执行：

```sh
g++ -O1 -g -fsanitize=undefined -fno-omit-frame-pointer -D_GLIBCXX_ASSERTIONS -std=c++17 -Iwork/wqr-review-8649e55 -Icodes/directory-aware-vector-attr-search/src -Icodes/vector-join-reproduction/build/_deps/hnswlib-src researches/nested-attr-vector-search/2026-10-30-exps/experiments/wqr_8649e55_review.cpp -o /tmp/wqr-8649e55-review-ubsan -lpthread
/tmp/wqr-8649e55-review-ubsan safe
```

编译警告保留：wqr_index.h:318，SIZE_MAX转uint32时从18446744073709551615截断为4294967295。未加-w压制。

实际stdout：

```text
sibling_containment actual=1 expected=0
category_reject actual=1 expected=0
empty_overlap_reject actual=1 expected=0
numeric_five_in_zero_ten actual=0 expected=1
literal_minus_one_bound actual=1 expected=0
external_id_result_count actual=0 expected=2 selected_blocks=1
portal_counter first_report=4 first_actual_delta=3 second_report=7 second_actual_delta=3
local_counter first_report=0 returned_objects=2
delete_live_count actual=2 expected=1
restore_zone actual=0 expected=1
```

程序只打印反例actual/expected，不用“进程exit0”冒充测试通过。safe模式本身完成；这些不一致正是审计结果。

## reserve首次插入实际运行

```sh
/tmp/wqr-8649e55-review-ubsan reserve
```

已有canonical容量3、初始只索引ID0/1，再I ID2。退出134：

```text
std::vector<wqr::Block>::operator[]: Assertion '__n < this->size()' failed.
```

这是被审源码的越界访问，被checked vector拦截；不是前述ASan启动问题。没有把后续空槽扫描或split作为已运行路径。

## 原runner混合事件实际运行

原runner编译：

```sh
g++ -O1 -g -fsanitize=undefined -fno-omit-frame-pointer -D_GLIBCXX_ASSERTIONS -std=c++17 -Icodes/directory-aware-vector-attr-search/src -Icodes/vector-join-reproduction/build/_deps/hnswlib-src work/wqr-review-8649e55/wqr_research.cpp -o /tmp/wqr-8649e55-runner-ubsan -lpthread
```

在Eval用struct.pack构造当前错误runner可接受的私有二进制格式，**不是正式研究数据**：

```python
import struct
from pathlib import Path
p=Path('work/wqr-review-8649e55/runner-fixture')
p.mkdir(parents=True,exist_ok=True)
(p/'out').mkdir(exist_ok=True)
for filename, fmt, vals in [
 ('base.f32','<4f',[1,0,2,0]),('test.f32','<4f',[1,0,2,0]),
 ('bindings.txt','<2q',[0,0]),('category.f64','<2q',[0,0]),
 ('numeric.f64','<2q',[0,0]),('tags.u64','<2Q',[0,0])]:
    (p/filename).write_bytes(struct.pack(fmt,*vals))
```

用write工具建立tape.txt：

```text
3
Q 0 0 0 0 0 -1 -1 -1 0
V 0 9 9
Q 1 0 0 0 0 -1 -1 -1 0
```

Q在这里特意使用被审runner字段顺序，以隔离事件丢失缺陷；不可用于宣称正式Q格式兼容。

实际执行：

```sh
/tmp/wqr-8649e55-runner-ubsan work/wqr-review-8649e55/runner-fixture work/wqr-review-8649e55/runner-fixture/tape.txt work/wqr-review-8649e55/runner-fixture/out
```

read工具读取原输出，只有：

```text
build_us 3581.42
Q 1 84.102 4 0 1 1 1 0 1:0 0:1
```

预期两条Q、一条V，实际一条Q、零条V。时间只记录原输出，不作为查询或构建效率结论。没有运行正式WIKI/SIFT、NodeANN对照或修复后验证。临时二进制在审计结束后删除；可重建的源码快照和fixture保留在ignored work/，不提交原始数据。
