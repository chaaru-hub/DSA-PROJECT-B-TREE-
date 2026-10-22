/*
 * btree.h - B-Tree of any order m, with a step-by-step hook for animation.
 *
 * Order m (Knuth's definition):
 *   - every node has at most m children and at most m-1 keys
 *   - every node except the root has at least ceil(m/2)-1 keys
 *   - all leaves are on the same level
 *
 * Insert works bottom-up: put the key in its leaf, and while a node has
 * too many keys, split it at the median and push the median into the parent.
 * Delete removes from a leaf (an internal key is first replaced by its
 * predecessor or successor), then fixes any underflow by borrowing from a
 * sibling or merging with one.
 *
 * Every interesting moment calls onStep(message, highlight) so a front end
 * can draw it. Used by btree_animation.cpp (terminal) and gui/btree_gui.cpp.
 */
#ifndef BTREE_H
#define BTREE_H

#include <algorithm>
#include <functional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace bt {

enum Hl { NONE, VISIT, PROBLEM, NEXT, NEW, FOUND, COMPARE };

struct Node {
    std::vector<int> keys;
    std::vector<Node*> children;
    bool leaf() const { return children.empty(); }
};

struct Highlight {
    std::vector<std::pair<const Node*, int> > nodes;           // node, Hl
    std::vector<std::tuple<const Node*, int, int> > keys;      // node, key index, Hl
    Highlight &node(const Node *n, int h) { nodes.push_back(std::make_pair(n, h)); return *this; }
    Highlight &key(const Node *n, int i, int h) { keys.push_back(std::make_tuple(n, i, h)); return *this; }
};

class BTree {
public:
    typedef std::function<void(const std::string&, const Highlight&)> StepFn;
    StepFn onStep;   // leave empty to change the tree without animation

    explicit BTree(int order) : m(std::max(3, order)) {}
    ~BTree() { destroy(root_); }

    int order() const { return m; }
    int maxKeys() const { return m - 1; }
    int minKeys() const { return (m + 1) / 2 - 1; }   // ceil(m/2) - 1
    const Node *root() const { return root_; }

    void clear() { destroy(root_); root_ = 0; }

    bool contains(int k) const {
        const Node *x = root_;
        while (x) {
            size_t i = std::lower_bound(x->keys.begin(), x->keys.end(), k) - x->keys.begin();
            if (i < x->keys.size() && x->keys[i] == k) return true;
            if (x->leaf()) return false;
            x = x->children[i];
        }
        return false;
    }

    std::vector<int> keys() const { std::vector<int> v; collect(root_, v); return v; }

    // ------------------------------------------------------------ search
    bool search(int k) {
        std::string ks = S(k);
        Node *x = root_;
        if (!x) { step("The tree is empty, so " + ks + " is not here."); return false; }
        while (true) {
            int i = 0;
            while (i < (int)x->keys.size() && k > x->keys[i]) {
                step(ks + " > " + S(x->keys[i]) + ", keep moving right.",
                     Highlight().node(x, VISIT).key(x, i, COMPARE));
                ++i;
            }
            if (i < (int)x->keys.size() && x->keys[i] == k) {
                step("Found " + ks + ".", Highlight().node(x, VISIT).key(x, i, FOUND));
                return true;
            }
            if (x->leaf()) {
                step("Reached a leaf without finding " + ks + ". It is not in the tree.",
                     Highlight().node(x, PROBLEM));
                return false;
            }
            step(goDown(k, x, i), Highlight().node(x, VISIT).node(x->children[i], NEXT));
            x = x->children[i];
        }
    }

