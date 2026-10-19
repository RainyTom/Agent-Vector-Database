# DAG范围的稀疏重叠支持与块级推拉维护

> **2026-10-19 alphaXiv查重决定：停止独立立项。** 支持撤销与Counting已有直接联系，增量/重算成本选择已有前序；稀疏例外表示尚无独立方法证据。 见[逐项审计](../alphaxiv-novelty-audit.md)。下文为查重前方案记录，保留用于前序对照，不是当前实施计划；未断言整份组合已被某一论文完全实现。

状态：D1研究方案，2026-10-19。研究者综合提出，未实现、未测量；新颖性尚未充分排查。[共同评测](../directions.md)。

## 问题定义

**定义1（DAG范围）。** 给定有向无环图 $G=(V,E)$ 与本地绑定集合，定义

$$
U_v=\operatorname{Local}(v)\cup\bigcup_{w:(v,w)\in E}U_w.
$$

查询 $(q,v,k)$ 在去重后的 $U_v$ 做向量top-k。删边、删绑定、删逻辑对象不同。MOVE是一条边的重连，插边拒绝成环；首版不处理任意软链接。[symlink可能循环或悬空](https://man7.org/linux/man-pages/man7/symlink.7.html)，不能偷用DAG算法。

**定义2（直接支持数）。**

$$
c_v(o)=\mathbf1[o\in\operatorname{Local}(v)]
+\sum_{w:(v,w)\in E}\mathbf1[o\in U_w].
$$

它计直接来源，不计完整路径；DAG上逆拓扑求值满足 $o\in U_v\iff c_v(o)>0$。Bitmap并集本身不重计，困难是删除某来源后保留另一来源。

## 方案

**机制一：只存重叠例外。** 每节点存成员bitmap $U_v$，及稀疏映射

$$
X_v=\{(o,c_v(o)):c_v(o)\ge2\}.
$$

不在U则计数0；在U但不在X则1；其余从X读真实值。计数最大出度加1，不能整数溢出后饱和。接近树时，省去单支持对象的完整计数。

插入来源为0→1、1→2或r→r+1；删除反向。仅成员0↔1向父亲传播。R经A/B都到C，删除A→C后，R计数2→1而成员不变，停止继续传播。这是计数维护的应用，不单独声称新颖。

**机制二：块级push/pull。** 将U/X按固定ID块存储，比较逐ID更新计数与读取全部直接来源重算的成本：

$$
\widehat C_{\mathrm{push}}=|\Delta|c_{\mathrm{counter}},\qquad
\widehat C_{\mathrm{pull}}
=\frac{\sum_w\operatorname{bytes}(U_w\cap B_j)}{\widehat{bw}}
+c_{\mathrm{local}}.
$$

选择预计较低者；预测错误不改变语义。高重叠块可转密集整数数组，低重叠块用bitmap加例外，按实际字节阈值和滞回转换，转换前后与完整计数oracle核对。

**机制三：事务合并，原子发布。** 批次先确定最终DAG与受影响祖先区域，逆拓扑处理，使父节点只消费直接子节点的最终净变化。先删后加合并，避免临时归零产生无效上推。旧epoch保留给读者，受影响块完成后发布新epoch；首版单写者，不声称lock-free。

~~~text
ApplyBatch(old_epoch, edits):
    验证边身份、绑定及无环性
    暂存最终拓扑和本地绑定
    按子节点先于父节点的顺序处理受影响节点：
        合并各块的直接来源成员变化
        选择精确计数差分或精确重算
        只向父节点提交成员集合的净变化
    全部完成后原子发布epoch
~~~

删边扣除旧来源成员，加边加入新来源成员；若同批改子树与边，按最终来源求净变化，不能对已删除来源重复扣减。

## 正确性与成本目标

拟证明U等于直接来源并集，X精确保存计数至少2的成员；逆拓扑归纳得到可达性一致。令 $z_v=|X_v|$，额外支持空间是 $O(\sum_v z_v)$ 个键值项，加bitmap与块目录；实际按字节报告。不是按全部路径存储。

固定ANN使用精确scope；ANN漏报与scope错漏分开。更新依赖实际影响范围；大删边可真实改变大量成员，不承诺只由深度决定。高重叠时例外可接近全部成员，必须允许退化。

## 前序与候选创新

| 前序 | 已有内容与阅读范围 | 需超出的部分 |
|---|---|---|
| [Directory-Aware §III–IV](https://arxiv.org/pdf/2606.16903v1) | 树形inclusive维护，已有本地全文 | 多父撤销和同批净变化，不能简单祖先减集 |
| [Optimised Maintenance of Datalog Materialisations](https://ojs.aaai.org/index.php/AAAI/article/view/11554) | 官方摘要：Counting、DRed及混合维护 | 计数与增量维护已有；本方案仅限DAG |
| [Materialisation Maintenance Revisited](https://www.cs.ox.ac.uk/people/boris.motik/pubs/mnph19maintenance-revisited.pdf) | 摘要/相关工作线索：扩展递归维护 | 不概括成“计数不能处理递归”；具体版本算法待精读 |
| [Compact Geometric Representations of Hierarchies](https://arxiv.org/abs/2606.18520) | 摘要：可达性表示 | 可达嵌入不同于语义ANN，不据此推断性能 |

候选增量限定为：面向ANN去重scope，在同预算下联合决定重叠支持编码和块级推拉，减少共享目录删除的写放大。需补查counting sets、bitmap reference counting、增量视图和动态可达索引，不能将压缩计数直接认证为新算法。

## 最小实验与停止条件

MVP：完整计数oracle与稀疏例外→块级push/pull→相同hnswlib过滤接口。先小DAG穷举每步所有节点scope，覆盖菱形、多重共享、多处绑定、最终来源删除、同批删加和拒绝成环。

10K–50K向量，改变入度1/2/4、重叠率、绑定数、批量及读写比。基线：查询时枚举、全量重算、完整计数、固定push稀疏例外、完整方案。DRed作为待实现强相邻基线，不能用名称代替结果。

报告漏成员/多成员必须为零，更新P95、来源读取字节、成员/计数变更量、例外密度、峰值内存及ANN端到端时间。若普遍高重叠或随机写/转换抵消空间收益，撤回稀疏假设；传统维护表现相同则只保留有限适配价值。
