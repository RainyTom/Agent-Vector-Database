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
SOFT = RGBColor(0xC9, 0xD4, 0xE9)
GREEN = RGBColor(0x2E, 0x7D, 0x32)
CREAM = RGBColor(0xFA, 0xF3, 0xEC)
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
    header(s, '索引总览：四个组件与五个构建步骤', logo, 6,
           '属性内先建局部图，再沿 DAG 逐层合并为一张平面 item 图')
    steps = [('① 局部图', '每个非空直属集合一个 HNSW'),
             ('② 代表点', '每属性选至多 8 个真实 item'),
             ('③ 合并', '逆拓扑序接入子属性代表'),
             ('④ 对称化', '本地边与桥边合并去重'),
             ('⑤ 落盘', 'CSR 邻接 + 倒排入口表')]
    for i, (t, d) in enumerate(steps):
        y = Inches(1.45 + i * 0.62)
        text(s, Inches(0.62), y, Inches(1.0), Inches(0.24), [t], size=9.5, bold=True, color=BLUE)
        text(s, Inches(1.66), y, Inches(4.9), Inches(0.24), [d], size=9.5)
        rule(s, Inches(0.62), y + Inches(0.34), Inches(5.9))
    text(s, Inches(7.02), Inches(1.42), Inches(5.7), Inches(0.26), ['四个组成部分'], size=11, bold=True, color=BLUE)
    comps = [('局部图 L(c)', 'M = 64，efC = 200，level-0 容量 2M = 128；只导出 level-0', '768 个'),
             ('入口表 S_dir / S_sub', 'attr → 至多 8 个真实 item，倒排数组，查询只读', '6,589 条'),
             ('邻接 row(u)', '本地边、桥边、快捷边对称化去重后的一行', '5,697,634 边'),
             ('allowed 掩码', '查询时由 A 与 σ 编译，不写盘', '每查询一份')]
    for i, (nm, desc, num) in enumerate(comps):
        y = Inches(1.74 + i * 1.12)
        rule(s, Inches(7.02), y, Inches(5.7))
        text(s, Inches(7.02), y + Inches(0.07), Inches(3.9), Inches(0.24), [nm], size=10, bold=True)
        text(s, Inches(11.35), y + Inches(0.07), Inches(1.35), Inches(0.24), [num], size=10,
             color=ORANGE, align=PP_ALIGN.RIGHT)
        text(s, Inches(7.02), y + Inches(0.38), Inches(5.7), Inches(0.55), [desc], size=9.5, color=GRAY)
    rule(s, Inches(7.02), Inches(6.28), Inches(5.7))
    text(s, Inches(7.02), Inches(6.36), Inches(5.7), Inches(0.7),
         [('数据：1,929 概念 / 2,040 边 / 103 多父 / 深度 ≤ 6；构建 8.0–8.2 s，单线程。',
           {'size': 9, 'color': LIGHTGRAY})], size=9)

    # ---------------------------------------------------------------- 7
    s = blank(prs)
    header(s, '步骤一：属性内局部图', logo, 7,
           '每个非空直属集合单独建一张 HNSW，彼此独立；这一步不看属性关系')
    # diagram: one cluster with level-0 edges
    cxs, cys = [], []
    base_x, base_y = Inches(0.9), Inches(1.85)
    pts = [(0, 1), (1, 0.4), (1.6, 1.2), (2.4, 0.5), (3.0, 1.5), (2.0, 2.0), (0.7, 2.1), (3.6, 2.2)]
    for i, (dx, dy) in enumerate(pts):
        x = Emu(int(base_x + Inches(dx * 0.95)))
        y = Emu(int(base_y + Inches(dy * 0.8)))
        cxs.append(x)
        cys.append(y)
    for a, b in ((0, 1), (0, 6), (1, 2), (2, 3), (3, 4), (2, 5), (5, 6), (4, 7), (5, 4)):
        ln = s.shapes.add_connector(1, cxs[a], cys[a], cxs[b], cys[b])
        ln.line.color.rgb = SOFT
        ln.line.width = Pt(1.0)
    for i, (x, y) in enumerate(zip(cxs, cys)):
        d = s.shapes.add_shape(MSO_SHAPE.OVAL, Emu(int(x - Inches(0.08))), Emu(int(y - Inches(0.08))),
                               Inches(0.16), Inches(0.16))
        d.fill.solid()
        d.fill.fore_color.rgb = BLUE if i in (0, 2, 5) else SOFT
        d.line.color.rgb = BLUE
        d.line.width = Pt(0.75)
        d.shadow.inherit = False
    text(s, Inches(0.62), Inches(1.5), Inches(4.5), Inches(0.24),
         [('属性 c 的直属集合 members(c)', {'size': 9.5, 'color': GRAY})], size=9.5)
    text(s, Inches(4.6), Inches(3.9), Inches(2.3), Inches(0.6),
         [('蓝点：level-0 上的邻居', {'size': 9, 'color': GRAY}),
          ('灰色细线：HNSW 近邻边', {'size': 9, 'color': GRAY})], size=9, space=3)
    caption(s, Inches(0.62), Inches(4.55), Inches(6.3), '图 4', '单个属性的局部 HNSW（仅示意 level-0）')
    text(s, Inches(7.05), Inches(1.45), Inches(5.65), Inches(2.2),
         [('建图规则', {'size': 10.5, 'bold': True, 'color': BLUE}),
          ('for each c ∈ C with members(c) ≠ ∅:', {'size': 9.5, 'latin': MONO}),
          ('    L(c) ← HNSW(M=64, efC=200, seed=100+c)', {'size': 9.5, 'latin': MONO}),
          ('    insert x ∈ members(c) 到 L(c)', {'size': 9.5, 'latin': MONO}),
          ('空属性不建图，但保留在 DAG 中', {'size': 9.5, 'color': GRAY})], size=9.5, space=5)
    panel(s, Inches(7.05), Inches(3.55), Inches(5.65), Inches(1.35))
    text(s, Inches(7.22), Inches(3.66), Inches(5.35), Inches(1.15),
         [('这一步的产出', {'size': 10, 'bold': True, 'color': BLUE}),
          ('768 张局部图；本地 level-0 边 4,019,094 条（双向计数）', {'size': 9.5}),
          ('高层不导出：合并图只使用 level-0 邻接', {'size': 9.5})], size=9.5, space=4)
    text(s, Inches(7.05), Inches(5.15), Inches(5.65), Inches(1.4),
         [('对象数分布：每属性中位 66 个对象，p90 492，最大 9,823；因此局部图大小差异很大，'
           '单张局部图的构建成本由该属性对象数决定。', {'size': 9.5})], size=9.5)

    # ---------------------------------------------------------------- 8
    s = blank(prs)
    header(s, '步骤二：代表点选择', logo, 8,
           'farthest-first 从候选池取至多 8 个真实 item；两类代表分别供两种检索使用')
    # 16 dots grid with 4 selected in order
    grid_x, grid_y = Inches(0.95), Inches(2.05)
    coords = []
    order = [5, 10, 1, 14]
    for i in range(16):
        cx = Emu(int(grid_x + Inches((i % 4) * 0.62)))
        cy = Emu(int(grid_y + Inches((i // 4) * 0.62)))
        coords.append((cx, cy))
    for i, (cx, cy) in enumerate(coords):
        d = s.shapes.add_shape(MSO_SHAPE.OVAL, Emu(int(cx - Inches(0.1))), Emu(int(cy - Inches(0.1))),
                               Inches(0.2), Inches(0.2))
        d.fill.solid()
        d.fill.fore_color.rgb = ORANGE if i in order else SOFT
        d.line.color.rgb = ORANGE if i in order else BLUE
        d.line.width = Pt(0.9)
        d.shadow.inherit = False
        if i in order:
            text(s, Emu(int(cx - Inches(0.42))), Emu(int(cy - Inches(0.11))), Inches(0.3), Inches(0.22),
                 ['①②③④'[order.index(i)]], size=10, bold=True, color=ORANGE, align=PP_ALIGN.RIGHT)
    for k in range(len(order) - 1):
        a, b = coords[order[k]], coords[order[k + 1]]
        ln = s.shapes.add_connector(1, a[0], a[1], b[0], b[1])
        ln.line.color.rgb = ORANGE
        ln.line.width = Pt(0.9)
        body = ln.line._get_or_add_ln()
        dash = body.makeelement('{http://schemas.openxmlformats.org/drawingml/2006/main}prstDash', {})
        dash.set('val', 'dash')
        body.append(dash)
    text(s, Inches(0.62), Inches(1.42), Inches(4.2), Inches(0.24),
         [('候选池（直属对象 ∪ 子属性代表）', {'size': 9.5, 'color': GRAY})], size=9.5)
    caption(s, Inches(0.62), Inches(4.9), Inches(4.4), '图 5', '①②③④ 为选中顺序，虚线为最远优先的连线')
    text(s, Inches(0.62), Inches(5.3), Inches(4.6), Inches(1.3),
         [('选点规则', {'size': 10, 'bold': True, 'color': BLUE}),
          ('第 1 个取最小 ID；其后每次取“到已选集合最近距离”最大的候选，平局取小 ID。',
           {'size': 9.5})], size=9.5, space=4)
    text(s, Inches(5.6), Inches(1.42), Inches(7.15), Inches(2.6),
         [('两类代表（沿 DAG 递推）', {'size': 10.5, 'bold': True, 'color': BLUE}),
          ('S_dir(c) = FF( members(c), p )', {'size': 10, 'latin': MONO}),
          ('S_sub(c) = FF( S_dir(c) ∪ ⋃_{d ∈ child(c)} S_sub(d), p )', {'size': 10, 'latin': MONO}),
          ('用途：S_dir 供非递归检索入口；S_sub 供递归检索入口', {'size': 9.5}),
          ('规模：每属性 ≤ 8，共 6,589 条入口；不新建质心、不删除 item',
           {'size': 9.5, 'color': GRAY})], size=9.5, space=6)
    panel(s, Inches(5.6), Inches(4.1), Inches(7.15), Inches(1.5))
    text(s, Inches(5.78), Inches(4.22), Inches(6.8), Inches(1.3),
         [('输入池的实际构成（属性 P 有两个子属性 A、B）', {'size': 10, 'bold': True, 'color': BLUE}),
          ('S_dir(P) 取 members(P)；S_sub(P) 的池 = S_dir(P) ∪ S_sub(A) ∪ S_sub(B)，'
           '至多 8 + 8 + 8 个候选，再从池中选 8 个。', {'size': 9.5}),
          ('共享后代（多父）只在各父的池中出现一次，最终结果去重。', {'size': 9.5, 'color': GRAY})], size=9.5, space=4)
    text(s, Inches(5.6), Inches(5.8), Inches(7.15), Inches(0.9),
         [('代价：单次选点为 O(p·|pool|·d) 次距离，只在构建期发生；查询期入口表为 O(1) 查找。',
           {'size': 9.5})], size=9.5)

    # ---------------------------------------------------------------- 9
    s = blank(prs)
    header(s, '步骤三：合并——输入组与四类边', logo, 9,
           '一个属性的“输入组”是它自己的代表，加上每个直接子属性的代表')
    def cluster(x0, y0, n=3, step=0.46):
        pts = []
        for i in range(n):
            cx = Emu(int(Inches(x0 + i * step)))
            cy = Emu(int(Inches(y0)))
            d = s.shapes.add_shape(MSO_SHAPE.OVAL, Emu(int(cx - Inches(0.09))), Emu(int(cy - Inches(0.09))),
                                   Inches(0.18), Inches(0.18))
            d.fill.solid()
            d.fill.fore_color.rgb = SOFT
            d.line.color.rgb = BLUE
            d.line.width = Pt(0.75)
            d.shadow.inherit = False
            pts.append((cx, cy))
        return pts

    def seg(p, q, color, width, dash=False):
        ln = s.shapes.add_connector(1, p[0], p[1], q[0], q[1])
        ln.line.color.rgb = color
        ln.line.width = Pt(width)
        if dash:
            body = ln.line._get_or_add_ln()
            d = body.makeelement('{http://schemas.openxmlformats.org/drawingml/2006/main}prstDash', {})
            d.set('val', 'dash')
            body.append(d)

    P = cluster(1.15, 3.15, 3)
    A = cluster(3.6, 2.0, 3)
    B = cluster(3.6, 4.3, 3)
    text(s, P[0][0] - Inches(0.12), P[0][1] - Inches(0.5), Inches(2.2), Inches(0.24),
         ['本属性 P 的直属组'], size=8, color=GRAY)
    text(s, A[0][0] - Inches(0.12), A[0][1] - Inches(0.5), Inches(2.2), Inches(0.24),
         ['子属性 A 的子图代表组'], size=8, color=GRAY)
    text(s, B[0][0] - Inches(0.12), B[0][1] + Inches(0.26), Inches(2.2), Inches(0.24),
         ['子属性 B 的子图代表组'], size=8, color=GRAY)
    seg(A[1], B[1], BLUE, 2.0)
    seg(P[0], A[0], ORANGE, 1.25)
    seg(P[2], B[2], ORANGE, 1.25)
    seg(P[1], A[2], GRAY, 0.9, dash=True)
    seg(P[1], B[0], GRAY, 0.9, dash=True)
    seg(A[2], B[2], GREEN, 1.25)
    legend = [('本地边（局部图，未在此画出）', SOFT), ('组间 MST', BLUE), ('近代表桥', ORANGE),
              ('分布式桥', GRAY), ('快捷边', GREEN)]
    for i, (lab, col) in enumerate(legend):
        y = Inches(5.0 + i * 0.28)
        sw = s.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0.62), y, Inches(0.24), Inches(0.1))
        sw.fill.solid()
        sw.fill.fore_color.rgb = col
        sw.line.fill.background()
        sw.shadow.inherit = False
        text(s, Inches(0.98), y - Inches(0.06), Inches(2.6), Inches(0.22), [lab], size=8, color=GRAY)
    caption(s, Inches(3.6), Inches(5.0), Inches(3.0), '图 6', '三组与四类跨组边')
    cols = [Inches(1.85), Inches(3.1), Inches(1.05)]
    heads = ['边类型', '生成规则', '数量']
    x = Inches(7.05)
    for w, hd in zip(cols, heads):
        text(s, x, Inches(1.42), w - Inches(0.08), Inches(0.24), [hd], size=9.5, bold=True, color=BLUE)
        x += w
    rule(s, Inches(7.05), Inches(1.68), Inches(6.0), color=BLUE)
    rows = [('本地边', '局部 level-0 边，双向对称化', '4,019,094'),
            ('组间 MST', '输入组为超点，组间最近代表连边', '—'),
            ('近代表桥', '每个代表连到其他组的最近代表', '—'),
            ('分布式桥', '直属对象连到其他组最近代表，反向每点上限 16', '1,652,935'),
            ('快捷边', 'S_dir(c) × S_sub(c)', '25,605')]
    for r, row in enumerate(rows):
        y = Inches(1.8 + r * 0.62)
        rule(s, Inches(7.05), y, Inches(6.0))
        x = Inches(7.05)
        for c, cell in enumerate(row):
            text(s, x, y + Inches(0.06), cols[c] - Inches(0.1), Inches(0.5), [cell], size=9, bold=(c == 0))
            x += cols[c]
    text(s, Inches(7.05), Inches(5.0), Inches(6.0), Inches(1.5),
         [('同一属性内，MST 保证输入组连通；近代表桥与分布式桥提供跨组的几何近邻；'
           '快捷边把直属代表与子图代表相连。', {'size': 9.5}),
          ('多父属性被多个父属性引用时，只在其每个父的输入组里出现一次，不复制对象。',
           {'size': 9.5, 'color': GRAY})], size=9.5, space=5)

    # ---------------------------------------------------------------- 10
    s = blank(prs)
    header(s, '步骤四：沿 DAG 逐层推进', logo, 10,
           '逆拓扑序处理：子属性的代表先就绪，父属性再接入；每层只连接“组与组”')
    panels = [('① 处理子属性 A、B', '各自完成局部图与代表选择，此时两组的图互相独立'),
              ('② 处理父属性 P', 'P 的输入组 = 直属组 + A 的代表组 + B 的代表组；组间 MST 与桥在此加入'),
              ('③ 继续向上', 'P 的 S_sub 成为上层属性的输入组；重复直到没有父属性的节点')]
    for i, (t, d) in enumerate(panels):
        x = Inches(0.62 + i * 4.12)
        panel(s, x, Inches(1.42), Inches(3.9), Inches(2.75), fill=WHITE, border=RULE)
        text(s, x + Inches(0.16), Inches(1.52), Inches(3.55), Inches(0.24), [t], size=10, bold=True, color=BLUE)
        text(s, x + Inches(0.16), Inches(1.86), Inches(3.55), Inches(0.75), [d], size=9, color=DARK)
        gy = Inches(3.35)
        def dot(px, py, label=''):
            d = s.shapes.add_shape(MSO_SHAPE.OVAL, Emu(int(x + Inches(px) - Inches(0.15))),
                                   Emu(int(gy + Inches(py) - Inches(0.15))), Inches(0.3), Inches(0.3))
            d.fill.solid()
            d.fill.fore_color.rgb = WHITE
            d.line.color.rgb = BLUE
            d.line.width = Pt(1.0)
            d.shadow.inherit = False
            if label:
                text(s, Emu(int(x + Inches(px) - Inches(0.15))), Emu(int(gy + Inches(py) - Inches(0.11))),
                     Inches(0.3), Inches(0.2), [label], size=8, bold=True, color=BLUE, align=PP_ALIGN.CENTER)
            return (Emu(int(x + Inches(px))), Emu(int(gy + Inches(py))))
        def line(p, q, color=BLUE, width=1.0):
            ln = s.shapes.add_connector(1, p[0], p[1], q[0], q[1])
            ln.line.color.rgb = color
            ln.line.width = Pt(width)
        if i == 0:
            a1 = dot(0.85, -0.45, 'A'); a2 = dot(1.4, -0.45)
            b1 = dot(2.45, 0.45, 'B'); b2 = dot(3.0, 0.45)
            line(a1, a2, SOFT, 1.4); line(b1, b2, SOFT, 1.4)
        elif i == 1:
            a1 = dot(0.75, -0.55, 'A'); a2 = dot(1.25, -0.55)
            b1 = dot(2.65, -0.55, 'B'); b2 = dot(3.15, -0.55)
            p1 = dot(2.0, 0.55, 'P')
            line(a1, a2, SOFT, 1.4); line(b1, b2, SOFT, 1.4)
            line(a2, p1, BLUE, 1.4); line(b1, p1, BLUE, 1.4)
        else:
            p1 = dot(1.7, 0.5, 'P'); q1 = dot(1.7, -0.55, 'Q')
            line(p1, q1, BLUE, 1.4)
        if i < 2:
            arrow(s, x + Inches(3.95), Inches(2.8), x + Inches(4.1), Inches(2.8))
    caption(s, Inches(0.62), Inches(4.3), Inches(12.0), '图 7', '逆拓扑序下同一批对象在不同层的接入方式')
    rule(s, Inches(0.62), Inches(4.64), Inches(12.1))
    text(s, Inches(0.62), Inches(4.76), Inches(12.1), Inches(2.2),
         [('推进顺序的三条约束', {'size': 10.5, 'bold': True, 'color': BLUE}),
          ('1. 顺序：按逆拓扑序处理属性，保证处理父属性时，所有子属性的 S_sub 已经算完。', {'size': 9.5}),
          ('2. 范围：每个属性只接入“直属对象 + 直接子属性的代表”，不展开整棵子树；总工作量与'
           'Σ_c (|members(c)| + |children(c)|) 同阶。', {'size': 9.5}),
          ('3. 共享：多父属性的 S_sub 只计算一次，被多个父属性按引用复用，不复制对象与边。', {'size': 9.5}),
          ('实测规模：768 个非空属性、103 个多父概念；整图构建 8.0–8.2 s。',
           {'size': 9.5, 'color': GRAY})], size=9.5, space=5)

    # ---------------------------------------------------------------- 11
    s = blank(prs)
    header(s, '步骤五：对称化、落盘与入口表', logo, 11,
           '所有边在收尾时统一去重，写成一行一行的邻接；查询只读这两类结构')
    text(s, Inches(0.62), Inches(1.42), Inches(6.1), Inches(0.24),
         [('邻接存储：row(u) 记录 u 的全部邻居', {'size': 10, 'bold': True, 'color': BLUE})], size=10)
    # CSR illustration
    box_y = Inches(1.75)
    for i in range(12):
        cell = s.shapes.add_shape(MSO_SHAPE.RECTANGLE, Emu(int(Inches(0.62 + i * 0.46))), box_y,
                                  Inches(0.46), Inches(0.34))
        cell.fill.solid()
        cell.fill.fore_color.rgb = ORANGE if i < 4 else (BLUE if i < 8 else GREEN)
        cell.line.color.rgb = WHITE
        cell.line.width = Pt(0.75)
        cell.shadow.inherit = False
        text(s, Emu(int(Inches(0.62 + i * 0.46))), Emu(int(box_y + Inches(0.06))), Inches(0.46), Inches(0.22),
             [str(1001 + i * 3)], size=7.5, color=WHITE, align=PP_ALIGN.CENTER)
    text(s, Inches(0.62), Inches(2.16), Inches(6.1), Inches(0.24),
         [('橙 4 条本地 level-0 边 │ 蓝 4 条桥边 │ 绿 4 条快捷边（示意）', {'size': 8.5, 'color': GRAY})], size=8.5)
    text(s, Inches(0.62), Inches(2.5), Inches(6.1), Inches(0.7),
         [('row(u) ← sort-dedup( L0(u) ∪ rev(L0(u)) ∪ E(u) )', {'size': 9.5, 'latin': MONO}),
          ('对称化后每行内按邻居 ID 排序，去重；行之间用 offsets 数组定位（CSR）。',
           {'size': 9})], size=9, space=4)
    caption(s, Inches(0.62), Inches(3.3), Inches(6.1), '图 8', '单行邻接的组成；实际行度中位 26')
    text(s, Inches(0.62), Inches(3.75), Inches(6.1), Inches(0.24),
         [('入口表：两张倒排数组', {'size': 10, 'bold': True, 'color': BLUE})], size=10)
    for i, (name, desc) in enumerate([('S_dir[c]', '该概念直属对象的代表，供非递归检索'),
                                      ('S_sub[c]', '该概念子树的代表，供递归检索')]):
        y = Inches(4.05 + i * 0.62)
        panel(s, Inches(0.62), y, Inches(6.1), Inches(0.52), fill=WHITE, border=RULE)
        text(s, Inches(0.76), y + Inches(0.06), Inches(1.5), Inches(0.24), [name], size=9.5,
             bold=True, color=BLUE, latin=MONO)
        text(s, Inches(2.3), y + Inches(0.06), Inches(4.3), Inches(0.24), [desc], size=9)
    text(s, Inches(0.62), Inches(5.4), Inches(6.1), Inches(1.2),
         [('数量：768 个非空属性 × 两张表，合计 6,589 条入口条目；查询时按概念直接取，不参与距离计算。',
           {'size': 9.5})], size=9.5)
    text(s, Inches(7.05), Inches(1.42), Inches(5.7), Inches(0.24),
         [('落盘后的行度分布（168,937 行）', {'size': 10, 'bold': True, 'color': BLUE})], size=10)
    stats = [('中位', '26'), ('p90', '74'), ('p99', '98'), ('最大', '193'), ('均值', '33.7')]
    for i, (k, v) in enumerate(stats):
        x = Inches(7.05 + i * 1.12)
        text(s, x, Inches(1.75), Inches(1.0), Inches(0.24), [k], size=8.5, color=GRAY)
        text(s, x, Inches(1.98), Inches(1.0), Inches(0.3), [v], size=13, bold=True, color=BLUE)
    text(s, Inches(7.05), Inches(2.5), Inches(5.7), Inches(0.8),
         [('> 64 的行 26,254（15.5%），> 128 的行 122；行度高于 M = 64 的稀疏图，因为默认不做全局剪枝。',
           {'size': 9.5})], size=9.5)
    panel(s, Inches(7.05), Inches(3.4), Inches(5.7), Inches(1.5))
    text(s, Inches(7.22), Inches(3.52), Inches(5.35), Inches(1.3),
         [('存储开销（仅计边与入口）', {'size': 10, 'bold': True, 'color': BLUE}),
          ('邻接 5,697,634 条 × 4 B（item ID）= 21.7 MB', {'size': 9.5, 'latin': MONO}),
          ('offsets 168,938 × 8 B = 1.35 MB', {'size': 9.5, 'latin': MONO}),
          ('入口 6,589 × 4 B ≈ 26 KB', {'size': 9.5, 'latin': MONO}),
          ('不含向量本体与局部图结构', {'size': 8.5, 'color': GRAY})], size=9.5, space=4)
    text(s, Inches(7.05), Inches(5.15), Inches(5.7), Inches(1.4),
         [('局部图本身仍保留在内存中：NodeANN 基线需要它们；本方案的查询只读 row(u) 与入口表，'
           '局部图仅在维护（增删对象）时使用。', {'size': 9.5})], size=9.5)

    # ---------------------------------------------------------------- 7
    s = blank(prs)
    header(s, '构建算法汇总', logo, 12, '逆拓扑序遍历属性；每层只连接输入组，不复制子树')
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
    text(s, Inches(0.66), Inches(5.05), Inches(12.0), Inches(1.5),
         [('第 9–13 行只生成组间接口；本地 level-0 边在第 15 行并入，保边不减边。默认不做全局出度剪枝，'
           '合并后最大行度 193。', {'size': 10}),
          ('属性 a 的输入组为“直属代表 + 各直接子属性的子图代表”；共享后代在多父路径中只参与一次代表选择。',
           {'size': 10}),
          ('自检：行内邻居无重复且正反向一致；每个非空属性的入口不超过 8 个；所有对象至少出现在一行中。',
           {'size': 10, 'color': GRAY})], size=10, space=6)

    # ---------------------------------------------------------------- 9
    s = blank(prs)
    header(s, '查询算法', logo, 13, '两种作用域共用同一执行过程；σ 只改变入口表与掩码')
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
    header(s, '实验设置与结果', logo, 14)
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
