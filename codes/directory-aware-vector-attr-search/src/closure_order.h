#pragma once

#include "neighborhood_filter.h"
#include <limits>
#include <utility>

namespace closure {

// Stable integer handles into a single allocation. Directory topology changes cut
// and splice a treap span; no descendant labels or cached DFS times are rewritten.
class DirectoryOrder {
public:
    using Handle = size_t;
    static constexpr Handle none = std::numeric_limits<Handle>::max();
    struct Bounds { size_t begin, end, first_live, live_count; };

    explicit DirectoryOrder(const npi::Dataset& data) : directories_(data.parent.size()) {
        if (!directories_ || directories_ > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            data.items.size() > std::numeric_limits<npi::Id>::max() ||
            directories_ > (none - data.items.size()) / 3)
            throw std::runtime_error("closure: invalid order dimensions");
        nodes_.resize(3 * directories_ + data.items.size());
        for (size_t i = 0; i < nodes_.size(); ++i) {
            auto& node = nodes_[i];
            node.priority = random_priority();
            node.object = i >= 3 * directories_;
            node.live = node.object ? 1 : 0;
        }
        std::vector<Handle> first_child(directories_, none), next_child(directories_, none);
        std::vector<Handle> first_item(directories_, none), next_item(data.items.size(), none);
        Handle root_dir = none;
        // Prepending descending IDs yields ascending initial sibling/object order.
        for (size_t i = directories_; i-- > 0;) {
            const int p = data.parent[i];
            if (p == -1) {
                if (root_dir != none) throw std::runtime_error("closure: multiple roots");
                root_dir = i;
            } else {
                if (p < 0 || static_cast<size_t>(p) >= directories_ || static_cast<size_t>(p) == i)
                    throw std::runtime_error("closure: invalid parent");
                next_child[i] = first_child[p];
                first_child[p] = i;
            }
        }
        if (root_dir == none) throw std::runtime_error("closure: missing root");
        for (size_t i = data.items.size(); i-- > 0;) {
            const auto& item = data.items[i];
            if (item.owner >= directories_) throw std::runtime_error("closure: invalid owner");
            if (item.alive) { next_item[i] = first_item[item.owner]; first_item[item.owner] = i; }
        }
        struct Frame { Handle dir, child; };
        std::vector<Frame> stack;
        size_t reached = 0;
        auto enter_dir = [&](Handle d) {
            ++reached;
            append(enter(d));
            for (Handle i = first_item[d]; i != none; i = next_item[i]) append(object(i));
            append(direct_end(d));
            stack.push_back({d, first_child[d]});
        };
        enter_dir(root_dir);
        while (!stack.empty()) {
            auto& top = stack.back();
            if (top.child == none) { append(exit(top.dir)); stack.pop_back(); }
            else {
                const Handle child = top.child;
                top.child = next_child[child];
                enter_dir(child);
            }
        }
        if (reached != directories_) throw std::runtime_error("closure: disconnected/cyclic tree");
    }