    // ------------------------------------------------------------ insert
    bool insert(int k) {
        std::string ks = S(k);
        if (contains(k)) { step(ks + " is already in the tree. Keys are kept unique, so it is skipped."); return false; }
        if (!root_) {
            root_ = new Node;
            root_->keys.push_back(k);
            step("The tree was empty, so " + ks + " becomes the root.", Highlight().key(root_, 0, NEW));
            return true;
        }

        std::vector<std::pair<Node*, int> > path;   // (parent, child index taken)
        Node *x = root_;
        while (!x->leaf()) {
            int i = upper(x, k);
            step(goDown(k, x, i), Highlight().node(x, VISIT).node(x->children[i], NEXT));
            path.push_back(std::make_pair(x, i));
            x = x->children[i];
        }

        int pos = upper(x, k);
        step("Reached a leaf. " + ks + " goes here in sorted order.", Highlight().node(x, VISIT));
        x->keys.insert(x->keys.begin() + pos, k);
        if ((int)x->keys.size() > maxKeys())
            step("Inserted " + ks + ". The node now has " + S(x->keys.size()) + " keys but order " + S(m) +
                 " allows at most " + S(maxKeys()) + ": overflow, so it must split.",
                 Highlight().node(x, PROBLEM).key(x, pos, NEW));
        else
            step("Inserted " + ks + ".", Highlight().key(x, pos, NEW));

        while ((int)x->keys.size() > maxKeys()) {
            int mid = (int)x->keys.size() / 2;
            int median = x->keys[mid];
            step("Split at the median " + S(median) + ": keys left of it stay, keys right of it form a new node, "
                 "and " + S(median) + " moves up.",
                 Highlight().node(x, PROBLEM).key(x, mid, NEW));

            Node *z = new Node;
            z->keys.assign(x->keys.begin() + mid + 1, x->keys.end());
            if (!x->leaf()) {
                z->children.assign(x->children.begin() + mid + 1, x->children.end());
                x->children.resize(mid + 1);
            }
            x->keys.resize(mid);

            Node *p;
            int ci;
            bool grew = path.empty();
            if (grew) {
                p = new Node;
                p->children.push_back(x);
                root_ = p;
                ci = 0;
            } else {
                p = path.back().first;
                ci = path.back().second;
                path.pop_back();
            }
            p->keys.insert(p->keys.begin() + ci, median);
            p->children.insert(p->children.begin() + ci + 1, z);

            std::string msg = grew
                ? S(median) + " becomes the new root. The tree grows one level taller."
                : S(median) + " moved up into the parent.";
            Highlight h;
            h.node(x, NEXT).node(z, NEXT).key(p, ci, NEW);
            if ((int)p->keys.size() > maxKeys()) {
                msg += " Now the parent has " + S(p->keys.size()) + " keys: it overflows too.";
                h.node(p, PROBLEM);
            }
            step(msg, h);
            x = p;
        }
        return true;
    }

