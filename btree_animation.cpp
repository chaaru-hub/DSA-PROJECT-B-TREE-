/*
 * B-Tree Animation in C++ (terminal version)
 * ------------------------------------------
 * Give the ORDER of the B-Tree and the VALUES to insert; the program then
 * animates every step in the terminal using ANSI colours: descending the
 * tree, overflow and node splits on insert, comparisons on search, and
 * borrowing / merging on delete. No external libraries.
 *
 * Build:   g++ -std=c++11 -O2 -o btree_animation btree_animation.cpp
 * Run:     ./btree_animation                          (asks for order and values)
 *          ./btree_animation -m 4 10 20 30 40 50      (order 4, these values)
 *          ./btree_animation --demo                   (automatic demo)
 */

#include "btree.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;
using bt::Node;

// ---------------------------------------------------------------- colours
namespace col {
const char *RESET  = "\033[0m";
const char *BOLD   = "\033[1m";
const char *DIM    = "\033[2m";
const char *RED    = "\033[1;31m";
const char *GREEN  = "\033[1;32m";
const char *YELLOW = "\033[1;33m";
const char *BLUE   = "\033[1;34m";
const char *MAGENTA= "\033[1;35m";
const char *CYAN   = "\033[1;36m";
const char *WHITE  = "\033[0;37m";
const char *FOUND  = "\033[1;30;42m";  // black on green
}

// ---------------------------------------------------------------- settings
static int  g_delayMs  = 900;   // pause between animation frames
static bool g_stepMode = false; // wait for ENTER between frames

// Highlights resolved to terminal colours for one frame
struct Colours {
    map<const Node*, const char*> nodes;
    map<pair<const Node*, int>, const char*> keys;
};

static const char *hlColour(int h) {
    switch (h) {
        case bt::VISIT:   return col::YELLOW;
        case bt::PROBLEM: return col::RED;
        case bt::NEXT:    return col::CYAN;
        case bt::NEW:     return col::GREEN;
        case bt::FOUND:   return col::FOUND;
        case bt::COMPARE: return col::MAGENTA;
        default:          return col::WHITE;
    }
}

// ---------------------------------------------------------------- canvas
// A 2-D grid of characters with a colour per cell, printed in one go.
struct Canvas {
    vector<string> ch;
    vector<vector<const char*> > co;
    void ensure(int row, int colIdx) {
        while ((int)ch.size() <= row) { ch.push_back(string()); co.push_back(vector<const char*>()); }
        if ((int)ch[row].size() <= colIdx) {
            ch[row].resize(colIdx + 1, ' ');
            co[row].resize(colIdx + 1, (const char*)0);
        }
    }
    void put(int row, int c, char x, const char *colour) {
        if (c < 0) return;
        ensure(row, c);
        ch[row][c] = x;
        co[row][c] = colour;
    }
    void text(int row, int c, const string &s, const char *colour) {
        for (size_t i = 0; i < s.size(); ++i) put(row, c + (int)i, s[i], colour);
    }
    void print() const {
        for (size_t r = 0; r < ch.size(); ++r) {
            const char *cur = 0;
            string line = "  ";
            for (size_t c = 0; c < ch[r].size(); ++c) {
                const char *want = co[r][c];
                if (want != cur) { line += col::RESET; if (want) line += want; cur = want; }
                line += ch[r][c];
            }
            line += col::RESET;
            cout << line << "\n";
        }
    }
};

// ---------------------------------------------------------------- animator
class Animator {
public:
    explicit Animator(int order) : tree(new bt::BTree(order)) { hook(); }
    ~Animator() { delete tree; }

    int order() const { return tree->order(); }

    // Start again with a new order (keys already in the tree are kept, without animation).
    void setOrder(int m) {
        vector<int> keep = tree->keys();
        delete tree;
        tree = new bt::BTree(m);
        for (size_t i = 0; i < keep.size(); ++i) tree->insert(keep[i]);
        hook();
    }

    void build(const vector<int> &values) {
        tree->clear();
        current = "BUILD (order " + to_string(order()) + ")";
        string list;
        for (size_t i = 0; i < values.size(); ++i) list += (i ? ", " : "") + to_string(values[i]);
        frame("Insert " + list + " one by one. " + rule(), bt::Highlight());
        for (size_t i = 0; i < values.size(); ++i) insert(values[i]);
    }