    Handle enter(size_t dir) const { return 3 * dir; }
    Handle direct_end(size_t dir) const { return 3 * dir + 1; }
    Handle exit(size_t dir) const { return 3 * dir + 2; }
    Handle object(size_t id) const { return 3 * directories_ + id; }
    size_t rank(Handle h) const {
        size_t result = size(nodes_[h].left);
        while (nodes_[h].parent != none) {
            const Handle p = nodes_[h].parent;
            if (nodes_[p].right == h) result += size(nodes_[p].left) + 1;
            h = p;
        }
        return result;
    }
    size_t prefix_live(size_t count) const {
        Handle h = root_;
        size_t result = 0;
        while (h != none && count) {
            const auto& n = nodes_[h];
            const size_t left = size(n.left);
            if (count <= left) h = n.left;
            else {
                result += live(n.left) + static_cast<size_t>(n.object);
                count -= left + 1;
                h = n.right;
            }
        }
        return result;
    }
    npi::Id live_at(size_t ordinal) const {
        Handle h = root_;
        if (ordinal >= live(root_)) throw std::runtime_error("closure: invalid live ordinal");
        while (h != none) {
            const auto& n = nodes_[h];
            const size_t left = live(n.left);
            if (ordinal < left) h = n.left;
            else if (n.object && ordinal == left)
                return static_cast<npi::Id>(h - 3 * directories_);
            else { ordinal -= left + static_cast<size_t>(n.object); h = n.right; }
        }
        throw std::runtime_error("closure: broken order aggregate");
    }
    Bounds bounds(npi::Id dir, bool recursive) const {
        const size_t begin = rank(enter(dir)) + 1;
        const size_t end = rank(recursive ? exit(dir) : direct_end(dir));
        const size_t first = prefix_live(begin);
        return {begin, end, first, prefix_live(end) - first};
    }
    bool beneath(npi::Id child, npi::Id ancestor) const {
        const size_t r = rank(enter(child));
        return rank(enter(ancestor)) <= r && r <= rank(exit(ancestor));
    }
    void remove_object(npi::Id id) { cut(rank(object(id)), 1); }
    void insert_object(npi::Id id, npi::Id owner) { insert_before(object(id), direct_end(owner)); }
    void move_object(npi::Id id, npi::Id owner) {
        const Handle token = cut(rank(object(id)), 1);
        insert_before(token, direct_end(owner));
    }
    void reparent(npi::Id source, npi::Id destination) {
        const size_t begin = rank(enter(source));
        const size_t length = rank(exit(source)) - begin + 1;
        const Handle span = cut(begin, length);
        insert_before(span, exit(destination));
    }
    size_t live_count() const { return live(root_); }
    size_t allocation_bytes() const { return nodes_.capacity() * sizeof(Node); }

private:
    struct Node {
        Handle left = none, right = none, parent = none;
        uint64_t priority = 0;
        size_t size = 1, live = 0;
        bool object = false;
    };
    std::vector<Node> nodes_;
    size_t directories_;
    Handle root_ = none;
    uint64_t random_ = 100;
    uint64_t random_priority() {
        uint64_t z = (random_ += UINT64_C(0x9e3779b97f4a7c15));
        z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
        return z ^ (z >> 31);
    }
    size_t size(Handle h) const { return h == none ? 0 : nodes_[h].size; }
    size_t live(Handle h) const { return h == none ? 0 : nodes_[h].live; }
    void pull(Handle h) {
        auto& n = nodes_[h];
        n.size = 1 + size(n.left) + size(n.right);
        n.live = static_cast<size_t>(n.object) + live(n.left) + live(n.right);
        if (n.left != none) nodes_[n.left].parent = h;
        if (n.right != none) nodes_[n.right].parent = h;
    }
    Handle merge(Handle a, Handle b) {
        if (a == none || b == none) {
            const Handle h = a == none ? b : a;
            if (h != none) nodes_[h].parent = none;
            return h;
        }
        if (nodes_[a].priority > nodes_[b].priority) {
            nodes_[a].right = merge(nodes_[a].right, b);
            pull(a); nodes_[a].parent = none; return a;
        }
        nodes_[b].left = merge(a, nodes_[b].left);
        pull(b); nodes_[b].parent = none; return b;
    }
    std::pair<Handle, Handle> split(Handle h, size_t count) {
        if (h == none) return {none, none};
        if (count <= size(nodes_[h].left)) {
            auto parts = split(nodes_[h].left, count);
            nodes_[h].left = parts.second;
            pull(h); nodes_[h].parent = none;
            return {parts.first, h};
        }
        auto parts = split(nodes_[h].right, count - size(nodes_[h].left) - 1);
        nodes_[h].right = parts.first;
        pull(h); nodes_[h].parent = none;
        return {h, parts.second};
    }
    Handle cut(size_t begin, size_t length) {
        const auto prefix = split(root_, begin);
        const auto suffix = split(prefix.second, length);
        root_ = merge(prefix.first, suffix.second);
        return suffix.first;
    }
    void insert_before(Handle span, Handle before) {
        const auto parts = split(root_, rank(before));
        root_ = merge(merge(parts.first, span), parts.second);
    }
    void append(Handle token) { root_ = merge(root_, token); }
};

} // namespace closure
