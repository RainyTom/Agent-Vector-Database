"""CCS 2012 ontology parsing shared by the structure measurement and workload export.

The flat ACM dump nests concepts as <ul>/<li>; a concept with several parents
appears once per parent branch, so parents must be collected as a set.
"""
import csv
from collections import defaultdict
from html.parser import HTMLParser


class OntologyParser(HTMLParser):
    """Rebuild the concept tree/DAG from the nested <ul>/<li> markup."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.nodes = []          # (concept_id, name, parent_id or None)
        self.ul_owner = []       # stack: owning node of the current <ul>
        self.pending = []        # stack of the current <li> record
        self.capture = None

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if tag == 'ul':
            parent = self.pending[-1] if self.pending else None
            self.ul_owner.append(parent)
        elif tag == 'li':
            record = {'id': None, 'name': None,
                      'parent': (self.ul_owner[-1]['id'] if self.ul_owner and self.ul_owner[-1] else None)}
            self.pending.append(record)
        elif tag == 'a':
            if attributes.get('class') == 'boxedlink' and attributes.get('name'):
                if self.pending and self.pending[-1]['id'] is None:
                    self.pending[-1]['id'] = attributes['name']
            if attributes.get('title') == 'Assign This CCS Concept':
                self.capture = []

    def handle_data(self, data):
        if self.capture is not None:
            self.capture.append(data)

    def handle_endtag(self, tag):
        if tag == 'a' and self.capture is not None:
            if self.pending and self.pending[-1]['name'] is None:
                self.pending[-1]['name'] = ''.join(self.capture).strip()
            self.capture = None
        elif tag == 'li' and self.pending:
            record = self.pending.pop()
            if record['id']:
                self.nodes.append((record['id'], record['name'], record['parent']))
        elif tag == 'ul' and self.ul_owner:
            self.ul_owner.pop()


def load_ontology(path):
    parser = OntologyParser()
    parser.feed(path.read_text(encoding='utf-8', errors='replace'))
    names, parents, children = {}, defaultdict(set), defaultdict(set)
    for concept_id, name, parent in parser.nodes:
        names.setdefault(concept_id, name)
        if parent:
            parents[concept_id].add(parent)
            children[parent].add(concept_id)
    roots = [c for c in names if not parents.get(c)]
    return names, parents, children, roots, len(parser.nodes)


def closure(start, neighbours):
    seen, frontier = set(), [start]
    while frontier:
        node = frontier.pop()
        if node in seen:
            continue
        seen.add(node)
        frontier.extend(neighbours.get(node, ()))
    return seen


def resolve_path(children, names, parts):
    """Walk a 'CCS->A->B' path from the synthetic root; return the terminal id."""
    current = 'CCS'
    for label in parts:
        match = [c for c in children.get(current, ()) if names.get(c) == label]
        if len(match) != 1:
            return None
        current = match[0]
    return current


