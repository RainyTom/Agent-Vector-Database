#!/usr/bin/env python3
"""Build the 10-slide project deck. Shapes only, so text stays editable."""
import argparse
from pathlib import Path

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.util import Emu, Inches, Pt

BLUE = RGBColor(0x1F, 0x3F, 0x86)
LIGHT = RGBColor(0xEC, 0xF1, 0xFA)
ORANGE = RGBColor(0xC5, 0x5A, 0x11)
GRAY = RGBColor(0x5A, 0x5F, 0x6A)
DARK = RGBColor(0x22, 0x26, 0x2E)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)
FONT = 'Microsoft YaHei'

W, H = Inches(13.333), Inches(7.5)


def style(run, size, bold=False, color=DARK):
    f = run.font
    f.size = Pt(size)
    f.bold = bold
    f.color.rgb = color
    f.name = FONT
    rPr = run._r.get_or_add_rPr()
    for tag in ('a:ea', 'a:cs'):
        el = rPr.find('{http://schemas.openxmlformats.org/drawingml/2006/main}' + tag.split(':')[1])
        if el is None:
            el = rPr.makeelement('{http://schemas.openxmlformats.org/drawingml/2006/main}' + tag.split(':')[1], {})
            rPr.append(el)
        el.set('typeface', FONT)


def text(slide, x, y, w, h, lines, size=16, bold=False, color=DARK, align=PP_ALIGN.LEFT,
         anchor=MSO_ANCHOR.TOP, space=6):
    box = slide.shapes.add_textbox(x, y, w, h)
    tf = box.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = anchor
    tf.margin_left = tf.margin_right = Emu(0)
    tf.margin_top = tf.margin_bottom = Emu(0)
    for i, item in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        p.space_after = Pt(space)
        if isinstance(item, tuple):
            content, opts = item
        else:
            content, opts = item, {}
        run = p.add_run()
        run.text = content
        style(run, opts.get('size', size), opts.get('bold', bold), opts.get('color', color))
    return box


def box(slide, x, y, w, h, fill=LIGHT, line=BLUE, radius=0.06, weight=1.25):
    s = slide.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE, x, y, w, h)
    s.adjustments[0] = radius
    s.fill.solid()
    s.fill.fore_color.rgb = fill
    s.line.color.rgb = line
    s.line.width = Pt(weight)
    s.shadow.inherit = False
    return s


def chip(slide, x, y, w, h, label, fill=BLUE, color=WHITE, size=12):
    s = box(slide, x, y, w, h, fill=fill, line=fill, radius=0.25, weight=0)
    tf = s.text_frame
    tf.word_wrap = True
    tf.margin_left = tf.margin_right = Emu(0)
    tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.CENTER
    style(p.add_run(), size, True, color)
    p.runs[0].text = label
    return s


def arrow(slide, x1, y1, x2, y2, color=BLUE, width=1.75):
    line = slide.shapes.add_connector(1, x1, y1, x2, y2)
    line.line.color.rgb = color
    line.line.width = Pt(width)
    head = line.line._get_or_add_ln().makeelement(
        '{http://schemas.openxmlformats.org/drawingml/2006/main}tailEnd', {})
    head.set('type', 'triangle')
    line.line._get_or_add_ln().append(head)
    return line


def page_head(slide, title, kicker=None, number=None, logo=None):
    bar = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(0), W, Inches(0.12))
    bar.fill.solid()
    bar.fill.fore_color.rgb = BLUE
    bar.line.fill.background()
    text(slide, Inches(0.6), Inches(0.34), Inches(10.6), Inches(0.62), [title], size=27, bold=True, color=BLUE)
    if kicker:
        text(slide, Inches(0.62), Inches(0.98), Inches(11.4), Inches(0.34), [kicker], size=13, color=GRAY)
    if logo:
        slide.shapes.add_picture(str(logo), Inches(11.62), Inches(0.26), height=Inches(0.72))
    if number is not None:
        text(slide, Inches(0.6), Inches(7.02), Inches(2.0), Inches(0.3), [f'{number} / 10'], size=10, color=GRAY)