    void insert(int k) { current = "INSERT " + to_string(k); tree->insert(k); }
    void remove(int k) { current = "DELETE " + to_string(k); tree->remove(k); }
    bool search(int k) { current = "SEARCH " + to_string(k); return tree->search(k); }
    void clear() { tree->clear(); current.clear(); }

    string rule() const {
        int mn = tree->minKeys();
        return "Order " + to_string(order()) + ": at most " + to_string(order()) + " children and " +
               to_string(tree->maxKeys()) + " keys per node; non-root nodes need at least " + to_string(mn) +
               (mn == 1 ? " key." : " keys.");
    }

    void show(const string &msg) { frame(msg, bt::Highlight(), false); }

    void frame(const string &msg, const bt::Highlight &hl, bool wait = true) {
        Colours h;
        for (size_t i = 0; i < hl.nodes.size(); ++i) h.nodes[hl.nodes[i].first] = hlColour(hl.nodes[i].second);
        for (size_t i = 0; i < hl.keys.size(); ++i)
            h.keys[make_pair(std::get<0>(hl.keys[i]), std::get<1>(hl.keys[i]))] = hlColour(std::get<2>(hl.keys[i]));

        cout << "\033[H\033[2J";
        cout << col::BOLD << col::BLUE
             << "  ================= B-TREE ANIMATION (order m = " << order()
             << ", max keys/node = " << tree->maxKeys() << ", min = " << tree->minKeys()
             << ") =================" << col::RESET << "\n";
        if (!current.empty())
            cout << col::BOLD << "  Operation: " << col::YELLOW << current << col::RESET << "\n";
        cout << "\n";
        if (!tree->root()) cout << col::DIM << "  (empty tree)" << col::RESET << "\n";
        else               draw(h);
        cout << "\n  " << col::BOLD << ">> " << col::RESET << msg << "\n\n";
        cout << "  " << col::YELLOW << "[x]" << col::RESET << " visiting   "
             << col::RED << "[x]" << col::RESET << " overflow/underflow   "
             << col::CYAN << "[x]" << col::RESET << " next/changed   "
             << col::GREEN << "x" << col::RESET << " new/moved key   "
             << col::MAGENTA << "x" << col::RESET << " compared   "
             << col::FOUND << "x" << col::RESET << " found\n";
        cout.flush();
        if (!wait) return;
        if (g_stepMode) {
            cout << col::DIM << "  (press ENTER for next step)" << col::RESET << flush;
            string dummy;
            getline(cin, dummy);
        } else {
            this_thread::sleep_for(chrono::milliseconds(g_delayMs));
        }
    }

private:
    bt::BTree *tree;
    string current;

    void hook() { tree->onStep = [this](const string &m, const bt::Highlight &h) { frame(m, h); }; }

    // ---- layout: every subtree gets a horizontal band wide enough for its children
    map<const Node*, int> widthCache;

    static string keyText(int k) { return to_string(k); }

    static int nodeWidth(const Node *n) {
        if (n->keys.empty()) return 3;                       // "[ ]"
        int w = 1;                                           // '['
        for (size_t i = 0; i < n->keys.size(); ++i) w += (int)keyText(n->keys[i]).size() + 1; // key + '|' or ']'
        return w;
    }

    int subtreeWidth(const Node *n) {
        int own = nodeWidth(n);
        int kids = 0;
        if (!n->leaf()) {
            for (size_t i = 0; i < n->children.size(); ++i) kids += subtreeWidth(n->children[i]);
            kids += 2 * (int)(n->children.size() - 1);       // gaps between subtrees
        }
        int w = max(own, kids);
        widthCache[n] = w;
        return w;
    }

