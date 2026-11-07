#!/usr/bin/env python3
"""Make the ASI SVGs independent of viewer fonts and text rendering.

Requires cairocffi, tinycss2, system Pango/Cairo and Noto Sans CJK SC at
*generation* time only. Pango shapes the entire label, including fallback
fonts for mathematical subscripts, before Cairo exports vector outlines.

The original editable diagram SVG is retained in metadata. Extract it with
`python svg_labels.py diagram.svg --extract-source`, edit the extracted SVG,
then run this helper on that file. No bitmap or web font is embedded.
"""
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET

SVG = 'http://www.w3.org/2000/svg'
SOURCE_ID = 'editable-svg-source'
ET.register_namespace('', SVG)
ET.register_namespace('xlink', 'http://www.w3.org/1999/xlink')


def _parse(source):
    return ET.fromstring(source, parser=ET.XMLParser(target=ET.TreeBuilder(insert_comments=True)))


def _rules(root):
    import tinycss2
    rules = []
    for style in root.iter(f'{{{SVG}}}style'):
        for rule in tinycss2.parse_stylesheet(style.text or '', skip_comments=True, skip_whitespace=True):
            if rule.type != 'qualified-rule':
                raise ValueError('Only ordinary SVG CSS rules are supported')
            props = {d.lower_name: tinycss2.serialize(d.value).strip()
                     for d in tinycss2.parse_declaration_list(rule.content, skip_comments=True, skip_whitespace=True)
                     if d.type == 'declaration'}
            for selector in tinycss2.serialize(rule.prelude).strip().split(','):
                selector = selector.strip()
                if selector != '*' and not selector.replace('-', '').isalnum() and not (
                        selector.startswith('.') and selector[1:].replace('-', '').isalnum()):
                    raise ValueError(f'Unsupported SVG CSS selector: {selector}')
                rules.append((selector, props))
    return rules


def _style(element, rules, inherited):
    import tinycss2
    result = dict(inherited)
    # Presentation attributes precede stylesheet rules in the CSS cascade.
    result.update(element.attrib)
    tag = element.tag.rsplit('}', 1)[-1]
    classes = element.get('class', '').split()
    for selector, props in sorted(rules, key=lambda r: 2 if r[0].startswith('.') else (0 if r[0] == '*' else 1)):
        if selector in ('*', tag) or selector.startswith('.') and selector[1:] in classes:
            result.update(props)
    result.update({d.lower_name: tinycss2.serialize(d.value).strip()
                   for d in tinycss2.parse_declaration_list(element.get('style', ''), skip_comments=True, skip_whitespace=True)
                   if d.type == 'declaration'})
    return result


class _Outliner:
    def __init__(self):
        import cairocffi as cairo
        from cffi import FFI
        self.cairo = cairo
        self.ffi = FFI()
        self.ffi.cdef('''
            typedef struct _PangoLayout PangoLayout;
            typedef struct _PangoContext PangoContext;
            typedef struct _PangoFontDescription PangoFontDescription;
            typedef struct _cairo cairo_t;
            PangoLayout *pango_cairo_create_layout(cairo_t *cr);
            void pango_cairo_layout_path(cairo_t *cr, PangoLayout *layout);
            PangoFontDescription *pango_font_description_from_string(const char *str);
            void pango_font_description_set_absolute_size(PangoFontDescription *, double);
            void pango_font_description_free(PangoFontDescription *);
            void pango_layout_set_font_description(PangoLayout *, const PangoFontDescription *);
            void pango_layout_set_text(PangoLayout *, const char *, int);
            void pango_layout_get_size(PangoLayout *, int *, int *);
            int pango_layout_get_baseline(PangoLayout *);
            int pango_layout_get_unknown_glyphs_count(PangoLayout *);
            PangoContext *pango_layout_get_context(PangoLayout *);
            void pango_context_set_round_glyph_positions(PangoContext *, int);
            void g_object_unref(void *);
        ''')
        self.pango = self.ffi.dlopen('libpango-1.0.so.0')
        self.pc = self.ffi.dlopen('libpangocairo-1.0.so.0')
        self.gobject = self.ffi.dlopen('libgobject-2.0.so.0')
        self.context = cairo.Context(cairo.RecordingSurface(cairo.CONTENT_COLOR_ALPHA, None))
        self.pointer = self.ffi.cast('cairo_t *', int(cairo.ffi.cast('uintptr_t', self.context._pointer)))

    def path(self, text, style):
        family = style.get('font-family', 'sans-serif').replace("'", '').replace('"', '')
        weight = style.get('font-weight', 'normal')
        font = self.pango.pango_font_description_from_string(
            (family + (' Bold' if weight in ('bold', '700') else '')).encode())
        layout = self.pc.pango_cairo_create_layout(self.pointer)
        try:
            self.pango.pango_context_set_round_glyph_positions(self.pango.pango_layout_get_context(layout), 0)
            self.pango.pango_font_description_set_absolute_size(font, float(style.get('font-size', '16').removesuffix('px')) * 1024)
            self.pango.pango_layout_set_font_description(layout, font)
            self.pango.pango_layout_set_text(layout, text.encode(), -1)
            if self.pango.pango_layout_get_unknown_glyphs_count(layout):
                raise ValueError(f'Missing font glyphs in SVG label: {text}')
            width = self.ffi.new('int *')
            self.pango.pango_layout_get_size(layout, width, self.ffi.NULL)
            offset = {'start': 0, 'middle': .5, 'end': 1}[style.get('text-anchor', 'start')]
            x = float(style.get('x', '0')) - offset * width[0] / 1024
            y = float(style.get('y', '0')) - self.pango.pango_layout_get_baseline(layout) / 1024
            self.context.new_path()
            self.context.move_to(x, y)
            self.pc.pango_cairo_layout_path(self.pointer, layout)
            commands = {self.cairo.PATH_MOVE_TO: 'M', self.cairo.PATH_LINE_TO: 'L',
                        self.cairo.PATH_CURVE_TO: 'C', self.cairo.PATH_CLOSE_PATH: 'Z'}
            return ' '.join(commands[op] + ' '.join(f'{v:.6f}'.rstrip('0').rstrip('.') for v in xy)
                            for op, xy in self.context.copy_path())
        finally:
            self.gobject.g_object_unref(layout)
            self.pango.pango_font_description_free(font)


