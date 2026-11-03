#!/usr/bin/env python3
"""v4 deck: neutral academic wording, figure plates from image generation,
pseudocode without rationale blocks.
"""
import argparse
from pathlib import Path

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.util import Emu, Inches, Pt

BLUE = RGBColor(0x1F, 0x3F, 0x86)
MIST = RGBColor(0xF2, 0xF4, 0xF8)
ORANGE = RGBColor(0xB5, 0x5A, 0x1E)
GRAY = RGBColor(0x4C, 0x51, 0x5C)
LIGHTGRAY = RGBColor(0x77, 0x7C, 0x86)
DARK = RGBColor(0x1A, 0x1D, 0x24)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)
RULE = RGBColor(0xB8, 0xC2, 0xD4)
CODEBG = RGBColor(0xF6, 0xF7, 0xF9)
FONT = 'Microsoft YaHei'
MONO = 'Consolas'
W, H = Inches(13.333), Inches(7.5)


def _paint(run, size, bold, color, latin, italic=False):
    f = run.font
    f.size = Pt(size)
    f.bold = bold
    f.italic = italic
    f.color.rgb = color
    f.name = latin
    rPr = run._r.get_or_add_rPr()
    ns = '{http://schemas.openxmlformats.org/drawingml/2006/main}'
    for tag in ('ea', 'cs'):
        el = rPr.find(ns + tag)
        if el is None:
            el = rPr.makeelement(ns + tag, {})
            rPr.append(el)
        el.set('typeface', FONT)


def text(slide, x, y, w, h, lines, size=12, bold=False, color=DARK, align=PP_ALIGN.LEFT,
         anchor=MSO_ANCHOR.TOP, space=4, latin=FONT, line=None):
    tb = slide.shapes.add_textbox(x, y, w, h)
    tf = tb.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = anchor
    tf.margin_left = tf.margin_right = Emu(0)
    tf.margin_top = tf.margin_bottom = Emu(0)
    for i, item in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        p.space_after = Pt(space)
        if line:
            p.line_spacing = line
        content, opts = item if isinstance(item, tuple) else (item, {})
        run = p.add_run()
        run.text = content
        _paint(run, opts.get('size', size), opts.get('bold', bold), opts.get('color', color),
               opts.get('latin', latin), opts.get('italic', False))
    return tb


def rule(slide, x, y, w, color=RULE, weight=0.75):
    ln = slide.shapes.add_connector(1, x, y, x + w, y)
    ln.line.color.rgb = color
    ln.line.width = Pt(weight)


def panel(slide, x, y, w, h, fill=MIST, border=RULE):
    s = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, x, y, w, h)
    s.fill.solid()
    s.fill.fore_color.rgb = fill
    s.line.color.rgb = border
    s.line.width = Pt(0.75)
    s.shadow.inherit = False
    return s


def arrow(slide, x1, y1, x2, y2, color=GRAY, width=1.0):
    ln = slide.shapes.add_connector(1, x1, y1, x2, y2)
    ln.line.color.rgb = color
    ln.line.width = Pt(width)
    body = ln.line._get_or_add_ln()
    head = body.makeelement('{http://schemas.openxmlformats.org/drawingml/2006/main}tailEnd', {})
    head.set('type', 'triangle')
    body.append(head)
    return ln


def pseudocode(slide, x, y, w, h, lines, title, size=9.5):
    panel(slide, x, y, w, h, fill=CODEBG, border=RULE)
    pad = Inches(0.14)
    text(slide, x + pad, y + Inches(0.09), w - 2*pad, Inches(0.24),
         [(title, {'size': 9.5, 'bold': True, 'color': BLUE})], size=9.5)
    body = [(f'{i:>2}  {ln}', {'size': size, 'latin': MONO}) for i, ln in enumerate(lines, 1)]
    text(slide, x + pad, y + Inches(0.37), w - 2*pad, h - Inches(0.45), body,
         size=size, space=1.5, latin=MONO)