    // returns centre column of the node
    int place(Canvas &cv, const Node *n, int left, int depth, const Colours &h) {
        int w = widthCache[n];
        int nw = nodeWidth(n);
        int nodeLeft = left + (w - nw) / 2;
        int row = depth * 4;

        const char *nc = col::WHITE;
        map<const Node*, const char*>::const_iterator f = h.nodes.find(n);
        if (f != h.nodes.end()) nc = f->second;

        // node box
        int c = nodeLeft;
        if (n->keys.empty()) { cv.text(row, c, "[ ]", nc); }
        else {
            cv.put(row, c++, '[', nc);
            for (size_t i = 0; i < n->keys.size(); ++i) {
                const char *kc = (nc == col::WHITE) ? col::BOLD : nc;
                map<pair<const Node*, int>, const char*>::const_iterator kf = h.keys.find(make_pair(n, (int)i));
                if (kf != h.keys.end()) kc = kf->second;
                cv.text(row, c, keyText(n->keys[i]), kc);
                c += (int)keyText(n->keys[i]).size();
                cv.put(row, c++, i + 1 < n->keys.size() ? '|' : ']', nc);
            }
        }
        int centre = nodeLeft + nw / 2;

        if (!n->leaf()) {
            int kids = 0;
            for (size_t i = 0; i < n->children.size(); ++i) kids += widthCache[n->children[i]];
            kids += 2 * (int)(n->children.size() - 1);
            int x = left + (w - kids) / 2;
            vector<int> centres;
            for (size_t i = 0; i < n->children.size(); ++i) {
                centres.push_back(place(cv, n->children[i], x, depth + 1, h));
                x += widthCache[n->children[i]] + 2;
            }
            // connectors
            cv.put(row + 1, centre, '|', col::DIM);
            int lo = min(centre, centres.front()), hi = max(centre, centres.back());
            for (int k = lo; k <= hi; ++k) cv.put(row + 2, k, '-', col::DIM);
            for (size_t i = 0; i < centres.size(); ++i) {
                cv.put(row + 2, centres[i], '+', col::DIM);
                cv.put(row + 3, centres[i], '|', col::DIM);
            }
            cv.put(row + 2, centre, '+', col::DIM);
        }
        return centre;
    }

    void draw(const Colours &h) {
        widthCache.clear();
        subtreeWidth(tree->root());
        Canvas cv;
        place(cv, tree->root(), 0, 0, h);
        cv.print();
    }
};

// ---------------------------------------------------------------- UI
static void enableAnsi() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode)) SetConsoleMode(hOut, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/);
#endif
}

static void printHelp() {
    cout << "\n  Commands (several keys allowed, e.g.  i 10 20 30):\n"
         << "    new           enter a new order and values, then animate\n"
         << "    i <keys...>   insert keys          d <keys...>   delete keys\n"
         << "    s <key>       search for a key     r <n>         insert n random keys\n"
         << "    order <m>     change the order     clear         empty the tree\n"
         << "    demo          run the demo         speed <ms>    frame delay (now " << g_delayMs << " ms)\n"
         << "    step          toggle step-by-step mode (ENTER per frame, now "
         << (g_stepMode ? "ON" : "OFF") << ")\n"
         << "    help          show this help       q             quit\n";
}

static vector<int> parseInts(const string &line) {
    string s = line;
    for (size_t i = 0; i < s.size(); ++i) if (s[i] == ',' || s[i] == ';') s[i] = ' ';
    istringstream in(s);
    vector<int> v;
    int k;
    while (in >> k) v.push_back(k);
    return v;
}

// Ask for the order and the values, then animate building the tree.
static bool askAndBuild(Animator &anim) {
    string line;
    int m = 0;
    while (m < 3 || m > 20) {
        cout << "\n  " << col::GREEN << "Enter the ORDER of the B-Tree (3-20): " << col::RESET << flush;
        if (!getline(cin, line)) return false;
        m = atoi(line.c_str());
        if (m < 3 || m > 20) cout << "  Please enter a whole number from 3 to 20.\n";
    }
    vector<int> values;
    while (values.empty()) {
        cout << "  " << col::GREEN << "Enter the VALUES to insert, in order (e.g. 10 20 5 6 12): " << col::RESET << flush;
        if (!getline(cin, line)) return false;
        values = parseInts(line);
        if (values.empty()) cout << "  Please enter at least one number.\n";
    }
    anim.setOrder(m);
    anim.build(values);
    anim.show("Done: the B-Tree of order " + to_string(m) + " is built. Type help to see more commands.");
    return true;
}

