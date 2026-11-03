#!/usr/bin/env python3
"""Geometry checks that do not need a renderer.

Estimates wrapped text height per text box (CJK width ~1.0em, ASCII ~0.55em),
flags text that would not fit its own box, boxes leaving the slide, and pairs
of *text* boxes that overlap each other.
"""
import argparse
import math
from itertools import combinations
from pathlib import Path

from pptx import Presentation

EMU_IN = 914400
CJK_BYTES = 1.0
ASCII = 0.55


def char_width(ch):
    return CJK_BYTES if ord(ch) > 0x2E80 else ASCII


def line_count(text, box_w_in, size_pt):
    if not text:
        return 1
    usable = box_w_in * 72.0
    total = 0
    for para in text.split('\n'):
        width = sum(char_width(c) for c in para) * size_pt
        total += max(1, math.ceil(width / usable))
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--deck', type=Path, required=True)
    a = ap.parse_args()
    prs = Presentation(str(a.deck))
    W, H = prs.slide_width/EMU_IN, prs.slide_height/EMU_IN
    problems, checked = [], 0
    for idx, slide in enumerate(prs.slides, 1):
        texts = []
        for sh in slide.shapes:
            if sh.left is None:
                continue
            x, y = sh.left/EMU_IN, sh.top/EMU_IN
            w, h = sh.width/EMU_IN, sh.height/EMU_IN
            if x < -0.01 or y < -0.01 or x + w > W + 0.01 or y + h > H + 0.01:
                problems.append(f'slide {idx}: {sh.name} outside slide '
                                f'({x:.2f},{y:.2f},{w:.2f}x{h:.2f})')
            if sh.shape_type == 13 and (x + w > W + 0.01 or y + h > H + 0.01):
                problems.append(f'slide {idx}: picture {sh.name} exceeds slide '
                                f'(bottom {y+h:.2f} in / right {x+w:.2f} in)')
            if not sh.has_text_frame:
                continue
            body = sh.text_frame.text
            if not body.strip():
                continue
            checked += 1
            sizes = [r.font.size.pt for p in sh.text_frame.paragraphs for r in p.runs if r.font.size]
            size = max(sizes) if sizes else 18.0
            gaps = sum((p.space_after.pt if p.space_after else 0) for p in sh.text_frame.paragraphs)
            lines = line_count(body, max(w, 0.05), size)
            needed = lines*size*1.28 + gaps
            if needed > h*72.0 + 3.0:
                problems.append(f'slide {idx}: {sh.name} text needs {needed:.0f}pt, box {h*72.0:.0f}pt '
                                f'[{body[:28].replace(chr(10), " ")}...]')
            texts.append((sh.name, x, y, w, h, body[:20].replace('\n', ' ')))
        for (n1, x1, y1, w1, h1, t1), (n2, x2, y2, w2, h2, t2) in combinations(texts, 2):
            ox = min(x1+w1, x2+w2) - max(x1, x2)
            oy = min(y1+h1, y2+h2) - max(y1, y2)
            if ox > 0.02 and oy > 0.02:
                problems.append(f'slide {idx}: text boxes overlap {n1}[{t1}] / {n2}[{t2}] '
                                f'({ox:.2f}x{oy:.2f} in)')
    print(f'text_boxes_checked={checked} problems={len(problems)}')
    for p in problems:
        print(' -', p)


if __name__ == '__main__':
    main()