    // ------------------------------------------------------------ delete
    bool remove(int k) {
        std::string ks = S(k);
        if (!root_) { step("The tree is empty."); return false; }

        std::vector<std::pair<Node*, int> > path;
        Node *x = root_;
        int idx;
        while (true) {
            idx = lower(x, k);
            if (idx < (int)x->keys.size() && x->keys[idx] == k) break;
            if (x->leaf()) {
                step(ks + " is not in the tree, so nothing is deleted.", Highlight().node(x, PROBLEM));
                return false;
            }
            step(ks + " is not in this node. " + goDown(k, x, idx), Highlight().node(x, VISIT).node(x->children[idx], NEXT));
            path.push_back(std::make_pair(x, idx));
            x = x->children[idx];
        }
        step("Found " + ks + ".", Highlight().node(x, VISIT).key(x, idx, PROBLEM));

        if (!x->leaf()) {
            // Replace with the predecessor, or the successor if that leaf can spare a key and the other cannot.
            Node *pl = x->children[idx];
            while (!pl->leaf()) pl = pl->children.back();
            Node *sl = x->children[idx + 1];
            while (!sl->leaf()) sl = sl->children.front();
            bool usePred = (int)pl->keys.size() > minKeys() || (int)sl->keys.size() <= minKeys();
            Node *leaf = usePred ? pl : sl;
            int li = usePred ? (int)leaf->keys.size() - 1 : 0;
            int r = leaf->keys[li];
            step(ks + " is in an internal node. Replace it with its " +
                 (usePred ? "predecessor " + S(r) + " (the largest key in its left subtree)."
                          : "successor " + S(r) + " (the smallest key in its right subtree)."),
                 Highlight().key(x, idx, PROBLEM).key(leaf, li, NEW));
            x->keys[idx] = r;
            step(S(r) + " copied up. Now delete the old " + S(r) + " from its leaf.",
                 Highlight().key(x, idx, NEW).key(leaf, li, PROBLEM));

            int c = usePred ? idx : idx + 1;
            path.push_back(std::make_pair(x, c));
            Node *y = x->children[c];
            while (!y->leaf()) {
                int cc = usePred ? (int)y->children.size() - 1 : 0;
                path.push_back(std::make_pair(y, cc));
                y = y->children[cc];
            }
            x = y;
            idx = li;
            k = r;
            ks = S(r);
        }

        x->keys.erase(x->keys.begin() + idx);
        if (x != root_ && (int)x->keys.size() < minKeys())
            step("Removed " + ks + " from the leaf. It now has " + S(x->keys.size()) + " key(s), fewer than the minimum " +
                 S(minKeys()) + ": underflow.", Highlight().node(x, PROBLEM));
        else
            step("Removed " + ks + " from the leaf.", Highlight().node(x, NEXT));

        while (x != root_ && (int)x->keys.size() < minKeys()) {
            Node *p = path.back().first;
            int ci = path.back().second;
            path.pop_back();
            Node *L = ci > 0 ? p->children[ci - 1] : 0;
            Node *R = ci + 1 < (int)p->children.size() ? p->children[ci + 1] : 0;

            if (L && (int)L->keys.size() > minKeys()) {
                step("Borrow from the left sibling: the parent key " + S(p->keys[ci - 1]) + " moves down and " +
                     S(L->keys.back()) + " moves up to replace it.",
                     Highlight().node(x, PROBLEM).node(L, VISIT).key(p, ci - 1, NEW).key(L, (int)L->keys.size() - 1, NEW));
                x->keys.insert(x->keys.begin(), p->keys[ci - 1]);
                p->keys[ci - 1] = L->keys.back();
                L->keys.pop_back();
                if (!L->leaf()) { x->children.insert(x->children.begin(), L->children.back()); L->children.pop_back(); }
                step("Borrowed. Every node has enough keys again.",
                     Highlight().key(x, 0, NEW).key(p, ci - 1, NEW).node(L, NEXT));
                break;
            }
            if (R && (int)R->keys.size() > minKeys()) {
                step("Borrow from the right sibling: the parent key " + S(p->keys[ci]) + " moves down and " +
                     S(R->keys.front()) + " moves up to replace it.",
                     Highlight().node(x, PROBLEM).node(R, VISIT).key(p, ci, NEW).key(R, 0, NEW));
                x->keys.push_back(p->keys[ci]);
                p->keys[ci] = R->keys.front();
                R->keys.erase(R->keys.begin());
                if (!R->leaf()) { x->children.push_back(R->children.front()); R->children.erase(R->children.begin()); }
                step("Borrowed. Every node has enough keys again.",
                     Highlight().key(x, (int)x->keys.size() - 1, NEW).key(p, ci, NEW).node(R, NEXT));
                break;
            }

            int li = L ? ci - 1 : ci;
            Node *a = p->children[li], *b = p->children[li + 1];
            step("No sibling can spare a key. Merge with the " + std::string(L ? "left" : "right") +
                 " sibling, pulling the parent key " + S(p->keys[li]) + " down between them.",
                 Highlight().node(a, PROBLEM).node(b, PROBLEM).key(p, li, NEW));
            a->keys.push_back(p->keys[li]);
            int sepPos = (int)a->keys.size() - 1;
            a->keys.insert(a->keys.end(), b->keys.begin(), b->keys.end());
            a->children.insert(a->children.end(), b->children.begin(), b->children.end());
            p->keys.erase(p->keys.begin() + li);
            p->children.erase(p->children.begin() + li + 1);
            b->children.clear();
            delete b;
            x = p;
            if (x != root_ && (int)x->keys.size() < minKeys())
                step("Merged. The parent lost a key and now underflows too.",
                     Highlight().node(a, NEXT).key(a, sepPos, NEW).node(x, PROBLEM));
            else
                step("Merged into one node with " + S(a->keys.size()) + " keys.",
                     Highlight().node(a, NEXT).key(a, sepPos, NEW));
        }

        if (root_->keys.empty()) {
            Node *old = root_;
            if (old->leaf()) {
                root_ = 0;
                delete old;
                step("The tree is now empty.");
            } else {
                root_ = old->children[0];
                old->children.clear();
                delete old;
                step("The root has no keys left, so its only child becomes the new root. The tree shrinks one level.",
                     Highlight().node(root_, NEXT));
            }
        }
        return true;
    }