def blank(prs):
    return prs.slides.add_slide(prs.slide_layouts[6])


def build(logo, out):
    prs = Presentation()
    prs.slide_width, prs.slide_height = W, H

    # ---------------- 1 cover
    s = blank(prs)
    band = s.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(0), W, Inches(2.55))
    band.fill.solid()
    band.fill.fore_color.rgb = BLUE
    band.line.fill.background()
    plate = box(s, Inches(0.78), Inches(0.55), Inches(1.5), Inches(1.5), fill=WHITE, line=WHITE, radius=0.12, weight=0)
    s.shapes.add_picture(str(logo), Inches(0.87), Inches(0.64), height=Inches(1.32))
    text(s, Inches(2.55), Inches(0.78), Inches(7.4), Inches(0.5),
         ['南京邮电大学  ·  计算机学院'], size=15, color=WHITE)
    text(s, Inches(2.55), Inches(1.32), Inches(9.2), Inches(0.9),
         ['层级属性约束下的向量检索'], size=38, bold=True, color=WHITE)
    text(s, Inches(0.85), Inches(3.05), Inches(11.6), Inches(1.0),
         ['从属性 DAG 到一张共享 item 图：索引建立与 Direct / Recursive 两类查询'],
         size=21, bold=True, color=BLUE)
    text(s, Inches(0.85), Inches(4.05), Inches(11.4), Inches(1.5),
         ['研究方案汇报', ('面向目录 / 分类体系等层级元数据的高维向量近邻检索', {'size': 15, 'color': GRAY}),
          ('数据：SIFT-128d × 1,929 属性 DAG；索引：属性内 HNSW + DAG 组合单图', {'size': 15, 'color': GRAY})],
         size=18, color=DARK)
    text(s, Inches(0.85), Inches(6.35), Inches(11.4), Inches(0.8),
         [('2026-11-03   ·   研究进展汇报   ·   结果均为本机实测，未复现他人实现', {'size': 12, 'color': GRAY})],
         size=12)

    # ---------------- 2 background
    s = blank(prs)
    page_head(s, '研究背景：属性和向量一起查', '目录、分类体系、标签体系都不是平铺的，而是有父子关系的图', 2, logo)
    cards = [
        ('① 属性是有向无环图', ['同一对象可属多个父类', '查询常按“某类及其子类”', '1,929 属性 / 2,040 边 / 103 多父']),
        ('② 两种作用域语义', ['Direct：只命中直属该属性', 'Recursive：含全部后代闭包', '两者结果集不同，不能混用']),
        ('③ 数据与属性都在变', ['新对象持续入库', '对象改归属、目录结构调整', '不能靠“定期全量重建”']),
    ]
    for i, (title, lines) in enumerate(cards):
        x = Inches(0.6 + i * 4.12)
        box(s, x, Inches(1.55), Inches(3.86), Inches(2.1))
        text(s, x + Inches(0.24), Inches(1.72), Inches(3.4), Inches(0.42), [title], size=16, bold=True, color=BLUE)
        text(s, x + Inches(0.24), Inches(2.2), Inches(3.4), Inches(1.5),
             [(line, {'size': 13, 'color': DARK}) for line in lines], size=13, space=8)
    text(s, Inches(0.6), Inches(3.95), Inches(12.1), Inches(0.4),
         ['一个真实查询长什么样'], size=15, bold=True, color=ORANGE)
    box(s, Inches(0.6), Inches(4.4), Inches(12.12), Inches(1.2), fill=RGBColor(0xFA, 0xF3, 0xEC), line=ORANGE)
    text(s, Inches(0.85), Inches(4.56), Inches(11.6), Inches(0.95),
         [('SELECT top-k  WHERE owner(x) ∈ A 或  owner(x) ∈ Desc*(A)', {'size': 16, 'bold': True}),
          ('同时要求：结果最近、必须合格、只扫一部分图 —— 过滤不能退化成“把候选扫描一遍”',
           {'size': 13, 'color': GRAY})], size=14, space=8)

    # ---------------- 3 motivation
    s = blank(prs)
    page_head(s, '研究动机：三条路都有硬伤', '我们先把三条常见路线跑在同一个负载上，再决定做什么', 3, logo)
    rows = [
        ('逐属性局部图分别搜', '每次查询要对 k 个局部图各搜一遍再合并',
         'Direct 单属性时反而更慢；查询代价随覆盖属性数线性增长'),
        ('一张全局图 + 过滤', '图不感知属性，合格点稀疏时诱导子图断裂',
         '要么算大量不合格距离，要么不可达；低选择率下召回塌陷'),
        ('边摘要 / 谓词索引', '把属性写进边或额外结构，更新要修补摘要',
         '改值、删除后旧信息残留，只能靠阈值 patch 或重建复位'),
    ]
    for i, (name, why, cost) in enumerate(rows):
        y = Inches(1.6 + i * 1.55)
        box(s, Inches(0.6), y, Inches(3.5), Inches(1.05))
        text(s, Inches(0.8), y + Inches(0.28), Inches(3.1), Inches(0.5), [name], size=15, bold=True, color=BLUE)
        text(s, Inches(4.35), y + Inches(0.1), Inches(3.85), Inches(1.15), [why], size=13, color=DARK)
        text(s, Inches(8.4), y + Inches(0.1), Inches(4.3), Inches(1.15), [cost], size=13, color=ORANGE)
    text(s, Inches(0.6), Inches(6.05), Inches(12.1), Inches(0.5),
         [('⇒ 我们的切入点：属性语义不写进边，只做一张几何共享图；查询时用掩码判合格',
           {'size': 15, 'bold': True, 'color': BLUE})], size=15)

    # ---------------- 4 problem
    s = blank(prs)
    page_head(s, '研究问题', '把“层级多属性 + 向量近邻”写成一个精确的问题，不引入额外近似', 4, logo)
    box(s, Inches(0.6), Inches(1.55), Inches(5.95), Inches(2.0))
    text(s, Inches(0.85), Inches(1.72), Inches(5.5), Inches(1.75),
         [('输入', {'size': 15, 'bold': True, 'color': BLUE}),
          ('向量集 V（FP32，无量化）、属性 DAG G=(C,E)', {'size': 13}),
          ('每个对象一个直属属性 owner(x)', {'size': 13}),
          ('查询 q=(v, A, mode, k)，mode ∈ {Direct, Recursive}', {'size': 13})], size=13, space=7)
    box(s, Inches(6.85), Inches(1.55), Inches(5.85), Inches(2.0), fill=RGBColor(0xFA, 0xF3, 0xEC), line=ORANGE)
    text(s, Inches(7.1), Inches(1.72), Inches(5.4), Inches(1.75),
         [('输出与合格集', {'size': 15, 'bold': True, 'color': ORANGE}),
          ('Direct：    S = { x | owner(x) ∈ A }', {'size': 13}),
          ('Recursive：S = { x | owner(x) ∈ ∪ Desc*(a) }', {'size': 13}),
          ('返回 S 中距 v 最近的 k 个（近似、单队列、合格优先）', {'size': 13})], size=13, space=7)
    text(s, Inches(0.6), Inches(3.68), Inches(12.1), Inches(0.42),
         ['要回答的三个子问题'], size=15, bold=True, color=BLUE)
    qs = [('Q1', '一个查询覆盖多个属性时，能否只搜一次、共享前沿？'),
          ('Q2', 'Direct 与 Recursive 能否复用同一张图，而不重建索引？'),
          ('Q3', '属性频繁变化时，更新要不要动图？代价多大？')]
    for i, (tag, q) in enumerate(qs):
        y = Inches(4.2 + i * 0.68)
        chip(s, Inches(0.62), y, Inches(0.62), Inches(0.42), tag, size=13)
        text(s, Inches(1.42), y + Inches(0.03), Inches(11.2), Inches(0.45), [q], size=14)
    text(s, Inches(0.6), Inches(6.58), Inches(12.1), Inches(0.34),
         [('约束：不改距离核（AVX FP32）、不做量化、不引入二次近似来源',
           {'size': 12, 'color': GRAY})], size=12)

    # ---------------- 5 pipeline
    s = blank(prs)
    page_head(s, '方案总览：一次构建，一张图，一种搜索', '属性层次只决定“谁合格”和“从哪进”，不改变图的几何含义', 5, logo)
    steps = [('① 属性内建图', '每个非空直属集合\n独立建 HNSW\nM=64 / efC=200'),
             ('② 选代表', 'farthest-first\n每属性 ≤ 8 个\n真实 item'),
             ('③ 沿 DAG 组合', '逆拓扑序\nMST + 近代表桥\n+ 分布式桥'),
             ('④ 一张 item 图', '本地边 ∪ 桥边\n平面共享邻接\n5,697,634 条有向边'),
             ('⑤ 两种查询', '合法入口\n单 frontier\n先资格后距离')]
    for i, (head, body) in enumerate(steps):
        x = Inches(0.55 + i * 2.48)
        box(s, x, Inches(1.62), Inches(2.16), Inches(1.95))
        text(s, x + Inches(0.14), Inches(1.74), Inches(1.9), Inches(0.4), [head], size=14, bold=True, color=BLUE)
        text(s, x + Inches(0.14), Inches(2.2), Inches(1.9), Inches(1.3),
             [(ln, {'size': 11, 'color': DARK}) for ln in body.split('\n')], size=11, space=4)
        if i < len(steps) - 1:
            arrow(s, x + Inches(2.2), Inches(2.6), x + Inches(2.42), Inches(2.6))
    box(s, Inches(0.55), Inches(3.9), Inches(12.2), Inches(1.15), fill=RGBColor(0xF4, 0xF7, 0xFD))
    text(s, Inches(0.8), Inches(4.05), Inches(11.7), Inches(0.95),
         [('与既有做法的关键差别', {'size': 14, 'bold': True, 'color': BLUE}),
          ('局部搜索次数从“每次查询 k 次”变成“始终 1 次”；属性不写进边，只在查询时算 allowed 掩码；'
           'Direct / Recursive 只换入口表，图不变。', {'size': 13, 'color': DARK})], size=13, space=6)
    text(s, Inches(0.55), Inches(5.35), Inches(12.2), Inches(1.0),
         [('诚实边界：局部图拼接、多入口共享搜索、先资格后距离都已有前序（Filtered-DiskANN 的 Stitched/OR 搜索、'
           'LSSG、UNG 等）；我们评的是“外部多父 DAG 驱动的那套组合规则”是否带来独立贡献 —— 这一点尚未证实。',
           {'size': 11, 'color': GRAY})], size=11)

    # ---------------- 6 index part 1
    s = blank(prs)
    page_head(s, '索引建立 ①：属性内 HNSW 与代表点', '这一步只建几何图，属性只用来分组', 6, logo)
    box(s, Inches(0.6), Inches(1.55), Inches(5.6), Inches(2.05))
    text(s, Inches(0.82), Inches(1.7), Inches(5.2), Inches(1.85),
         [('属性内建图', {'size': 15, 'bold': True, 'color': BLUE}),
          ('for each 非空直属集合 members[a]:', {'size': 12}),
          ('    HNSW(M=64, efC=200, seed=100+a)', {'size': 12}),
          ('    逐点 addPoint(v_i, i)', {'size': 12}),
          ('768 个非空局部图；空属性不建图', {'size': 12, 'color': GRAY})], size=12, space=6)
    box(s, Inches(6.5), Inches(1.55), Inches(6.2), Inches(2.05), fill=RGBColor(0xFA, 0xF3, 0xEC), line=ORANGE)
    text(s, Inches(6.72), Inches(1.7), Inches(5.8), Inches(1.85),
         [('两类入口（都是真实 item，不是质心）', {'size': 15, 'bold': True, 'color': ORANGE}),
          ('direct_portal[a]：从 members[a] 选 ≤8', {'size': 12}),
          ('portal[a]：从 members[a] ∪ portal[children] 选 ≤8', {'size': 12}),
          ('farthest-first：最远优先，保证代表分散', {'size': 12}),
          ('共 6,589 个入口条目；不压缩、不删除任何 item', {'size': 12, 'color': GRAY})], size=12, space=6)
    text(s, Inches(0.6), Inches(3.72), Inches(12.1), Inches(0.4), ['为什么这样选'], size=14, bold=True, color=BLUE)
    for i, (t, d) in enumerate([('入口要能跨区', '代表分散，才能在子图内多区域起步'),
                                ('入口必须合格', '直接取真实 item，资格判定与结果一致'),
                                ('更新只动成员', '入口是摘要，不参与距离，坏了可重算')]):
        x = Inches(0.6 + i * 4.12)
        box(s, x, Inches(4.2), Inches(3.86), Inches(1.15), fill=RGBColor(0xF4, 0xF7, 0xFD))
        text(s, x + Inches(0.2), Inches(4.36), Inches(3.45), Inches(0.35), [t], size=13, bold=True, color=BLUE)
        text(s, x + Inches(0.2), Inches(4.74), Inches(3.45), Inches(0.55), [d], size=11, color=DARK)
    text(s, Inches(0.6), Inches(5.72), Inches(12.1), Inches(0.5),
         [('代价：farthest-first 是 O(p·|候选|·d)，构建期一次付清；查询期不参与。',
           {'size': 11, 'color': GRAY})], size=11)

    # ---------------- 7 index part 2
    s = blank(prs)
    page_head(s, '索引建立 ②：沿属性 DAG 组合成一张图', '按逆拓扑序处理属性：先子后父，避免重复计算', 7, logo)
    box(s, Inches(0.6), Inches(1.5), Inches(6.05), Inches(3.95))
    text(s, Inches(0.84), Inches(1.64), Inches(5.6), Inches(0.4), ['四类边（默认保留全部本地边，不减边）'],
         size=14, bold=True, color=BLUE)
    items = [('本地边 4,019,094', '局部 HNSW 的 level-0 边，对称化 + 去重'),
             ('组间 MST 1,652,935', '每个属性的输入组当超点，组间最近代表连接'),
             ('近代表 / 分布式桥', '直属 item 连到其他组代表；反向桥每点上限 16'),
             ('代表快捷边 25,605', '属性直属代表 ↔ 子图代表，减少逐层绕行')]
    for i, (t, d) in enumerate(items):
        y = Inches(2.12 + i * 0.9)
        chip(s, Inches(0.84), y, Inches(2.3), Inches(0.42), t, size=11)
        text(s, Inches(3.3), y - Inches(0.02), Inches(3.15), Inches(0.8), [d], size=11, color=DARK)
    text(s, Inches(0.6), Inches(5.6), Inches(6.05), Inches(0.4),
         [('结果：5,697,634 条有向边，最大行度 193，构建 7.99–8.21 s',
           {'size': 11, 'color': GRAY})], size=11)
    # schematic
    cx = Inches(9.75)
    for i, (label, dx, dy) in enumerate([('根属性 R', 0.0, 0.0), ('子 A', -1.35, 0.95), ('子 B', 1.35, 0.95),
                                        ('孙 A1', -2.05, 1.95), ('孙 B1', 1.95, 1.95)]):
        x = Emu(int(cx + Inches(dx) - Inches(0.72)))
        y = Emu(int(Inches(1.75 + dy)))
        b = box(s, x, y, Inches(1.44), Inches(0.5), fill=WHITE if i else LIGHT, radius=0.2)
        tf = b.text_frame
        tf.vertical_anchor = MSO_ANCHOR.MIDDLE
        p = tf.paragraphs[0]
        p.alignment = PP_ALIGN.CENTER
        p.add_run().text = label
        style(p.runs[0], 11, True, BLUE)
    for a, b in (((0, 0.0, 0.0), (1, -1.35, 0.95)), ((0, 0.0, 0.0), (2, 1.35, 0.95)),
                 ((1, -1.35, 0.95), (3, -2.05, 1.95)), ((2, 1.35, 0.95), (4, 1.95, 1.95))):
        arrow(s, Emu(int(cx + Inches(a[1]))), Emu(int(Inches(2.27 + a[2]))),
              Emu(int(cx + Inches(b[1]))), Emu(int(Inches(1.73 + b[2]))), color=GRAY, width=1.25)
    text(s, Inches(8.15), Inches(4.38), Inches(4.6), Inches(1.15),
         [('每层只连“输入组之间”，不把子树整棵复制；',
           {'size': 11}),
          ('共享后代只算一次，多父属性不会重复展开。',
           {'size': 11}),
          ('这条规则是我们相对既有拼接方法的具体差异。',
           {'size': 11, 'color': ORANGE})], size=11, space=5)
    text(s, Inches(8.15), Inches(5.62), Inches(4.6), Inches(0.8),
         [('注意：桥是启发式。MST 保证连接，不保证任意合格子图可导航。',
           {'size': 11, 'color': GRAY})], size=11)

    # ---------------- 8 queries
    s = blank(prs)
    page_head(s, '两种查询：同一张图，不同入口与掩码', 'Direct / Recursive 只改“谁合格、从哪进”，不重建图', 8, logo)
    heads = ['', 'Direct', 'Recursive']
    rows = [('合格条件', 'owner(x) ∈ A', 'owner(x) ∈ ∪ Desc*(a)'),
            ('入口表', 'direct_portal[a]', 'portal[a]（去冗余祖先后）'),
            ('典型规模', '88/101 条查询只覆盖 1 个图', '最多 24 个非空图'),
            ('图是否重建', '否', '否')]
    x0, widths = Inches(0.6), [Inches(2.15), Inches(4.85), Inches(4.9)]
    x = x0
    for i, h in enumerate(heads):
        if h:
            chip(s, x + (Inches(0) if i == 0 else Inches(0)), Inches(1.5), widths[i], Inches(0.45),
                 h, fill=BLUE if i == 1 else ORANGE, size=13)
        x += widths[i]
    for r, (name, d, rc) in enumerate(rows):
        y = Inches(2.12 + r * 0.72)
        text(s, x0, y, widths[0], Inches(0.5), [name], size=12, bold=True, color=GRAY)
        text(s, x0 + widths[0], y, widths[1], Inches(0.6), [d], size=12)
        text(s, x0 + widths[0] + widths[1], y, widths[2], Inches(0.6), [rc], size=12)
    text(s, Inches(0.6), Inches(5.0), Inches(12.1), Inches(0.4), ['搜索流程（每查询一次）'],
         size=14, bold=True, color=BLUE)
    flow = ['读入口表\n多入口种子', 'allowed 掩码\n（查询时算）', '共享 frontier\n一个 best 堆', '先判资格\n合格才算距离', 'top-k\n返回']
    for i, f in enumerate(flow):
        x = Inches(0.6 + i * 2.45)
        box(s, x, Inches(5.45), Inches(2.05), Inches(1.05), fill=RGBColor(0xF4, 0xF7, 0xFD))
        tf = s.shapes[-1].text_frame
        tf.word_wrap = True
        tf.vertical_anchor = MSO_ANCHOR.MIDDLE
        for j, ln in enumerate(f.split('\n')):
            p = tf.paragraphs[0] if j == 0 else tf.add_paragraph()
            p.alignment = PP_ALIGN.CENTER
            p.add_run().text = ln
            style(p.runs[0], 11.5, j == 0, BLUE if j == 0 else DARK)
        if i < len(flow) - 1:
            arrow(s, x + Inches(2.09), Inches(5.97), x + Inches(2.4), Inches(5.97))
    text(s, Inches(0.6), Inches(6.62), Inches(12.1), Inches(0.4),
         [('当前实现是 b=0：不合格点不计距离、不入队、不导航。（b>0 的绕行跳转尚未实现）',
           {'size': 11, 'color': GRAY})], size=11)

    # ---------------- 9 results
    s = blank(prs)
    page_head(s, '实测现状：数字与它们的边界', 'SIFT-128d，168,937 项，属性随机绑定；单核，AVX FP32，无量化', 9, logo)
    kpis = [('7.99–8.21 s', '一次性构建'), ('5.70 M', '有向边'), ('6,589', '入口条目'),
            ('4.21', '每项祖先属性数')]
    for i, (v, l) in enumerate(kpis):
        x = Inches(0.6 + i * 3.13)
        box(s, x, Inches(1.5), Inches(2.92), Inches(1.05), fill=RGBColor(0xF4, 0xF7, 0xFD))
        text(s, x + Inches(0.18), Inches(1.6), Inches(2.6), Inches(0.42), [v], size=20, bold=True, color=BLUE)
        text(s, x + Inches(0.18), Inches(2.05), Inches(2.6), Inches(0.35), [l], size=11, color=GRAY)
    box(s, Inches(0.6), Inches(2.78), Inches(6.0), Inches(2.95))
    text(s, Inches(0.8), Inches(2.9), Inches(5.6), Inches(0.35), ['按覆盖子图数 G 看查询效率'], size=13, bold=True, color=BLUE)
    table = [('G', 'Direct 加速比', 'Recursive 加速比'),
             ('1', '0.79 ×（更慢）', '0.77 ×（更慢）'),
             ('2', '1.16 × / 1.12 ×', '1.11 × / 1.14 ×'),
             ('3', '2.49 × / 1.42 ×', '1.39 × / 1.15 ×')]
    for r, row in enumerate(table):
        y = Inches(3.3 + r * 0.5)
        for c, cell in enumerate(row):
            col = GRAY if r == 0 else (ORANGE if (r and c and float(cell.split(' ')[0]) < 1) else DARK)
            text(s, Inches(0.8 + c * 1.92), y, Inches(1.85), Inches(0.4),
                 [cell], size=11.5, bold=(r == 0), color=col)
    text(s, Inches(0.8), Inches(5.32), Inches(5.6), Inches(0.35),
         [('加速比 = NodeANN(n=k) 时延 / 我们；两格分别为 k10/ef32、k100/ef128',
           {'size': 10, 'color': GRAY})], size=10)
    box(s, Inches(6.85), Inches(2.78), Inches(5.85), Inches(2.95), fill=RGBColor(0xFA, 0xF3, 0xEC), line=ORANGE)
    text(s, Inches(7.05), Inches(2.9), Inches(5.45), Inches(2.7),
         [('必须一起读的三条边界', {'size': 13, 'bold': True, 'color': ORANGE}),
          ('1) G=1（单属性）我们更慢，优势只从 G≥2 出现', {'size': 11.5}),
          ('2) G≥3 的部分点召回下降（0.983 / 0.995），不能算同质量胜出', {'size': 11.5}),
          ('3) G=2 双方召回都为 1，但只有 7 / 11 条查询', {'size': 11.5}),
          ('样本是随机属性绑定，真实属性—向量相关性未验证', {'size': 11.5, 'color': GRAY})], size=11.5, space=7)
    text(s, Inches(0.6), Inches(5.95), Inches(12.1), Inches(0.35), ['距离与工作量不是瓶颈假设'],
         size=13, bold=True, color=BLUE)
    text(s, Inches(0.6), Inches(6.35), Inches(12.1), Inches(0.6),
         [('双方都用 AVX FP32；我们实际距离调用只少 6%/5%，单属性甚至近乎相同 —— 差距不在“少算距离”，'
           '而在访存与执行路径。', {'size': 11, 'color': DARK})], size=11)

    # ---------------- 10 limits
    s = blank(prs)
    page_head(s, '已做到的、还没做到的、下一步', '把结论收在能被检验的范围内', 10, logo)
    box(s, Inches(0.6), Inches(1.5), Inches(5.95), Inches(2.6))
    text(s, Inches(0.82), Inches(1.62), Inches(5.5), Inches(0.35), ['已实现并可复现'], size=14, bold=True, color=BLUE)
    text(s, Inches(0.82), Inches(2.05), Inches(5.5), Inches(1.9),
         [('• 属性内 HNSW + DAG 组合单图（5.70M 边）', {'size': 11.5}),
          ('• Direct / Recursive 两套入口，共享一次搜索', {'size': 11.5}),
          ('• 查询内拒绝缓存、堆/缓冲复用、合格向量预取', {'size': 11.5}),
          ('• 真实负载实测、逐查询结果与工作量交叉核验', {'size': 11.5})], size=11.5, space=6)
    box(s, Inches(6.85), Inches(1.5), Inches(5.85), Inches(2.6), fill=RGBColor(0xFA, 0xF3, 0xEC), line=ORANGE)
    text(s, Inches(7.07), Inches(1.62), Inches(5.4), Inches(0.35), ['尚未实现 / 未证实'], size=14, bold=True, color=ORANGE)
    text(s, Inches(7.07), Inches(2.05), Inches(5.4), Inches(1.9),
         [('• 更新入口：当前只有一次性构建，无 relabel/insert', {'size': 11.5}),
          ('• 全局分层、b>0 绕行、等字节剪枝', {'size': 11.5}),
          ('• 更新成本：单次换属主中位约 3.15×10⁵ 次距离计算', {'size': 11.5}),
          ('• 与 UNG / LSSG / EMA 的同质量、同字节对比', {'size': 11.5})], size=11.5, space=6)
    text(s, Inches(0.6), Inches(4.3), Inches(12.1), Inches(0.4), ['下一步（按优先级）'], size=14, bold=True, color=BLUE)
    nxt = [('①', '补齐 G=4/5 与高 G 召回', '先修漏答，再谈效率前沿'),
           ('②', '同入口/同邻接的执行路径对照', '把“表示差异”和“实现差异”分开'),
           ('③', '实现 relabel + churn 曲线', '含失效条件：何时增量不如重建')]
    for i, (tag, t, d) in enumerate(nxt):
        x = Inches(0.6 + i * 4.12)
        box(s, x, Inches(4.72), Inches(3.86), Inches(1.1), fill=RGBColor(0xF4, 0xF7, 0xFD))
        chip(s, x + Inches(0.18), Inches(4.86), Inches(0.45), Inches(0.38), tag, size=12)
        text(s, x + Inches(0.75), Inches(4.87), Inches(2.95), Inches(0.35), [t], size=12, bold=True, color=BLUE)
        text(s, x + Inches(0.75), Inches(5.25), Inches(2.95), Inches(0.5), [d], size=10.5, color=DARK)
    text(s, Inches(0.6), Inches(6.35), Inches(12.1), Inches(0.6),
         [('一句结论：单属性没有优势；多属性共享图有真实收益但样本有限；'
           '创新性与“频繁更新”都还没有跨过证据门槛。', {'size': 12, 'bold': True, 'color': BLUE})], size=12)

    out.parent.mkdir(parents=True, exist_ok=True)
    prs.save(str(out))
    return len(prs.slides._sldIdLst)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--logo', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    print('slides', build(a.logo, a.out), '->', a.out)
