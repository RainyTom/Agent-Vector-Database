"""Finite WQR specification checks, not a Python/C++ speed comparison.
Exercises exact bitplanes and actual cut/link order-statistics treap against
independent scalar predicates and parent walking. No vector distance kernel.
"""
import itertools
import json
import random
from dataclasses import dataclass
from pathlib import Path

U64 = (1 << 64) - 1
SIGN = 1 << 63


class Token:
    def __init__(self, key, priority):
        self.key, self.priority = key, priority
        self.left = self.right = self.parent = None
        self.size = 1


def size(t):
    return t.size if t else 0


def pull(t):
    t.size = 1 + size(t.left) + size(t.right)
    if t.left:
        t.left.parent = t
    if t.right:
        t.right.parent = t


def merge(a, b):
    if not a or not b:
        t = a or b
    elif a.priority < b.priority:
        a.right = merge(a.right, b)
        pull(a)
        t = a
    else:
        b.left = merge(a, b.left)
        pull(b)
        t = b
    if t:
        t.parent = None
    return t


def split(t, count):
    if not t:
        return None, None
    if size(t.left) >= count:
        a, t.left = split(t.left, count)
        pull(t)
        t.parent = None
        return a, t
    t.right, b = split(t.right, count - size(t.left) - 1)
    pull(t)
    t.parent = None
    return t, b


def rank(token):
    value = size(token.left)
    while token.parent:
        parent = token.parent
        if parent.right is token:
            value += 1 + size(parent.left)
        token = parent
    return value


class Directory:
    def __init__(self, parents):
        self.parents = dict(parents)
        self.tokens = {}
        self.root = None
        rng = random.Random(100)
        def visit(node):
            for kind in ('in', 'out'):
                if kind == 'out':
                    for child in sorted(n for n, p in self.parents.items() if p == node):
                        visit(child)
                token = Token((node, kind), rng.random())
                self.tokens[node, kind] = token
                self.root = merge(self.root, token)
        visit(0)

    def contains(self, scope, owner):
        return rank(self.tokens[scope, 'in']) <= rank(self.tokens[owner, 'in']) < rank(self.tokens[scope, 'out'])

    def scalar_contains(self, scope, owner):
        while owner is not None:
            if owner == scope:
                return True
            owner = self.parents[owner]
        return False

    def move(self, node, target):
        assert node != 0 and not self.contains(node, target)
        begin, end = rank(self.tokens[node, 'in']), rank(self.tokens[node, 'out']) + 1
        a, rest = split(self.root, begin)
        middle, b = split(rest, end - begin)
        self.root = merge(a, b)
        # Recompute target rank AFTER cut; token handles, not cached integers.
        position = rank(self.tokens[target, 'out'])
        a, b = split(self.root, position)
        self.root = merge(merge(a, middle), b)
        self.parents[node] = target

    def verify(self):
        assert size(self.root) == 2 * len(self.parents)
        for scope, owner in itertools.product(self.parents, repeat=2):
            assert self.contains(scope, owner) == self.scalar_contains(scope, owner)


@dataclass
class Item:
    owner: int
    category: int
    numeric: int
    tags: int
    alive: bool = True


class Block:
    def __init__(self, objects):
        self.objects = objects
        self.live = 0
        self.category = [0] * 64
        self.numeric = [0] * 64
        self.tags = [0] * 64
        for slot in range(len(objects)):
            self.put(slot)

    def put(self, slot):
        item, mask = self.objects[slot], 1 << slot
        self.live = (self.live | mask) if item.alive else (self.live & ~mask)
        values = ((self.category, item.category & U64),
                  (self.numeric, (item.numeric & U64) ^ SIGN),
                  (self.tags, item.tags))
        for planes, value in values:
            for bit in range(64):
                planes[bit] = (planes[bit] | mask) if item.alive and value & (1 << bit) else (planes[bit] & ~mask)

    def equal(self, planes, value):
        mask = self.live
        for bit in range(64):
            mask &= planes[bit] if value & (1 << bit) else ~planes[bit]
        return mask

    def less(self, encoded):
        equal, less = self.live, 0
        for bit in reversed(range(64)):
            if encoded & (1 << bit):
                less |= equal & ~self.numeric[bit]
                equal &= self.numeric[bit]
            else:
                equal &= ~self.numeric[bit]
        return less

    def mask(self, directory, scope, recursive, category, low, high, mode, tags, use_or):
        ordinary = []
        if category != -1:
            ordinary.append(self.equal(self.category, category & U64))
        if low != -1 or high != -1:
            lo, hi = ((low & U64) ^ SIGN), ((high & U64) ^ SIGN)
            le_hi = self.less(hi) | self.equal(self.numeric, hi)
            ordinary.append((self.live & ~self.less(lo)) & le_hi)
        all_tags, any_tags, forbidden = self.live, 0, 0
        if mode:
            for bit in range(64):
                if tags & (1 << bit):
                    all_tags &= self.tags[bit]
                    any_tags |= self.tags[bit]
                else:
                    forbidden |= self.tags[bit]
            subset = self.live & ~forbidden
            ordinary.append({1: all_tags, 2: any_tags, 3: all_tags & subset, 4: subset}[mode])
        combined = 0 if use_or and ordinary else self.live
        for value in ordinary:
            combined = (combined | value) if use_or else (combined & value)
        scope_mask = 0
        cache = {}
        for slot, item in enumerate(self.objects):
            if item.owner not in cache:
                cache[item.owner] = directory.contains(scope, item.owner) if recursive else item.owner == scope
            if cache[item.owner]:
                scope_mask |= 1 << slot
        return self.live & scope_mask & combined