    // ------------------------------------------------------------ checks (used by tests)
    bool valid(std::string *why = 0) const {
        if (!root_) return true;
        int leafDepth = -1;
        return check(root_, 0, leafDepth, 0, 0, false, false, why);
    }

private:
    Node *root_ = 0;
    int m;

    static std::string S(long long v) { return std::to_string(v); }

    void step(const std::string &msg, const Highlight &h = Highlight()) { if (onStep) onStep(msg, h); }

    static int upper(const Node *x, int k) { return (int)(std::upper_bound(x->keys.begin(), x->keys.end(), k) - x->keys.begin()); }
    static int lower(const Node *x, int k) { return (int)(std::lower_bound(x->keys.begin(), x->keys.end(), k) - x->keys.begin()); }

    static std::string goDown(int k, const Node *x, int i) {
        if (i == 0) return S(k) + " < " + S(x->keys[0]) + ", so go down to the first child.";
        if (i == (int)x->keys.size()) return S(k) + " > " + S(x->keys.back()) + ", so go down to the last child.";
        return S(x->keys[i - 1]) + " < " + S(k) + " < " + S(x->keys[i]) + ", so go down to child " + S(i + 1) + ".";
    }

    static void destroy(Node *n) {
        if (!n) return;
        for (size_t i = 0; i < n->children.size(); ++i) destroy(n->children[i]);
        delete n;
    }

    static void collect(const Node *n, std::vector<int> &v) {
        if (!n) return;
        for (size_t i = 0; i < n->keys.size(); ++i) {
            if (!n->leaf()) collect(n->children[i], v);
            v.push_back(n->keys[i]);
        }
        if (!n->leaf()) collect(n->children.back(), v);
    }

    bool fail(std::string *why, const std::string &s) const { if (why) *why = s; return false; }

    bool check(const Node *n, int depth, int &leafDepth, int lo, int hi, bool hasLo, bool hasHi, std::string *why) const {
        int nk = (int)n->keys.size();
        if (nk > maxKeys()) return fail(why, "node has too many keys");
        if (n != root_ && nk < minKeys()) return fail(why, "node has too few keys");
        if (nk == 0) return fail(why, "empty node");
        for (int i = 0; i < nk; ++i) {
            if (i && n->keys[i - 1] >= n->keys[i]) return fail(why, "keys not sorted");
            if ((hasLo && n->keys[i] <= lo) || (hasHi && n->keys[i] >= hi)) return fail(why, "key out of range");
        }
        if (n->leaf()) {
            if (leafDepth < 0) leafDepth = depth;
            return leafDepth == depth ? true : fail(why, "leaves at different depths");
        }
        if ((int)n->children.size() != nk + 1) return fail(why, "wrong number of children");
        for (int i = 0; i <= nk; ++i) {
            bool l = i > 0 || hasLo, h = i < nk || hasHi;
            int lv = i > 0 ? n->keys[i - 1] : lo, hv = i < nk ? n->keys[i] : hi;
            if (!check(n->children[i], depth + 1, leafDepth, lv, hv, l, h, why)) return false;
        }
        return true;
    }
};

}  // namespace bt

#endif
