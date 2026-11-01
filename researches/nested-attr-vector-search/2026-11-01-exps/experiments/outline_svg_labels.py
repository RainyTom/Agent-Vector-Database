#!/usr/bin/env python3
"""Export the two method illustrations with font-independent label outlines.

Requires fontTools and the OFL-licensed Noto Sans CJK SC Regular/Bold fonts.
Original text SVGs remain the editable sources. This supports their current
plain <text x/y> labels, not arbitrary SVG typography or a general SVG editor.
"""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.ttLib import TTFont

NS = 'http://www.w3.org/2000/svg'
ET.register_namespace('', NS)
FONT_BASE = 'https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/'
LICENSE_URL = FONT_BASE + 'LICENSE'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def number(value):
    return format(value, '.6f').rstrip('0').rstrip('.') or '0'


def convert(source, destination, fonts):
    tree = ET.parse(source)
    labels = 0
    paths = 0

    def visit(parent, inherited):
        nonlocal labels, paths
        style = dict(inherited)
        style.update(parent.attrib)
        for index, child in enumerate(list(parent)):
            if child.tag != '{' + NS + '}text':
                visit(child, style)
                continue
            if list(child) or any(key in child.attrib for key in ('dx', 'dy', 'transform', 'style', 'rotate', 'textLength')):
                raise ValueError('unsupported typography; update the exporter explicitly')
            current = dict(style)
            current.update(child.attrib)
            text = child.text or ''
            bold = current.get('font-weight', '') == 'bold'
            font = fonts[bold]
            glyphs = font.getGlyphSet()
            cmap = font.getBestCmap()
            upem = font['head'].unitsPerEm
            size = float(current.get('font-size', 15))
            scale = size / upem
            names = []
            for char in text:
                if ord(char) not in cmap:
                    raise ValueError('font lacks character ' + repr(char))
                names.append(cmap[ord(char)])
            advances = [font['hmtx'][name][0] for name in names]
            width = sum(advances) * scale
            x = float(child.attrib.get('x', '0'))
            y = float(child.attrib.get('y', '0'))
            anchor = current.get('text-anchor', 'start')
            if anchor == 'middle':
                x -= width / 2
            elif anchor == 'end':
                x -= width
            elif anchor != 'start':
                raise ValueError('unsupported anchor ' + anchor)
            replacement = ET.Element('{' + NS + '}g', {
                'aria-label': text,
                'fill': current.get('fill', 'currentColor'),
                'stroke': 'none',
                'transform': 'translate(' + number(x) + ' ' + number(y) + ') scale(' + number(scale) + ' ' + number(-scale) + ')',
            })
            cursor = 0
            for name, advance in zip(names, advances):
                pen = SVGPathPen(glyphs)
                glyphs[name].draw(pen)
                commands = pen.getCommands()
                if commands:
                    ET.SubElement(replacement, '{' + NS + '}path', {
                        'd': commands,
                        'transform': 'translate(' + str(cursor) + ' 0)',
                    })
                    paths += 1
                cursor += advance
            replacement.tail = child.tail
            parent.remove(child)
            parent.insert(index, replacement)
            labels += 1

    visit(tree.getroot(), {})
    metadata = ET.Element('{' + NS + '}metadata')
    metadata.text = 'Label outlines generated from Noto Sans CJK SC (SIL OFL 1.1). ' + LICENSE_URL + '; editable source: ' + source.name
    tree.getroot().insert(0, metadata)
    destination.parent.mkdir(parents=True, exist_ok=True)
    tree.write(destination, encoding='utf-8', xml_declaration=True)
    return {'source': str(source), 'source_sha256': digest(source),
            'output': str(destination), 'output_sha256': digest(destination),
            'label_groups': labels, 'glyph_paths': paths,
            'remaining_text_elements': len(tree.findall('.//{' + NS + '}text'))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', type=Path, required=True)
    parser.add_argument('--bold-font', type=Path, required=True)
    parser.add_argument('--out-dir', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('sources', type=Path, nargs='+')
    args = parser.parse_args()
    fonts = {False: TTFont(args.font), True: TTFont(args.bold_font)}
    try:
        results = [convert(source, args.out_dir / (source.stem + '-outlined.svg'), fonts)
                   for source in args.sources]
    finally:
        for font in fonts.values():
            font.close()
    report = {
        'purpose': 'SVG text compatibility: no runtime font or text shaping dependency',
        'font_license': LICENSE_URL,
        'fonts': [{'path': str(path), 'sha256': digest(path),
                   'source': FONT_BASE + 'OTF/SimplifiedChinese/' + path.name}
                  for path in (args.font, args.bold_font)],
        'figures': results,
        'tradeoff': 'Outline labels are not selectable text. Original SVG and Markdown remain editable/searchable.',
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(results, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
