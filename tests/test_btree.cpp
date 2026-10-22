// Randomised check of btree.h: after every insert/delete the tree must be a
// valid B-Tree of its order and hold exactly the keys a std::set holds.
#include "../btree.h"

#include <cstdio>
#include <cstdlib>
#include <set>

int main() {
    srand(12345);
    long steps = 0;
    for (int order = 3; order <= 10; ++order) {
        for (int round = 0; round < 20; ++round) {
            bt::BTree tree(order);
            tree.onStep = [&](const std::string &, const bt::Highlight &) { ++steps; };
            std::set<int> ref;
            int range = 50 + rand() % 300;
            for (int op = 0; op < 2000; ++op) {
                int k = rand() % range;
                int what = rand() % 10;
                if (what < 5) {
                    bool added = tree.insert(k);
                    if (added != ref.insert(k).second) { printf("order %d: insert %d result mismatch\n", order, k); return 1; }
                } else if (what < 9) {
                    bool removed = tree.remove(k);
                    if (removed != (ref.erase(k) == 1)) { printf("order %d: delete %d result mismatch\n", order, k); return 1; }
                } else if (tree.search(k) != (ref.count(k) == 1)) {
                    printf("order %d: search %d wrong\n", order, k); return 1;
                }
                std::string why;
                if (!tree.valid(&why)) { printf("order %d: invalid tree after op %d: %s\n", order, op, why.c_str()); return 1; }
                if (tree.keys() != std::vector<int>(ref.begin(), ref.end())) { printf("order %d: key set differs\n", order); return 1; }
            }
        }
    }
    printf("all checks passed (orders 3-10, %ld animation steps)\n", steps);
    return 0;
}