static void runDemo(Animator &anim) {
    int ins[] = {10, 20, 5, 6, 12, 30, 7, 17, 3, 4, 2, 25, 27, 40, 50, 1};
    anim.build(vector<int>(ins, ins + sizeof(ins) / sizeof(ins[0])));
    anim.search(17);
    anim.search(8);
    int del[] = {6, 20, 5, 10, 1, 27};
    for (size_t i = 0; i < sizeof(del) / sizeof(del[0]); ++i) anim.remove(del[i]);
}

int main(int argc, char **argv) {
    enableAnsi();
    srand((unsigned)time(0));

    int m = 0;
    bool demo = false;
    vector<int> values;
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a == "--demo") demo = true;
        else if ((a == "-m" || a == "--order") && i + 1 < argc) m = max(3, atoi(argv[++i]));
        else if (a == "--speed" && i + 1 < argc) g_delayMs = max(0, atoi(argv[++i]));
        else if (a == "-h" || a == "--help") {
            cout << "Usage: " << argv[0] << " [-m order] [values...] [--speed ms] [--demo]\n"
                 << "  With no order or values, the program asks for them.\n";
            return 0;
        } else {
            vector<int> v = parseInts(a);
            values.insert(values.end(), v.begin(), v.end());
        }
    }

    Animator anim(m ? m : 3);
    if (demo) {
        runDemo(anim);
        anim.show("Demo finished.");
        cout << "\n";
        return 0;
    }

    anim.show("Welcome! A B-Tree of order m holds at most m-1 keys per node.");
    if (m && !values.empty()) {
        anim.build(values);
        anim.show("Done: the B-Tree of order " + to_string(m) + " is built. Type help to see more commands.");
    } else if (!values.empty()) {
        anim.build(values);
        anim.show("Built with order 3 (use -m to choose another order). Type help to see more commands.");
    } else if (!askAndBuild(anim)) {
        cout << "\n";
        return 0;
    }
    printHelp();

    string line;
    while (true) {
        cout << "\n  " << col::GREEN << "btree> " << col::RESET << flush;
        if (!getline(cin, line)) break;
        istringstream in(line);
        string cmd;
        if (!(in >> cmd)) continue;
        string rest;
        getline(in, rest);
        vector<int> keys = parseInts(rest);

        if (cmd == "q" || cmd == "quit" || cmd == "exit") break;
        else if (cmd == "help" || cmd == "h") { printHelp(); continue; }
        else if (cmd == "new") { if (!askAndBuild(anim)) break; }
        else if (cmd == "i" || cmd == "insert") { for (size_t j = 0; j < keys.size(); ++j) anim.insert(keys[j]); anim.show("Insert done."); }
        else if (cmd == "d" || cmd == "delete") { for (size_t j = 0; j < keys.size(); ++j) anim.remove(keys[j]); anim.show("Delete done."); }
        else if (cmd == "s" || cmd == "search") {
            if (!keys.empty()) {
                bool ok = anim.search(keys[0]);
                anim.show(to_string(keys[0]) + (ok ? " was found." : " was NOT found."));
            }
        }
        else if (cmd == "r" || cmd == "random") {
            int n = keys.empty() ? 10 : keys[0];
            for (int j = 0; j < n; ++j) anim.insert(rand() % 100 + 1);
            anim.show("Inserted random keys.");
        }
        else if (cmd == "order" || cmd == "m") {
            if (!keys.empty() && keys[0] >= 3) { anim.setOrder(keys[0]); anim.show(anim.rule()); }
            else anim.show("Give an order of at least 3, e.g.  order 4");
        }
        else if (cmd == "demo") { runDemo(anim); anim.show("Demo finished."); }
        else if (cmd == "clear") { anim.clear(); anim.show("Tree cleared."); }
        else if (cmd == "speed") { if (!keys.empty()) g_delayMs = max(0, keys[0]); anim.show("Speed set to " + to_string(g_delayMs) + " ms/frame."); }
        else if (cmd == "step") { g_stepMode = !g_stepMode; anim.show(string("Step mode ") + (g_stepMode ? "ON" : "OFF") + "."); }
        else { anim.show("Unknown command: " + cmd); printHelp(); }
    }
    cout << "\n  Bye!\n";
    return 0;
}