def scalar(item, directory, scope, recursive, category, low, high, mode, tags, use_or):
    if not item.alive or not (directory.scalar_contains(scope, item.owner) if recursive else scope == item.owner):
        return False
    values = []
    if category != -1:
        values.append(item.category == category)
    if low != -1 or high != -1:
        values.append(low <= item.numeric <= high)
    if mode:
        values.append({1: item.tags & tags == tags, 2: bool(item.tags & tags),
                       3: item.tags == tags, 4: item.tags & tags == item.tags}[mode])
    return not values or (any(values) if use_or else all(values))


def run():
    directory = Directory({0: None, 1: 0, 2: 0, 3: 1, 4: 1, 5: 2, 6: 3})
    objects = [Item(i % 7, i % 2, i - 3, (i * 3) % 8) for i in range(9)]
    objects[-1].alive = False
    blocks = [Block(objects[:4]), Block(objects[4:])]
    location = [(0, i) if i < 4 else (1, i - 4) for i in range(9)]
    checked = 0
    events = ['initial']
    def verify():
        nonlocal checked
        directory.verify()
        for scope, recursive, category, limits, mode, tags, use_or in itertools.product(
                range(7), (False, True), (-1, 0, 1), ((-1,-1), (-2,1), (0,2), (2,4)),
                range(5), (0,1,3,7), (False,True)):
            args = (scope, recursive, category, *limits, mode, tags, use_or)
            for block in blocks:
                actual = block.mask(directory, *args)
                expected = sum(1 << slot for slot, item in enumerate(block.objects) if scalar(item, directory, *args))
                assert actual == expected, (events[-1], args, actual, expected)
                checked += 1
    def refresh(ids):
        for oid in ids:
            b, slot = location[oid]
            blocks[b].put(slot)
    verify()
    objects[0].category, objects[0].numeric = 1, -2
    refresh([0]); events.append('V'); verify()
    objects[1].tags = 0
    refresh([1]); events.append('T'); verify()
    objects[2].owner = 5
    events.append('M'); verify()
    frozen = [i for i, x in enumerate(objects) if x.alive and directory.scalar_contains(1, x.owner)]
    for i in frozen:
        objects[i].owner = 2
    events.append('B'); verify()
    old_owners = [x.owner for x in objects]
    directory.move(3, 2)
    assert [x.owner for x in objects] == old_owners
    events.append('R'); verify()
    objects[4].alive = False
    refresh([4]); events.append('D'); verify()
    objects[4].alive, objects[4].owner = True, 3
    refresh([4]); events.append('I-restore'); verify()
    objects[8].alive = True
    refresh([8]); events.append('I-reserve'); verify()
    rng = random.Random(101)
    moves = 0
    for _ in range(200):
        node, target = rng.randrange(1,7), rng.randrange(7)
        if not directory.scalar_contains(node, target):
            directory.move(node, target)
            directory.verify()
            moves += 1
    # Exact common-witness rejection, unlike independent nonempty flags.
    pair = Block([Item(0,1,100,0), Item(0,2,5,0)])
    assert pair.equal(pair.category,1) != 0
    assert pair.mask(directory,0,True,-1,0,10,0,0,False) != 0
    assert pair.mask(directory,0,True,1,0,10,0,0,False) == 0
    return {'kind':'finite_executable_semantics','mask_comparisons':checked,
            'mask_mismatches':0,'events':events,'additional_valid_cut_link_moves':moves,
            'ancestor_pairs_per_topology_check':49,'move_object_owner_rewrites':0,
            'common_witness_counterexample_rejected':True,'performance_claim':False}


if __name__ == '__main__':
    result = run()
    path = Path(__file__).resolve().parents[1] / 'data/wqr-semantics.json'
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))