def self_contained_svg(path, title=None, description=None):
    """Outline live labels, retain editable source, and expose SVG accessibility."""
    path = Path(path)
    source = path.read_text()
    root = _parse(source)
    texts = list(root.iter(f'{{{SVG}}}text'))
    if texts:
        if root.find(f'{{{SVG}}}metadata[@id="{SOURCE_ID}"]') is not None:
            raise ValueError('Edit the extracted source, not an outlined SVG')
        rules = _rules(root)
        outliner = _Outliner()

        def convert(parent, inherited):
            for element in list(parent):
                if not isinstance(element.tag, str):
                    continue
                style = _style(element, rules, inherited)
                if element.tag == f'{{{SVG}}}text':
                    if len(element):
                        raise ValueError('Nested SVG text is not supported')
                    text = element.text or ''
                    element.tag = f'{{{SVG}}}path'
                    element.attrib.clear()
                    element.set('d', outliner.path(text, style))
                    element.set('fill', style.get('fill', '#000'))
                    if 'transform' in style:
                        element.set('transform', style['transform'])
                    element.set('aria-label', text)
                    element.text = None
                else:
                    convert(element, {k: v for k, v in style.items() if k.startswith('font-') or k in ('fill', 'text-anchor')})
        convert(root, {})
        metadata = ET.SubElement(root, f'{{{SVG}}}metadata', {'id': SOURCE_ID, 'data-format': 'image/svg+xml'})
        metadata.text = source
    for tag, value in [('title', title), ('desc', description)]:
        element = root.find(f'.//{{{SVG}}}{tag}')
        if element is None:
            if not value:
                raise ValueError(f'{path}: missing accessible {tag}')
            element = ET.Element(f'{{{SVG}}}{tag}')
            root.insert(0, element)
        elif element not in list(root):
            next(parent for parent in root.iter() if element in list(parent)).remove(element)
            root.insert(0, element)
        element.set('id', tag)
        if value:
            element.text = value
    root.set('role', 'img')
    root.set('aria-labelledby', 'title desc')
    path.write_text(ET.tostring(root, encoding='unicode') + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('svg', type=Path, nargs='+')
    parser.add_argument('--extract-source', action='store_true', help='print the editable source of one outlined diagram')
    args = parser.parse_args()
    if args.extract_source:
        if len(args.svg) != 1:
            parser.error('--extract-source requires exactly one SVG')
        node = _parse(args.svg[0].read_text()).find(f'{{{SVG}}}metadata[@id="{SOURCE_ID}"]')
        if node is None:
            parser.error('SVG has no retained editable source')
        print(node.text, end='')
    else:
        for svg in args.svg:
            self_contained_svg(svg)