def header(slide, title, logo, number, subtitle=None):
    text(slide, Inches(0.62), Inches(0.42), Inches(10.4), Inches(0.5), [title], size=23, bold=True, color=BLUE)
    rule(slide, Inches(0.62), Inches(1.0), Inches(12.1), color=RULE, weight=1.0)
    if subtitle:
        text(slide, Inches(0.62), Inches(1.08), Inches(11.6), Inches(0.3), [subtitle],
             size=10.5, color=LIGHTGRAY)
    if logo:
        slide.shapes.add_picture(str(logo), Inches(11.85), Inches(0.36), height=Inches(0.56))
    text(slide, Inches(0.62), Inches(7.08), Inches(1.2), Inches(0.26), [str(number)],
         size=9, color=LIGHTGRAY)


def blank(prs):
    return prs.slides.add_slide(prs.slide_layouts[6])


def caption(slide, x, y, w, label, note=''):
    text(slide, x, y, w, Inches(0.28),
         [(label + ('   ' + note if note else ''), {'size': 9, 'color': LIGHTGRAY, 'italic': False})], size=9)


def build(logo, out, figdir):
    prs = Presentation()
    prs.slide_width, prs.slide_height = W, H
    fig = lambda name: figdir / name

    # ---------------------------------------------------------------- 1
    s = blank(prs)
    s.shapes.add_picture(str(logo), Inches(0.9), Inches(0.75), height=Inches(1.15))
    text(s, Inches(2.45), Inches(0.95), Inches(8.0), Inches(0.35),
         ['南京邮电大学  计算机学院'], size=12, color=GRAY)
    rule(s, Inches(0.9), Inches(2.35), Inches(11.5), color=BLUE, weight=1.5)
    text(s, Inches(0.9), Inches(2.65), Inches(11.5), Inches(0.8),
         ['层级属性约束下的向量混合搜索'], size=32, bold=True, color=BLUE)
    text(s, Inches(0.9), Inches(3.45), Inches(11.5), Inches(0.4),
         ['索引形态、合并规则与两类查询语义'], size=15, color=GRAY)
    text(s, Inches(0.9), Inches(4.35), Inches(11.5), Inches(1.4),
         [('研究方案汇报', {'size': 11.5, 'color': GRAY}),
          ('数据：SIFT-128d，1,929 个属性节点、2,040 条属性边、168,937 个向量', {'size': 11.5}),
          ('索引：属性内 HNSW，沿属性 DAG 合并为单张 item 图；查询：单队列，资格判定先于距离计算', {'size': 11.5})],
         size=11.5, space=5)
    text(s, Inches(0.9), Inches(6.72), Inches(11.5), Inches(0.3),
         [('2026 年 11 月', {})], size=10, color=LIGHTGRAY)

    # ---------------------------------------------------------------- 2
    s = blank(prs)
    header(s, '应用场景：ACM CCS 概念体系上的检索', logo, 2,
           '论文按 CCS 路径标注，检索限定在某个概念及其后代之内')
    rows = [('Information systems', 0, '4,440 / 31,810', False),
            ('Information retrieval', 1, '3,722 / 10,881', False),
            ('Document representation', 2, '1,579 / 1,647', False),
            ('Retrieval models and ranking', 2, '1,373 / 1,373', False),
            ('Search engine architectures and scalability', 2, '0 / 522', True)]
    geom = []
    for i, (name, depth, cnt, hot) in enumerate(rows):
        x = Inches(0.62 + depth * 0.42)
        y = Inches(1.48 + i * 0.72)
        w = Inches(5.0 - depth * 0.42)
        panel(s, x, y, w, Inches(0.56), fill=WHITE, border=ORANGE if hot else RULE)
        text(s, x + Inches(0.1), y + Inches(0.05), w - Inches(0.2), Inches(0.22),
             [(name, {'size': 8.5, 'bold': True, 'color': ORANGE if hot else BLUE})], size=8.5)
        text(s, x + Inches(0.1), y + Inches(0.29), w - Inches(0.2), Inches(0.2),
             [(cnt, {'size': 8, 'color': GRAY, 'latin': MONO})], size=8)
        geom.append((x, y, w))
    def elbow(parent_i, child_is, color_map=None):
        px, py, pw = geom[parent_i]
        bar_x = Emu(int(px + Inches(0.18)))
        top = Emu(int(py + Inches(0.56)))
        bottoms = [Emu(int(geom[j][1] + Inches(0.28))) for j in child_is]
        ln = s.shapes.add_connector(1, bar_x, top, bar_x, max(bottoms))
        ln.line.color.rgb = GRAY
        ln.line.width = Pt(0.75)
        for j, by in zip(child_is, bottoms):
            cx = geom[j][0]
            stub = s.shapes.add_connector(1, bar_x, by, Emu(int(cx)), by)
            hot = color_map and color_map(j)
            stub.line.color.rgb = ORANGE if hot else GRAY
            stub.line.width = Pt(0.75)

    elbow(0, [1])
    elbow(1, [2, 3, 4], color_map=lambda j: rows[j][3])
    caption(s, Inches(0.62), Inches(5.12), Inches(6.0), '图 1', 'CCS 分支；数值为直属篇数 / 含后代篇数')
    text(s, Inches(0.62), Inches(5.42), Inches(6.15), Inches(1.5),
         [('标注数据为 SciHTC：186,160 篇论文各带一条 CCS 路径，路径覆盖 816 个概念，其中 26 个概念有两个父节点'
           '（如 Virtual reality 同属 Graphics systems and interfaces 与 Interaction paradigms）。', {'size': 9.5}),
          ('内部概念可以没有直属论文：Search engine architectures and scalability 直属 0 篇、含后代 522 篇。',
           {'size': 9.5, 'color': ORANGE})], size=9.5, space=5)
    text(s, Inches(7.05), Inches(1.42), Inches(5.65), Inches(0.26), ['两种检索语义'], size=11, bold=True, color=BLUE)
    text(s, Inches(7.05), Inches(1.74), Inches(5.65), Inches(1.75),
         [('非递归（direct）', {'size': 10, 'bold': True}),
          ('S_dir(a) = { x | owner(x) = a }', {'size': 10, 'latin': MONO}),
          ('Information retrieval 取到 3,722 篇', {'size': 9, 'color': GRAY}),
          ('递归（recursive）', {'size': 10, 'bold': True}),
          ('S_rec(a) = { x | owner(x) ∈ Desc*(a) }', {'size': 10, 'latin': MONO}),
          ('同一概念取到 10,881 篇', {'size': 9, 'color': GRAY})], size=10, space=5)
    panel(s, Inches(7.05), Inches(3.62), Inches(5.65), Inches(1.55))
    text(s, Inches(7.22), Inches(3.74), Inches(5.35), Inches(1.35),
         [('查询形式', {'size': 10, 'bold': True, 'color': BLUE}),
          ('q = (v, A, σ, k)', {'size': 10, 'latin': MONO}),
          ('v 为查询向量，A ⊆ C 为概念集合（多概念取并），σ ∈ {direct, recursive}；', {'size': 9.5}),
          ('返回 S_σ(A) 中距 v 最近的 k 个向量。', {'size': 9.5})], size=9.5, space=5)
    text(s, Inches(7.05), Inches(5.35), Inches(5.65), Inches(1.2),
         [('索引工作负载另按完整 CCS 树构造：1,929 个概念、2,040 条边、103 个多父概念；'
           '168,937 篇论文按单一直属概念绑定并与随机向量配对。', {'size': 9, 'color': LIGHTGRAY})], size=9)

    # ---------------------------------------------------------------- 3
    s = blank(prs)
    header(s, '两种语义的规模差异', logo, 3, '同一概念下，递归范围通常为直属范围的 2–7 倍')
    cols = [Inches(3.55), Inches(2.0), Inches(2.2), Inches(1.6)]
    heads = ['概念', '直属篇数', '含后代篇数', '倍数']
    x = Inches(0.62)
    for w, hd in zip(cols, heads):
        text(s, x, Inches(1.4), w - Inches(0.1), Inches(0.26), [hd], size=10, bold=True, color=BLUE)
        x += w
    rule(s, Inches(0.62), Inches(1.7), Inches(10.2), color=BLUE)
    rows = [('Information systems', '4,440', '31,810', '7.2x'),
            ('Information retrieval', '3,722', '10,881', '2.9x'),
            ('Machine learning', '1,605', '3,193', '2.0x'),
            ('Computer vision', '435', '3,209', '7.4x'),
            ('Search engine architectures and scalability', '0', '522', '-')]
    for r, row in enumerate(rows):
        y = Inches(1.78 + r * 0.46)
        rule(s, Inches(0.62), y, Inches(10.2))
        x = Inches(0.62)
        for c, cell in enumerate(row):
            hot = (c == 1 and cell == '0') or (c == 3 and cell == '-')
            text(s, x, y + Inches(0.06), cols[c] - Inches(0.12), Inches(0.3), [cell], size=9.5,
                 color=ORANGE if hot else DARK)
            x += cols[c]
    text(s, Inches(11.0), Inches(1.4), Inches(1.7), Inches(0.26), ['说明'], size=10, bold=True, color=BLUE)
    text(s, Inches(11.0), Inches(1.72), Inches(1.7), Inches(2.6),
         [('倍数 = 含后代 ÷ 直属', {'size': 8.5, 'color': GRAY}),
          ('直属为 0 时无法定义，记“-”', {'size': 8.5, 'color': GRAY}),
          ('来源：SciHTC 标注路径', {'size': 8.5, 'color': GRAY})], size=8.5, space=8)
    rule(s, Inches(0.62), Inches(4.85), Inches(12.1))
    text(s, Inches(0.62), Inches(5.0), Inches(12.1), Inches(1.8),
         [('三点观察', {'size': 10.5, 'bold': True, 'color': BLUE}),
          ('1. 两种语义的结果集不同，不能用一方替代另一方：递归包含后代概念下的论文，非递归不包含。', {'size': 10}),
          ('2. 越靠近上层倍数越大；内部概念可能没有直属论文，非递归检索返回空集。', {'size': 10}),
          ('3. 概念体系存在多父：26 个概念有两条父链，后代集合互相重叠，不能按树处理。', {'size': 10})],
         size=10, space=5)

    # ---------------------------------------------------------------- 4
    s = blank(prs)
    header(s, '研究动机', logo, 4, '概念范围被摊平写入索引后，概念变更的代价沿子树传播')
    s.shapes.add_picture(str(fig('motivation.png')), Inches(7.55), Inches(1.45), width=Inches(5.15))
    caption(s, Inches(7.55), Inches(4.62), Inches(5.15), '图 2', '层级结构摊平为扁平谓词的示意')
    text(s, Inches(0.62), Inches(1.5), Inches(6.6), Inches(2.6),
         [('现有工作把概念范围转换为扁平谓词：查询期展开 ID 列表，写入期展开祖先位图，或编码为区间与标签集合。', {}),
          ('扁平表示写入索引后，一次概念变更需重写子树内全部条目。以本工作负载为例，整库祖先位图共 880,649 条；'
           '一次子树 MOVE 需重写的中位数 576 条，p90 为 7,511 条，根子树 156,370 条，占全库 17.8%。', {}),
          ('重命名同样触发重写：路径键包含名称。图侧另有代价：边摘要需 patch 或重建。', {})],
         size=11, space=9, line=1.25)
    panel(s, Inches(0.62), Inches(4.05), Inches(6.6), Inches(1.05))
    text(s, Inches(0.82), Inches(4.16), Inches(6.2), Inches(0.86),
         [('本方案的取舍', {'size': 11, 'bold': True, 'color': BLUE}),
          ('概念集合不写入索引。属性在查询时编译为 allowed 掩码；图只表示向量邻接。', {'size': 10.5})], size=10.5, space=5)
    rule(s, Inches(0.62), Inches(5.4), Inches(12.1))
    text(s, Inches(0.62), Inches(5.55), Inches(12.1), Inches(0.6),
         [('由此确定两个设计问题：单张图能否同时服务两种检索语义；合并规则能否在不重建索引的前提下保持可用。',
           {'size': 11})], size=11)

    # ---------------------------------------------------------------- 5
    s = blank(prs)
    header(s, '现有工作（仅列已发表）', logo, 5)
    cols = [Inches(2.85), Inches(2.3), Inches(3.6), Inches(3.25)]
    heads = ['工作', '发表', '机制', '与本问题的关系']
    x = Inches(0.62)
    for w, hd in zip(cols, heads):
        text(s, x, Inches(1.35), w - Inches(0.1), Inches(0.26), [hd], size=10, bold=True, color=BLUE)
        x += w
    rule(s, Inches(0.62), Inches(1.64), Inches(12.0), color=BLUE)
    rows = [('ACORN', 'SIGMOD 2024', '谓词子图遍历，加密 HNSW 邻域', '谓词无关；层级需自行编码'),
            ('Filtered-DiskANN', 'WWW 2023', '过滤感知构图；按标签建图后拼接', '单标签或析取；不处理层级闭包'),
            ('iRangeGraph', 'SIGMOD 2025', '分段树上的范围专用图', '数值区间；非多父概念闭包'),
            ('UNG', 'SIGMOD 2025', '按标签集分组，依包含 DAG 加跨组边', '最接近；未讨论属性更新'),
            ('Curator', 'SIGMOD 2026', '共享聚类树上的分组索引', '基于划分；不共享图邻接'),
            ('TFANNS', 'ICDE 2025', '标签图与共享度数预算', '标签合取；块内共享，非概念 DAG'),
            ('动态范围检索', 'PVLDB 2025', '支持数值属性更新的范围索引', '数值区间；未读正文，仅记录存在')]
    for r, row in enumerate(rows):
        y = Inches(1.72 + r * 0.56)
        rule(s, Inches(0.62), y, Inches(12.0))
        x = Inches(0.62)
        for c, cell in enumerate(row):
            text(s, x, y + Inches(0.05), cols[c] - Inches(0.12), Inches(0.42), [cell], size=9.5,
                 bold=(c == 0), color=BLUE if c == 0 else DARK)
            x += cols[c]
    text(s, Inches(0.62), Inches(6.5), Inches(12.1), Inches(0.5),
         [('未列入：LSSG、JAG、EMA 等未见正式发表；TrieHI（Directory-Aware）发表状态未核实，故不在相关工作列出。'
           '结论只引用已发表版本。', {'size': 9, 'color': LIGHTGRAY})], size=9)

    # ---------------------------------------------------------------- 6
    s = blank(prs)
    header(s, '索引形态', logo, 6, '单层平面邻接，加两张派生入口表；层级仅出现在入口与掩码中')
    s.shapes.add_picture(str(fig('morphology.png')), Inches(0.62), Inches(1.42), width=Inches(6.35))
    caption(s, Inches(0.62), Inches(5.02), Inches(6.35), '图 3', '属性组、代表点与合并后的单层邻接')
    rows = [('局部图 L(c)', '每个非空直属集合一个 HNSW：M = 64，efC = 200，level-0 容量 2M = 128', '768'),
            ('入口表 S_dir / S_sub', 'attr → 至多 8 个真实 item，倒排数组', '6,589'),
            ('邻接 row(u)', '本地 level-0 边、桥边、快捷边对称化去重', '5,697,634 边'),
            ('allowed 掩码', '查询时由 A 与 σ 编译，不写盘', '每查询一份')]
    text(s, Inches(7.25), Inches(1.42), Inches(5.5), Inches(0.26), ['四个组成部分'], size=11, bold=True, color=BLUE)
    for i, (nm, desc, num) in enumerate(rows):
        y = Inches(1.74 + i * 1.06)
        rule(s, Inches(7.25), y, Inches(5.45))
        text(s, Inches(7.25), y + Inches(0.07), Inches(3.6), Inches(0.24), [nm], size=10.5, bold=True)
        text(s, Inches(10.95), y + Inches(0.07), Inches(1.75), Inches(0.24), [num], size=10.5,
             color=ORANGE, align=PP_ALIGN.RIGHT)
        text(s, Inches(7.25), y + Inches(0.36), Inches(5.45), Inches(0.5), [desc], size=9.5, color=GRAY)
    rule(s, Inches(7.25), Inches(5.98), Inches(5.45))
    text(s, Inches(7.25), Inches(6.08), Inches(5.45), Inches(0.8),
         [('数据形态：深度最大 6（中位 4），扇出最大 18（p90 = 5），多父属性 103 个，入度最大 3；'
           '非空属性 768 个，每属性对象中位 66 个。', {'size': 9.5, 'color': LIGHTGRAY})], size=9.5)

    # ---------------------------------------------------------------- 7
    s = blank(prs)
    header(s, '构建算法', logo, 7, '逆拓扑序遍历属性；每层只连接输入组，不复制子树')
    pseudocode(s, Inches(0.62), Inches(1.36), Inches(8.1), Inches(3.34), [
        'BUILD(V, G = (C, E), p, M, efC)',
        '  for each c ∈ C with members(c) ≠ ∅',
        '      L(c) ← HNSW(M, efC)',
        '      insert all x ∈ members(c) into L(c)',
        '  for c ∈ reverse-topological order of G',
        '      S_dir(c) ← FARTHEST-FIRST(members(c), p)',
        '      P ← S_dir(c) ∪ ⋃_{d ∈ child(c)} S_sub(d)',
        '      S_sub(c) ← FARTHEST-FIRST(P, p)',
        '      A(c) ← {S_dir(c)} ∪ {S_sub(d) : d ∈ child(c)}',
        '      E ← E ∪ MST(A(c))',
        '      E ← E ∪ NEAREST-PORTAL(A(c))',
        '      for x ∈ members(c), G′ ∈ A(c) \\ {own group}',
        '          E ← E ∪ {(x, NEAREST(G′, x))}',
        '      E ← E ∪ (S_dir(c) × S_sub(c))',
        '  for each u: row(u) ← sort-dedup(L0(u) ∪ rev(L0(u)) ∪ E(u))',
        '  return (L, S_dir, S_sub, row)',
    ], '算法 1  索引构建', size=9)
    text(s, Inches(9.0), Inches(1.46), Inches(3.75), Inches(3.5),
         [('参数', {'size': 10.5, 'bold': True, 'color': BLUE}),
          ('p = 8      每属性代表数上限', {'size': 9.5, 'latin': MONO}),
          ('M = 64     局部图出度', {'size': 9.5, 'latin': MONO}),
          ('efC = 200  局部图构建候选数', {'size': 9.5, 'latin': MONO}),
          ('反向桥上限 16', {'size': 9.5}),
          ('输出规模', {'size': 10.5, 'bold': True, 'color': BLUE}),
          ('局部边 4,019,094', {'size': 9.5, 'latin': MONO}),
          ('桥边 1,652,935', {'size': 9.5, 'latin': MONO}),
          ('快捷边 25,605', {'size': 9.5, 'latin': MONO}),
          ('构建时间 8.0–8.2 s', {'size': 9.5})], size=9.5, space=5)
    s.shapes.add_picture(str(fig('merge.png')), Inches(0.62), Inches(4.82), width=Inches(3.55))
    caption(s, Inches(0.62), Inches(6.74), Inches(3.6), '图 4', '选点、桥接与合并')
    text(s, Inches(4.45), Inches(4.95), Inches(8.3), Inches(1.45),
         [('第 9–13 行只生成组间接口；本地 level-0 边在第 15 行并入，保边不减边。默认不做全局出度剪枝，'
           '合并后最大行度 193。', {'size': 10}),
          ('属性 a 的输入组为“直属代表 + 各直接子属性的子图代表”；共享后代在多父路径中只参与一次代表选择。',
           {'size': 10})], size=10, space=6)

    # ---------------------------------------------------------------- 8
    s = blank(prs)
    header(s, '合并规则：代表点选择与出度控制', logo, 8)
    pseudocode(s, Inches(0.62), Inches(1.38), Inches(6.05), Inches(1.9), [
        'FARTHEST-FIRST(pool, p)',
        '  S ← {argmin_{x∈pool} id(x)}',
        '  while |S| < p and |S| < |pool|',
        '      x* ← argmax_{x∈pool\\S} min_{s∈S} ‖x − s‖',
        '      S ← S ∪ {x*}',
        '  return S',
    ], '代表点选择')
    text(s, Inches(0.62), Inches(3.42), Inches(6.05), Inches(1.6),
         [('输入池为直属成员与子属性代表的并集，不展开子树。距离计算次数为 O(p·|pool|·d)，只发生在构建期。'
           '代表为真实 item，可直接作为查询入口。', {'size': 10})], size=10, line=1.2)
    heads = [('边类型', '生成规则', '数量'),
             ('本地边', '局部 level-0 边，双向对称化', '4,019,094'),
             ('组间 MST', '输入组为超点，组间最近代表连边', '1,652,935'),
             ('近代表桥', '每个代表连接其他组最近代表', '（含上行）'),
             ('分布式桥', '直属 item 连接其他组最近代表，反向上限 16', '（含上行）'),
             ('快捷边', 'S_dir(c) × S_sub(c)', '25,605')]
    x0 = Inches(6.95)
    for r, row in enumerate(heads):
        y = Inches(1.38 + r * 0.52)
        rule(s, x0, y, Inches(5.75))
        text(s, x0, y + Inches(0.06), Inches(1.5), Inches(0.3), [row[0]], size=9.5,
             bold=(r == 0), color=BLUE if r == 0 else DARK)
        text(s, x0 + Inches(1.55), y + Inches(0.06), Inches(2.75), Inches(0.42), [row[1]], size=9,
             bold=(r == 0), color=BLUE if r == 0 else DARK)
        text(s, x0 + Inches(4.35), y + Inches(0.06), Inches(1.4), Inches(0.3), [row[2]], size=9,
             bold=(r == 0), color=BLUE if r == 0 else ORANGE, align=PP_ALIGN.RIGHT)
    panel(s, Inches(6.95), Inches(4.62), Inches(5.75), Inches(1.15))
    text(s, Inches(7.12), Inches(4.72), Inches(5.45), Inches(1.0),
         [('行度分布（168,937 行）', {'size': 10, 'bold': True, 'color': BLUE}),
          ('中位 26，p90 74，p99 98，最大 193，均值 33.7；超过 64 的行 26,254（15.5%），超过 128 的行 122。',
           {'size': 9.5})], size=9.5, space=4)
    rule(s, Inches(0.62), Inches(5.98), Inches(12.1))
    text(s, Inches(0.62), Inches(6.08), Inches(12.1), Inches(0.7),
         [('p 与反向桥上限为经验取值，不含最优性结论。保边策略使行度高于 M = 64 的稀疏图；'
           'MST 只保证组间连通，不保证过滤后的子图可导航。', {'size': 9.5, 'color': GRAY})], size=9.5)

    # ---------------------------------------------------------------- 9
    s = blank(prs)
    header(s, '查询算法', logo, 9, '两种作用域共用同一执行过程；σ 只改变入口表与掩码')
    pseudocode(s, Inches(0.62), Inches(1.4), Inches(8.1), Inches(5.0), [
        'SEARCH(v, A, σ, k, ef)',
        '  allowed ← MASK(A, σ)',
        '  R ← σ = direct ? ⋃_{a∈A} S_dir(a) : ⋃_{a∈A} S_sub(a)',
        '  F ← ∅; B ← ∅; pend ← ∅',
        '  for u ∈ R: ADMIT(u)',
        '  SCORE-PENDING()',
        '  while F ≠ ∅',
        '      p ← EXTRACT-MIN(F)',
        '      if |B| = ef and dist(v, x_p) > max(B)',
        '          break',
        '      for u ∈ row(p): ADMIT(u)',
        '      SCORE-PENDING()',
        '  return TOP-K(B, k)',
        '',
        'ADMIT(u):',
        '  if seen[u] then return',
        '  seen[u] ← true',
        '  if ¬allowed[owner(u)] then return',
        '  pend ← pend ∪ {u}; PREFETCH(x_u)',
        'SCORE-PENDING():',
        '  for u ∈ pend: dist(v, x_u); PUSH-OR-REPLACE(F, B, dist, u)',
        '  pend ← ∅',
    ], '算法 2  查询执行')
    text(s, Inches(9.0), Inches(1.4), Inches(3.75), Inches(5.0),
         [('执行性质', {'size': 10.5, 'bold': True, 'color': BLUE}),
          ('不合格点不计距离、不入队、不展开（b = 0）', {'size': 9.5}),
          ('资格在查询内不变，重复拒绝只判定一次', {'size': 9.5}),
          ('多入口共享同一 frontier 与结果堆', {'size': 9.5}),
          ('停止条件同 HNSW：最近候选劣于第 ef 个结果', {'size': 9.5}),
          ('与既有执行器', {'size': 10.5, 'bold': True, 'color': BLUE}),
          ('搜索骨架与 Filtered-DiskANN 的 OR 搜索、LSSG 的多入口 beam 一致；'
           '差异在入口来自属性 DAG 摘要，且两种语义共用同一张图。', {'size': 9.5})], size=9.5, space=7)

    # ---------------------------------------------------------------- 10
    s = blank(prs)
    header(s, '实验设置与结果', logo, 10)
    text(s, Inches(0.62), Inches(1.32), Inches(5.9), Inches(2.3),
         [('设置', {'size': 10.5, 'bold': True, 'color': BLUE}),
          ('数据：SIFT 前 168,937 条，128 维；属性取自 CCS 的 1,929 节点、2,040 条边', {'size': 10}),
          ('作用域：120 组查询，direct 与 recursive 各自解析；空作用域单独统计', {'size': 10}),
          ('参数：M = 64，efC = 200，p = 8；比较点 k = 10/ef = 32 与 k = 100/ef = 128', {'size': 10}),
          ('基线：每属性局部 HNSW 分别检索后归并（NodeANN，n = k）', {'size': 10})], size=10, space=6)
    text(s, Inches(6.75), Inches(1.32), Inches(5.9), Inches(0.26), ['按查询覆盖子图数 G 统计'], size=10.5,
         bold=True, color=BLUE)
    tbl = [('G', '查询数（direct / recursive）', '加速比（k10 / k100）'),
           ('1', '88 / 74', '0.79× / 0.66×'),
           ('2', '7 / 11', '1.16× / 1.12×'),
           ('3', '6 / 12', '2.49× / 1.42×'),
           ('≥4', '0 / 13', '无 direct 样本')]
    for r, row in enumerate(tbl):
        y = Inches(1.66 + r * 0.46)
        rule(s, Inches(6.75), y, Inches(5.75))
        for c, cell in enumerate(row):
            text(s, Inches(6.78 + c * 1.95), y + Inches(0.04), Inches(1.88), Inches(0.42), [cell], size=9,
                 bold=(r == 0), color=BLUE if r == 0 else (ORANGE if (r and cell.startswith('0')) else DARK))
    text(s, Inches(6.75), Inches(4.05), Inches(5.75), Inches(1.0),
         [('加速比 = NodeANN 时延 / 本方案时延。G = 1 时本方案更慢；G = 2 时两种语义、两档参数均更快，'
           '双方 Recall 均为 1，但样本为 7 与 11 条。', {'size': 9.5, 'color': GRAY})], size=9.5, line=1.2)
    rule(s, Inches(0.62), Inches(5.18), Inches(12.1))
    text(s, Inches(0.62), Inches(5.3), Inches(12.1), Inches(1.6),
         [('限制', {'size': 10.5, 'bold': True, 'color': BLUE}),
          ('1. G ≥ 3 的部分样本 Recall 为 0.983–0.997，未达到与基线相同的质量。', {'size': 10}),
          ('2. 属性随机绑定，未验证真实属性与向量的相关性；direct 无 G ≥ 4 样本。', {'size': 10}),
          ('3. 属性更新仅有成本估计，尚未实现 relabel 与 churn 曲线；全局分层与 b > 0 跳转未实现。', {'size': 10})],
         size=10, space=5)

    out.parent.mkdir(parents=True, exist_ok=True)
    prs.save(str(out))
    return len(prs.slides._sldIdLst)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--logo', type=Path, required=True)
    ap.add_argument('--figdir', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    print('slides', build(args.logo, args.out, args.figdir), '->', args.out)
