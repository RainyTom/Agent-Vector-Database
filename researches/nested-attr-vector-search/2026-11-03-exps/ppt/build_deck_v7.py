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
    header(s, '由查询语义推导索引结构', logo, 6,
           '先列出查询必须满足的事实，再由事实推出结构约束，最后落到具体步骤')
    geo = [(0.62, 2.78), (3.55, 2.72), (6.42, 3.35), (9.92, 2.7)]
    heads = ['查询侧事实', '由此得到的设计要求', '结构决策', '落地步骤']
    for (hx, hw), hd in zip(geo, heads):
        text(s, Inches(hx), Inches(1.4), Inches(hw), Inches(0.24), [hd], size=9.5, bold=True, color=BLUE)
    rule(s, Inches(0.62), Inches(1.66), Inches(12.1), color=BLUE)
    rows = [('两种语义的结果集不同，但要在同一份索引上执行',
             '语义不能写进边，否则一份索引只能服务一种语义',
             '谓词不入图：查询时把 A 与 σ 编译成 allowed 掩码', '⑤ 落盘 / 查询'),
            ('一次查询可覆盖多个概念，递归还要含整个后代闭包',
             '不能按概念拆成多张图分别搜，否则搜索次数随概念数增长',
             '一张平面图 + 多入口 + 单一 frontier', '③ 合并 / ④ 逐层'),
            ('入口点必须合格，否则首个候选即被掩码拒绝',
             '入口取自真实对象，且在几何上分散',
             'farthest-first 选 S_dir 与 S_sub，每概念 ≤ 8', '② 代表点'),
            ('递归范围要从父概念进入子树，路径不能断',
             '父概念的代表必须能直接到达子概念的代表',
             '逆拓扑序聚合子代表 + 快捷边 S_dir(c) × S_sub(c)', '④ 逐层 / ③ 合并'),
            ('多父概念的后代集合互相重叠',
             '同一对象不能在多个父路径里被重复计算或重复存边',
             '代表按引用复用，共享后代在池中只出现一次', '② 代表点 / ④ 逐层')]
    for r, row in enumerate(rows):
        y = Inches(1.76 + r * 0.8)
        rule(s, Inches(0.62), y, Inches(12.1))
        for c, cell in enumerate(row):
            gx, gw = geo[c]
            text(s, Inches(gx), y + Inches(0.08), Inches(gw), Inches(0.68), [cell],
                 size=8.8, color=(BLUE if c == 2 else DARK))
    text(s, Inches(0.62), Inches(6.42), Inches(11.6), Inches(0.5),
         [('五条事实彼此独立，对应五项决策；每项决策在第 8–12 页有对应的构建步骤与不变量。',
           {'size': 9.5, 'color': LIGHTGRAY})], size=9.5)

    # ---------------------------------------------------------------- 7
    s = blank(prs)
    header(s, '构建流程的依赖关系', logo, 7,
           '五个步骤不是并列清单，而是一条数据依赖链；箭头方向即计算顺序')
    def node(x, y, w, h, title, sub, fill=WHITE, border=RULE):
        panel(s, Inches(x), Inches(y), Inches(w), Inches(h), fill=fill, border=border)
        text(s, Inches(x + 0.13), Inches(y + 0.08), Inches(w - 0.26), Inches(0.24), [title],
             size=9.5, bold=True, color=BLUE)
        text(s, Inches(x + 0.13), Inches(y + 0.36), Inches(w - 0.26), Inches(0.45),
             [(ln, {'size': 8.3, 'color': GRAY}) for ln in sub.split('\n')], size=8.3, space=2)

    def hline(x1, y, x2, arrow=True):
        ln = s.shapes.add_connector(1, Inches(x1), Inches(y), Inches(x2), Inches(y))
        ln.line.color.rgb = GRAY
        ln.line.width = Pt(1.0)
        if arrow:
            body = ln.line._get_or_add_ln()
            head = body.makeelement('{http://schemas.openxmlformats.org/drawingml/2006/main}tailEnd', {})
            head.set('type', 'triangle')
            body.append(head)

    def vline(x, y1, y2, arrow=True):
        ln = s.shapes.add_connector(1, Inches(x), Inches(y1), Inches(x), Inches(y2))
        ln.line.color.rgb = GRAY
        ln.line.width = Pt(1.0)
        if arrow:
            body = ln.line._get_or_add_ln()
            head = body.makeelement('{http://schemas.openxmlformats.org/drawingml/2006/main}tailEnd', {})
            head.set('type', 'triangle')
            body.append(head)

    node(0.62, 1.55, 2.3, 0.9, 'members(c)', '概念的直属对象，按 ID 升序')
    node(3.45, 1.55, 2.3, 0.9, 'L(c)', '局部 HNSW；只导出 level-0')
    node(0.62, 3.15, 2.3, 0.9, 'S_dir(c)', 'FF(members(c), p)，≤ 8')
    node(3.45, 3.15, 2.3, 0.9, 'S_sub(c)', 'FF(S_dir ∪ ⋃S_sub(child), p)')
    node(6.6, 3.15, 2.3, 0.9, 'E(c)', 'MST + 近代表桥 + 分布式桥 + 快捷边')
    node(9.55, 3.15, 2.3, 0.9, 'row(u)', '本地边 ∪ 桥 ∪ 快捷，对称化去重')
    node(9.55, 1.55, 2.3, 0.9, '入口表', 'S_dir / S_sub 两份倒排', fill=MIST)

    hline(2.92, 2.0, 3.45)
    vline(1.77, 2.45, 3.15)
    hline(2.92, 3.6, 3.45)
    hline(5.75, 3.6, 6.6)
    hline(8.9, 3.6, 9.55)
    # L(c) 的 level-0 边汇入 row(u)：走上方总线，避免穿过中间节点
    vline(4.6, 2.45, 2.72, arrow=False)
    hline(4.6, 2.72, 10.7, arrow=False)
    vline(10.7, 2.72, 3.15)
    # 入口表 → 查询一行
    hline(8.85, 2.0, 9.55)
    text(s, Inches(6.35), Inches(1.66), Inches(2.4), Inches(0.4),
         [('由 S_dir 与 S_sub 直接生成（落盘时）', {'size': 8, 'color': GRAY})], size=8)
    text(s, Inches(0.62), Inches(4.18), Inches(5.8), Inches(0.3),
         [('S_sub(c) 依赖所有子属性的 S_sub，故按逆拓扑序求解（第 11 页）。', {'size': 8.5, 'color': GRAY})], size=8.5)
    text(s, Inches(6.6), Inches(4.18), Inches(6.1), Inches(0.3),
         [('L(c) 与 S_dir(c) 相互独立，可并行；row(u) 最后统一对称化。', {'size': 8.5, 'color': GRAY})], size=8.5)

    text(s, Inches(0.62), Inches(4.62), Inches(12.1), Inches(0.24), ['每一步的输入与输出'], size=10,
         bold=True, color=BLUE)
    io = [('① 局部图', 'members(c)', 'L(c) 与 level-0 边'),
          ('② 代表点', 'members(c)、子属性的 S_sub', 'S_dir(c)、S_sub(c)'),
          ('③ 合并', 'S_dir、S_sub、L(c) 的 level-0', '组间边 E(c)'),
          ('④ 逐层', '子属性的 S_sub', '父属性的 S_sub 与 E(c)'),
          ('⑤ 落盘', 'L0 与 E(c)', 'row(u)、入口表')]
    for i, (a, b, c) in enumerate(io):
        y = Inches(4.95 + i * 0.38)
        text(s, Inches(0.62), y, Inches(1.2), Inches(0.26), [a], size=9, bold=True, color=BLUE)
        text(s, Inches(1.95), y, Inches(4.4), Inches(0.26), [b], size=9, color=DARK)
        text(s, Inches(6.5), y, Inches(6.2), Inches(0.26), [c], size=9, color=ORANGE)

    # ---------------------------------------------------------------- 8
    s = blank(prs)
    header(s, '步骤一：属性内局部图', logo, 8,
           '输入 members(c)，输出一张只看几何的 HNSW；与属性关系无关')
    cxs, cys = [], []
    base_x, base_y = Inches(0.9), Inches(2.0)
    pts = [(0, 1), (1, 0.4), (1.6, 1.2), (2.4, 0.5), (3.0, 1.5), (2.0, 2.0), (0.7, 2.1), (3.6, 2.2)]
    for i, (dx, dy) in enumerate(pts):
        cxs.append(Emu(int(base_x + Inches(dx * 0.95))))
        cys.append(Emu(int(base_y + Inches(dy * 0.8))))
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
    text(s, Inches(0.62), Inches(1.6), Inches(4.6), Inches(0.24),
         [('属性 c 的直属集合 members(c)', {'size': 9, 'color': GRAY})], size=9)
    text(s, Inches(4.55), Inches(4.0), Inches(2.3), Inches(0.6),
         [('深色：level-0 上的邻居', {'size': 8.5, 'color': GRAY}),
          ('浅线：HNSW 近邻边', {'size': 8.5, 'color': GRAY})], size=8.5, space=3)
    caption(s, Inches(0.62), Inches(4.6), Inches(6.2), '图 3', '单个属性的局部 HNSW（示意 level-0）')
    chain = [('前置', 'members(c) 已按 ID 升序就绪；属性之间尚不参与'),
             ('操作', 'L(c) ← HNSW(M=64, efC=200, seed=100+c)；逐点 addPoint'),
             ('后置', 'L(c) 存在且只含该概念的对象；导出 level-0 邻接，高层不导出')]
    for i, (tag, desc) in enumerate(chain):
        y = Inches(1.45 + i * 0.78)
        text(s, Inches(7.05), y, Inches(0.8), Inches(0.24), [tag], size=9.5, bold=True, color=BLUE)
        text(s, Inches(7.9), y, Inches(4.85), Inches(0.66), [desc], size=9)
    rule(s, Inches(7.05), Inches(3.82), Inches(5.7))
    text(s, Inches(7.05), Inches(3.94), Inches(5.7), Inches(1.5),
         [('产出与规模', {'size': 10, 'bold': True, 'color': BLUE}),
          ('768 张局部图；本地 level-0 边 4,019,094 条（双向计数）', {'size': 9.5}),
          ('对象数：中位 66 / p90 492 / 最大 9,823 —— 单图构建成本由该概念的对象数决定', {'size': 9.5}),
          ('空概念不建图，但仍保留在 DAG 中，供上层聚合', {'size': 9}),
          ('规模：非空概念占 768 / 1,929 = 39.8%', {'size': 9})], size=9.5, space=4)
    panel(s, Inches(7.05), Inches(5.55), Inches(5.7), Inches(0.95))
    text(s, Inches(7.22), Inches(5.64), Inches(5.35), Inches(0.8),
         [('不变量 I1：每个非空概念恰有一张局部图；L(c) 的对象集合 = members(c)。',
           {'size': 9.5})], size=9.5)

    # ---------------------------------------------------------------- 9
    s = blank(prs)
    header(s, '步骤二：代表点选择', logo, 9,
           '输入 members(c) 与子属性代表，输出两类入口；这是唯一“压缩”语义的一步')
    grid_x, grid_y = Inches(0.95), Inches(2.05)
    coords = []
    order = [5, 10, 1, 14]
    for i in range(16):
        coords.append((Emu(int(grid_x + Inches((i % 4) * 0.62))), Emu(int(grid_y + Inches((i // 4) * 0.62)))))
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
    text(s, Inches(0.62), Inches(1.45), Inches(4.6), Inches(0.24),
         [('候选池：直属对象 ∪ 各子属性的 S_sub', {'size': 9, 'color': GRAY})], size=9)
    caption(s, Inches(0.62), Inches(5.05), Inches(4.5), '图 4', '①②③④ 为选中顺序')
    text(s, Inches(0.62), Inches(5.4), Inches(4.6), Inches(1.2),
         [('选点：首个取最小 ID；其后每次取到已选集最小距离最大者，平局取小 ID。',
           {'size': 9})], size=9)
    chain = [('前置', 'members(c) 就绪；若 c 有子属性，其 S_sub 已算完'),
             ('操作', 'S_dir(c) ← FF(members(c), p)；S_sub(c) ← FF(S_dir ∪ ⋃S_sub(child), p)'),
             ('后置', 'S_dir(c) ⊆ members(c)；S_sub(c) ⊆ members(c) ∪ 各子树的代表池；两者 ≤ 8 个真实 item')]
    for i, (tag, desc) in enumerate(chain):
        y = Inches(1.45 + i * 0.66)
        text(s, Inches(5.6), y, Inches(0.8), Inches(0.24), [tag], size=9.5, bold=True, color=BLUE)
        text(s, Inches(6.45), y, Inches(6.3), Inches(0.56), [desc], size=9)
    rule(s, Inches(5.6), Inches(3.45), Inches(7.15))
    text(s, Inches(5.6), Inches(3.56), Inches(7.15), Inches(1.5),
         [('两项递推关系的含义', {'size': 10, 'bold': True, 'color': BLUE}),
          ('S_dir 是“本概念”的入口：非递归检索从这里进图。', {'size': 9}),
          ('S_sub 是本概念子树的入口：递归检索从这里进图，且成为父属性的候选池。', {'size': 9}),
          ('池的规模上界：1 + |children(c)| 组 × 8；p=8 时单次选点最多 72 个候选。', {'size': 9})],
         size=9, space=4)
    panel(s, Inches(5.6), Inches(5.1), Inches(7.15), Inches(1.1))
    text(s, Inches(5.78), Inches(5.2), Inches(6.8), Inches(0.95),
         [('不变量 I2 / I3：S_dir(c) ⊆ members(c)；S_sub(c) 的元素必属 c 或其后代概念的直属集合；'
           '两者都是真实 item，可为查询入口。', {'size': 9.5})], size=9.5)

    # ---------------------------------------------------------------- 10
    s = blank(prs)
    header(s, '步骤三：合并——由代表生成组间边', logo, 10,
           '输入一个概念的输入组，输出该概念的组间边；边只连接“组与组”')
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
    legend = [('组间 MST', BLUE), ('近代表桥', ORANGE), ('分布式桥', GRAY), ('快捷边', GREEN)]
    for i, (lab, col) in enumerate(legend):
        y = Inches(5.05 + i * 0.26)
        sw = s.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0.62), y, Inches(0.24), Inches(0.1))
        sw.fill.solid()
        sw.fill.fore_color.rgb = col
        sw.line.fill.background()
        sw.shadow.inherit = False
        text(s, Inches(0.98), y - Inches(0.06), Inches(2.4), Inches(0.22), [lab], size=8, color=GRAY)
    caption(s, Inches(3.55), Inches(5.05), Inches(3.0), '图 5', '三组与四类跨组边')
    chain = [('前置', 'S_dir(c)、各子属性 S_sub 就绪；L(c) 的 level-0 边可查'),
             ('操作', '输入组 A(c) = {S_dir(c)} ∪ {S_sub(d)}；组间跑 MST，代表补近邻桥，直属对象补分布式桥，'
                      '再加 S_dir × S_sub 快捷边'),
             ('后置', '任一组都能经 MST 到达任一组；跨组边端点均为真实对象；反向桥每点 ≤ 16')]
    for i, (tag, desc) in enumerate(chain):
        y = Inches(1.42 + i * 0.86)
        text(s, Inches(7.05), y, Inches(0.8), Inches(0.24), [tag], size=9.5, bold=True, color=BLUE)
        text(s, Inches(7.9), y, Inches(4.85), Inches(0.76), [desc], size=9)
    rule(s, Inches(7.05), Inches(4.1), Inches(5.7))
    text(s, Inches(7.05), Inches(4.2), Inches(5.7), Inches(1.4),
         [('边数与规模', {'size': 10, 'bold': True, 'color': BLUE}),
          ('组间与桥合计 1,652,935 条；快捷边 25,605 条', {'size': 9.5}),
          ('MST 每个概念 |A(c)| − 1 条；近代表桥约 |A(c)|·p 条', {'size': 9.5}),
          ('多父概念只在其每个父的输入组中出现一次', {'size': 9})], size=9.5, space=4)
    panel(s, Inches(7.05), Inches(5.55), Inches(5.7), Inches(0.95))
    text(s, Inches(7.22), Inches(5.64), Inches(5.35), Inches(0.8),
         [('不变量 I4：E(c) 的端点都属于 members(c) 或其后代；边不引入新对象，只增加邻接。',
           {'size': 9.5})], size=9.5)

    # ---------------------------------------------------------------- 11
    s = blank(prs)
    header(s, '步骤四：逆拓扑序推进', logo, 11,
           'S_sub(c) 依赖所有子属性的 S_sub，因此必须按逆拓扑序求解')
    panels = [('① 叶子概念先完成', '先处理无子属性或子属性已完成的节点：局部图与两类代表就绪'),
              ('② 父概念聚合子代表', '父的候选池并入子 S_sub；生成组间边，得到父的 S_sub'),
              ('③ 逐层向上直到根', '根完成后，全部概念的 S_sub 与边均已生成，进入落盘')]
    for i, (t, d) in enumerate(panels):
        x = Inches(0.62 + i * 4.12)
        panel(s, x, Inches(1.42), Inches(3.9), Inches(2.35), fill=WHITE, border=RULE)
        text(s, x + Inches(0.16), Inches(1.52), Inches(3.55), Inches(0.24), [t], size=10, bold=True, color=BLUE)
        text(s, x + Inches(0.16), Inches(1.86), Inches(3.55), Inches(0.7), [d], size=9, color=DARK)
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
            arrow(s, x + Inches(3.95), Inches(2.6), x + Inches(4.1), Inches(2.6))
    caption(s, Inches(0.62), Inches(4.08), Inches(12.0), '图 6', '同一批对象在不同层被接入的方式')
    rule(s, Inches(0.62), Inches(4.38), Inches(12.1))
    text(s, Inches(0.62), Inches(4.5), Inches(12.1), Inches(2.4),
         [('顺序为什么是这条链，而不是任意顺序', {'size': 10.5, 'bold': True, 'color': BLUE}),
          ('S_sub(c) 的定义里含 ⋃ S_sub(child)，所以父必须在所有子之后求解；这正是逆拓扑序。', {'size': 9.5}),
          ('若先用正序处理父属性，父的候选池会缺子代表，导致递归入口无法从父进入子树。', {'size': 9.5}),
          ('工作量：每个概念只遍历“直属对象 + 直接子属性的代表”，总距离计算与 '
           'Σ_c (|members(c)| + |children(c)|·p) 同阶，与子树大小无关。', {'size': 9.5}),
          ('共享：多父概念的 S_sub 只算一次，被多个父按引用复用，因此同一对象不会重复建边。', {'size': 9.5}),
          ('实测：768 个非空概念、103 个多父概念；整图构建 8.0–8.2 s，单线程。', {'size': 9.5, 'color': GRAY})],
         size=9.5, space=4)

    # ---------------------------------------------------------------- 12
    s = blank(prs)
    header(s, '步骤五：收尾与落盘', logo, 12,
           '最后一步把“边”变成可查的两种结构，并把所有关系固定为静态数据')
    text(s, Inches(0.62), Inches(1.4), Inches(6.1), Inches(0.24),
         [('邻接：row(u) = sort-dedup(L0(u) ∪ rev(L0(u)) ∪ E(u))', {'size': 9.5, 'bold': True, 'color': BLUE})],
         size=9.5)
    box_y = Inches(1.72)
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
    text(s, Inches(0.62), Inches(2.12), Inches(6.1), Inches(0.24),
         [('橙：本地 level-0 │ 蓝：桥边 │ 绿：快捷边（示意，实际长度不一）', {'size': 8.5, 'color': GRAY})], size=8.5)
    caption(s, Inches(0.62), Inches(2.55), Inches(6.1), '图 7', '单行邻接的组成；全库行度中位 26')
    text(s, Inches(0.62), Inches(3.0), Inches(6.1), Inches(0.24), ['入口表：两张倒排数组'], size=9.5,
         bold=True, color=BLUE)
    for i, (name, desc) in enumerate([('S_dir[c]', '该概念直属对象的代表，非递归检索入口'),
                                      ('S_sub[c]', '该概念子树的代表，递归检索入口')]):
        y = Inches(3.28 + i * 0.58)
        panel(s, Inches(0.62), y, Inches(6.1), Inches(0.5), fill=WHITE, border=RULE)
        text(s, Inches(0.76), y + Inches(0.06), Inches(1.5), Inches(0.24), [name], size=9,
             bold=True, color=BLUE, latin=MONO)
        text(s, Inches(2.3), y + Inches(0.06), Inches(4.3), Inches(0.24), [desc], size=9)
    text(s, Inches(0.62), Inches(4.55), Inches(6.1), Inches(0.9),
         [('查询只读这两类结构；局部图仅用于后续增删对象时的维护。', {'size': 9})], size=9)
    chain = [('前置', '所有概念的 S_sub 与 E 已生成'),
             ('操作', '对称化去重 → 行内按 ID 排序 → 写 offsets；入口表按概念直取'),
             ('后置', 'row(u) 与入口表为只读数据；查询不再触碰构建态结构')]
    for i, (tag, desc) in enumerate(chain):
        y = Inches(1.42 + i * 0.66)
        text(s, Inches(7.05), y, Inches(0.8), Inches(0.24), [tag], size=9.5, bold=True, color=BLUE)
        text(s, Inches(7.9), y, Inches(4.85), Inches(0.56), [desc], size=9)
    rule(s, Inches(7.05), Inches(3.4), Inches(5.7))
    text(s, Inches(7.05), Inches(3.52), Inches(5.7), Inches(1.2),
         [('存储与规模', {'size': 10, 'bold': True, 'color': BLUE}),
          ('邻接 5,697,634 × 4 B = 21.7 MB；offsets 168,938 × 8 B = 1.35 MB', {'size': 9}),
          ('入口 6,589 × 4 B ≈ 26 KB；不含向量本体与局部图结构', {'size': 9}),
          ('行度：中位 26 / p90 74 / p99 98 / 最大 193', {'size': 9})], size=9, space=4)
    panel(s, Inches(7.05), Inches(4.8), Inches(5.7), Inches(1.5))
    text(s, Inches(7.22), Inches(4.9), Inches(5.35), Inches(1.3),
         [('不变量 I5：每行邻居对称且无重复；I6：每个对象至少出现在 own 行内；'
           'I7：每个非空概念的入口数 ≤ 8。', {'size': 9.5}),
          ('违反 I5/I6 会直接表现为漏答或重复计分；违反 I7 只影响入口质量，不影响正确性。',
           {'size': 9})], size=9.5, space=4)

    # ---------------------------------------------------------------- 13
    s = blank(prs)
    header(s, '构建不变量与检验', logo, 13,
           '每一步的后置条件合成七条不变量；构建程序在写盘前逐条断言')
    cols = [Inches(0.62), Inches(1.15), Inches(6.4), Inches(5.1)]
    heads = [('编号', Inches(0.68), Inches(0.45)), ('不变量', Inches(1.15), Inches(5.15)),
             ('检验方式 / 违背后果', Inches(6.45), Inches(5.45))]
    for hd, hx, hw in heads:
        text(s, hx, Inches(1.44), hw, Inches(0.24), [hd], size=9.5, bold=True, color=BLUE)
    rule(s, Inches(0.62), Inches(1.7), Inches(12.1), color=BLUE)
    rows = [('I1', '每非空概念恰一张局部图，对象集合 = members(c)', '构图后逐概念比对集合大小；否则对象无图可查'),
            ('I2', 'S_dir(c) ⊆ members(c)，且都是真实 item', '选点后校验来源；否则入口即被掩码拒绝'),
            ('I3', 'S_sub(c) 的元素属 c 或其后代', '沿 DAG 校验归属；否则递归入口指向范围外'),
            ('I4', 'E(c) 端点属 members(c) 或其后代，无新对象', '生成边时校验端点；否则引入非法对象'),
            ('I5', '每行邻居对称、无重复、按 ID 升序', '落盘前排序去重并比对反向；否则漏答或重复计分'),
            ('I6', '每个对象至少出现在自身行内', '统计行覆盖；否则对象不可达'),
            ('I7', '每非空概念的入口 ≤ 8（S_dir 与 S_sub 各自）', '落盘前计数；否则入口膨胀但结果仍正确')]
    for r, row in enumerate(rows):
        y = Inches(1.8 + r * 0.52)
        rule(s, Inches(0.62), y, Inches(12.1))
        text(s, Inches(0.68), y + Inches(0.06), Inches(0.45), Inches(0.24), [row[0]], size=9.5, bold=True, color=ORANGE)
        text(s, Inches(1.15), y + Inches(0.06), Inches(5.15), Inches(0.42), [row[1]], size=9)
        text(s, Inches(6.45), y + Inches(0.06), Inches(5.45), Inches(0.42), [row[2]], size=9, color=GRAY)
    text(s, Inches(0.62), Inches(6.05), Inches(12.1), Inches(0.9),
         [('I1–I4 是构建阶段的结构约束，I5–I7 是落盘阶段的数据约束；两者都只保证“结构与语义一致”，'
           '不保证过滤后的子图可导航——后者是经验指标，由召回实验衡量。', {'size': 9.5})], size=9.5)

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
