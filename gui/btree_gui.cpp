/*
 * B-Tree Visualizer (graphical version)
 * -------------------------------------
 * Animated B-Tree insert / search / delete with smooth key movement,
 * built with raylib. Every algorithm step is recorded as a snapshot of
 * the tree; the player then tweens each key from where it was to where
 * it is in the next snapshot, so splits, merges and borrows are visible.
 *
 * Build (from the repository root):
 *     cmake -B build && cmake --build build
 *     ./build/btree_gui
 *
 * Controls:  type values (e.g. "10 20 30"), pick the order m and press
 *            Build & animate. Enter inserts into the current tree,
 *            drag to pan, mouse wheel to zoom, F to fit, Space to play/pause,
 *            Left / Right arrows to step.
 */

#include "raylib.h"
#include "raymath.h"

#include "../btree.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>
#include <tuple>
#include <sstream>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

using std::map;
using std::pair;
using std::string;
using std::vector;

// ============================================================== theme
namespace theme {
const Color bgTop     = {11, 16, 32, 255};
const Color bgBottom  = {17, 24, 51, 255};
const Color panel     = {18, 26, 46, 255};
const Color panelEdge = {34, 45, 74, 255};
const Color field     = {11, 17, 33, 255};
const Color text      = {230, 235, 245, 255};
const Color muted     = {139, 151, 176, 255};
const Color faint     = {86, 98, 124, 255};
const Color node      = {24, 33, 58, 255};
const Color nodeEdge  = {52, 66, 102, 255};
const Color edge      = {70, 86, 128, 255};
const Color amber     = {255, 181, 71, 255};
const Color red       = {255, 107, 107, 255};
const Color cyan      = {79, 214, 232, 255};
const Color green     = {91, 227, 125, 255};
const Color violet    = {199, 146, 255, 255};
const Color blue      = {110, 168, 255, 255};
const Color ink       = {10, 14, 28, 255};
}

// ============================================================== highlights
// same values as bt::Hl
enum Hl { HL_NONE, HL_VISIT, HL_PROBLEM, HL_NEXT, HL_NEW, HL_FOUND, HL_COMPARE };
enum OpKind { OP_NONE, OP_INSERT, OP_DELETE, OP_SEARCH, OP_INFO };

static Color hlColor(int h) {
    switch (h) {
        case HL_VISIT:   return theme::amber;
        case HL_PROBLEM: return theme::red;
        case HL_NEXT:    return theme::cyan;
        case HL_NEW:     return theme::green;
        case HL_FOUND:   return theme::green;
        case HL_COMPARE: return theme::violet;
        default:         return theme::nodeEdge;
    }
}

static Color opColor(int k) {
    switch (k) {
        case OP_INSERT: return theme::green;
        case OP_DELETE: return theme::red;
        case OP_SEARCH: return theme::blue;
        default:        return theme::muted;
    }
}

// ============================================================== snapshots
struct SNode {
    vector<int> keys;
    vector<int> kids;
};

struct Snapshot {
    vector<SNode> nodes;              // index 0 is the root, level order
    map<int, int> nodeHl;             // node index -> Hl
    map<pair<int, int>, int> keyHl;   // (node index, key index) -> Hl
    string op, msg;
    int kind = OP_NONE;
    int opId = 0;
    int probe = 0;                    // the key being inserted/searched/deleted
    int keyCount = 0, height = 0;
};

// ============================================================== B-Tree model
// The algorithm lives in btree.h (shared with the terminal version).
// The recorder turns each of its steps into a Snapshot for the player.
struct Recorder {
    bt::BTree *tree = nullptr;
    vector<Snapshot> *out = nullptr;
    string op;
    int kind = OP_NONE, key = 0, opId = 0;

    void begin(int k, const string &o, int keyValue) { kind = k; op = o; key = keyValue; ++opId; }

    void attach(bt::BTree *t) {
        tree = t;
        t->onStep = [this](const string &msg, const bt::Highlight &h) { record(msg, h); };
    }

    void record(const string &msg, const bt::Highlight &h = bt::Highlight()) {
        Snapshot s;
        s.op = op; s.msg = msg; s.kind = kind; s.opId = opId; s.probe = key;
        map<const bt::Node*, int> index;
        if (const bt::Node *root = tree->root()) {
            vector<const bt::Node*> q{root};
            for (size_t i = 0; i < q.size(); ++i) {
                index[q[i]] = (int)i;
                for (const bt::Node *c : q[i]->children) q.push_back(c);
            }
            s.nodes.resize(q.size());
            for (size_t i = 0; i < q.size(); ++i) {
                s.nodes[i].keys = q[i]->keys;
                for (const bt::Node *c : q[i]->children) s.nodes[i].kids.push_back(index[c]);
                s.keyCount += (int)q[i]->keys.size();
            }
            for (const bt::Node *n = root; n; n = n->leaf() ? nullptr : n->children[0]) ++s.height;
        }
        for (auto &n : h.nodes) {
            auto it = index.find(n.first);
            if (it != index.end()) s.nodeHl[it->second] = n.second;
        }
        for (auto &k : h.keys) {
            auto it = index.find(std::get<0>(k));
            if (it != index.end()) s.keyHl[{it->second, std::get<1>(k)}] = std::get<2>(k);
        }
        out->push_back(s);
    }
};

// ============================================================== layout
static const float KW = 50.f;     // key cell width
static const float NH = 50.f;     // node height
static const float PAD = 6.f;     // node inner padding
static const float GAPX = 30.f;   // gap between sibling subtrees
static const float LEVEL = 124.f; // vertical distance between levels

struct Layout {
    vector<Rectangle> rect;
    vector<vector<Vector2> > keyPos;
    Rectangle bounds{0, 0, 0, 0};
};

static float nodeWidth(const SNode &n) { return std::max<size_t>(1, n.keys.size()) * KW + 2 * PAD; }

static float subtreeWidth(const Snapshot &s, int i, vector<float> &w) {
    const SNode &n = s.nodes[i];
    float kids = 0;
    for (size_t c = 0; c < n.kids.size(); ++c) kids += subtreeWidth(s, n.kids[c], w) + (c ? GAPX : 0);
    w[i] = std::max(nodeWidth(n), kids);
    return w[i];
}

static void placeNode(const Snapshot &s, int i, float left, float y, const vector<float> &w, Layout &L) {
    const SNode &n = s.nodes[i];
    float nw = nodeWidth(n);
    float x = left + (w[i] - nw) / 2;
    L.rect[i] = {x, y, nw, NH};
    L.keyPos[i].clear();
    for (size_t k = 0; k < n.keys.size(); ++k)
        L.keyPos[i].push_back({x + PAD + KW * (k + 0.5f), y + NH / 2});
    float kids = 0;
    for (size_t c = 0; c < n.kids.size(); ++c) kids += w[n.kids[c]] + (c ? GAPX : 0);
    float cx = left + (w[i] - kids) / 2;
    for (int c : n.kids) { placeNode(s, c, cx, y + LEVEL, w, L); cx += w[c] + GAPX; }
}

static Layout computeLayout(const Snapshot &s) {
    Layout L;
    if (s.nodes.empty()) return L;
    vector<float> w(s.nodes.size());
    float total = subtreeWidth(s, 0, w);
    L.rect.resize(s.nodes.size());
    L.keyPos.resize(s.nodes.size());
    placeNode(s, 0, -total / 2, 0, w, L);
    float minX = 1e9f, maxX = -1e9f, maxY = 0;
    for (auto &r : L.rect) { minX = std::min(minX, r.x); maxX = std::max(maxX, r.x + r.width); maxY = std::max(maxY, r.y + r.height); }
    L.bounds = {minX, 0, maxX - minX, maxY};
    return L;
}

// identity of a key across snapshots: (value, occurrence in level order)
typedef pair<int, int> KeyId;

static vector<vector<KeyId> > keyIds(const Snapshot &s) {
    vector<vector<KeyId> > ids(s.nodes.size());
    map<int, int> seen;
    for (size_t i = 0; i < s.nodes.size(); ++i)
        for (int k : s.nodes[i].keys) ids[i].push_back({k, seen[k]++});
    return ids;
}

// ============================================================== fonts & drawing helpers
struct Fonts { Font ui, bold, mono; bool owned = false; } F;

static Font loadFont(const char *file, int size) {
    vector<int> cp;
    for (int c = 32; c < 127; ++c) cp.push_back(c);
    cp.push_back(0x00B7); cp.push_back(0x2013); cp.push_back(0x2264); cp.push_back(0x2265); cp.push_back(0x2190); cp.push_back(0x2192);
    const char *dirs[] = {"assets/fonts/", "gui/assets/fonts/", "../gui/assets/fonts/"};
    for (const char *d : dirs) {
        string p = string(d) + file;
        if (FileExists(p.c_str())) {
            Font f = LoadFontEx(p.c_str(), size, cp.data(), (int)cp.size());
            GenTextureMipmaps(&f.texture);
            SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
            return f;
        }
        string q = string(GetApplicationDirectory()) + p;
        if (FileExists(q.c_str())) {
            Font f = LoadFontEx(q.c_str(), size, cp.data(), (int)cp.size());
            GenTextureMipmaps(&f.texture);
            SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
            return f;
        }
    }
    return GetFontDefault();
}

static void text(const Font &f, const string &s, float x, float y, float size, Color c) {
    DrawTextEx(f, s.c_str(), {std::round(x), std::round(y)}, size, 0, c);
}
static float textW(const Font &f, const string &s, float size) { return MeasureTextEx(f, s.c_str(), size, 0).x; }

static void rrect(Rectangle r, float radius, Color c) {
    float m = std::min(r.width, r.height);
    if (m <= 0) return;
    DrawRectangleRounded(r, std::min(1.f, 2 * radius / m), 12, c);
}
static void rline(Rectangle r, float radius, float thick, Color c) {
    float m = std::min(r.width, r.height);
    if (m <= 0) return;
    DrawRectangleRoundedLinesEx(r, std::min(1.f, 2 * radius / m), 12, thick, c);
}
static Rectangle grow(Rectangle r, float d) { return {r.x - d, r.y - d, r.width + 2 * d, r.height + 2 * d}; }

static vector<string> wrap(const Font &f, const string &s, float size, float maxW) {
    vector<string> lines;
    std::istringstream in(s);
    string word, line;
    while (in >> word) {
        string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && textW(f, tryLine, size) > maxW) { lines.push_back(line); line = word; }
        else line = tryLine;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

static float easeInOut(float x) { x = Clamp(x, 0, 1); return x < .5f ? 4 * x * x * x : 1 - std::pow(-2 * x + 2, 3) / 2; }

// ============================================================== widgets
static bool g_hand = false;

static bool button(Rectangle r, const string &label, Color accent, bool primary = false, bool enabled = true, bool active = false) {
    Vector2 m = GetMousePosition();
    bool hover = enabled && CheckCollisionPointRec(m, r);
    bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (hover) g_hand = true;
    Color fill = primary ? accent : (active ? ColorAlpha(accent, .22f) : Color{27, 37, 64, 255});
    if (hover) fill = primary ? ColorBrightness(accent, .12f) : ColorBrightness(fill, .15f);
    if (down) fill = ColorBrightness(fill, -.15f);
    if (!enabled) fill = ColorAlpha(fill, .4f);
    rrect(r, 9, fill);
    if (!primary) rline(r, 9, 1, active ? accent : (hover ? ColorAlpha(accent, .7f) : theme::panelEdge));
    Color tc = primary ? theme::ink : (active ? accent : theme::text);
    if (!enabled) tc = ColorAlpha(tc, .45f);
    float s = 17;
    text(F.bold, label, r.x + (r.width - textW(F.bold, label, s)) / 2, r.y + (r.height - s) / 2 - 1, s, tc);
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

enum Icon { IC_PLAY, IC_PAUSE, IC_PREV, IC_NEXT, IC_END, IC_START };

static bool iconButton(Rectangle r, int icon, bool primary) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    if (hover) g_hand = true;
    Color fill = primary ? theme::amber : Color{27, 37, 64, 255};
    if (hover) fill = ColorBrightness(fill, primary ? .1f : .18f);
    if (hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) fill = ColorBrightness(fill, -.15f);
    rrect(r, 9, fill);
    if (!primary) rline(r, 9, 1, hover ? ColorAlpha(theme::amber, .7f) : theme::panelEdge);
    Color c = primary ? theme::ink : theme::text;
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2, s = 7;
    switch (icon) {
        case IC_PLAY:  DrawTriangle({cx - s + 2, cy - s - 1}, {cx - s + 2, cy + s + 1}, {cx + s + 2, cy}, c); break;
        case IC_PAUSE: DrawRectangleRec({cx - 6, cy - 7, 4, 14}, c); DrawRectangleRec({cx + 2, cy - 7, 4, 14}, c); break;
        case IC_NEXT:  DrawTriangle({cx - 5, cy - 6}, {cx - 5, cy + 6}, {cx + 5, cy}, c); break;
        case IC_PREV:  DrawTriangle({cx + 5, cy - 6}, {cx - 5, cy}, {cx + 5, cy + 6}, c); break;
        case IC_END:   DrawTriangle({cx - 7, cy - 6}, {cx - 7, cy + 6}, {cx + 3, cy}, c); DrawRectangleRec({cx + 4, cy - 6, 3, 12}, c); break;
        case IC_START: DrawTriangle({cx + 7, cy - 6}, {cx - 3, cy}, {cx + 7, cy + 6}, c); DrawRectangleRec({cx - 7, cy - 6, 3, 12}, c); break;
    }
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static void slider(Rectangle r, float *v, bool *dragging) {
    Vector2 m = GetMousePosition();
    Rectangle hit = grow(r, 8);
    if (CheckCollisionPointRec(m, hit)) { g_hand = true; if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) *dragging = true; }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) *dragging = false;
    if (*dragging) *v = Clamp((m.x - r.x) / r.width, 0, 1);
    rrect(r, r.height / 2, Color{30, 41, 70, 255});
    rrect({r.x, r.y, r.width * *v, r.height}, r.height / 2, theme::amber);
    DrawCircleV({r.x + r.width * *v, r.y + r.height / 2}, 9, theme::text);
    DrawCircleV({r.x + r.width * *v, r.y + r.height / 2}, 5, theme::amber);
}

// ============================================================== application
struct KeyAnim {
    Vector2 from{}, pos{}, to{};
    float alpha = 1, alphaFrom = 1, alphaTo = 1;
    bool alive = true;
};

struct App {
    int order = 3;
    bt::BTree *tree = nullptr;
    Recorder rec;
    vector<Snapshot> frames;
    int cur = -1;                 // frame being shown
    bool playing = true;
    float speed = .55f;
    bool speedDrag = false;
    double changedAt = 0;         // when `cur` last changed
    Layout layout;
    vector<vector<KeyId> > ids;
    map<KeyId, KeyAnim> anims;
    Vector2 probeFrom{}, probePos{}, probeTo{};
    float probeAlpha = 0, probeAlphaFrom = 0, probeAlphaTo = 0;

    string input = "10 20 5 6 12 30 7 17 3 4 2 25";
    bool inputFocus = true;
    string toast;
    double toastAt = -10;

    Camera2D cam{};
    bool autoFit = true;
    bool dragging = false;

    float duration() const { return Lerp(.95f, .2f, speed); }
    float hold() const { return Lerp(1.5f, .15f, speed); }

    static string orderRule(int m) {
        int mx = m - 1, mn = (m + 1) / 2 - 1;
        return "Order " + std::to_string(m) + ": a node has at most " + std::to_string(m) + " children and " +
               std::to_string(mx) + " keys; every node except the root has at least " + std::to_string(mn) +
               (mn == 1 ? " key." : " keys.");
    }

    // Change the order. Keys already in the tree are rebuilt into the new shape without animation.
    void setOrder(int m) {
        vector<int> keep = tree ? tree->keys() : vector<int>();
        delete tree;
        order = m;
        tree = new bt::BTree(order);
        for (int k : keep) tree->insert(k);
        rec.out = &frames;
        rec.attach(tree);
        rec.begin(OP_INFO, "Order m = " + std::to_string(order), 0);
        rec.record(orderRule(order) + (keep.empty() ? "" : " The current keys were rebuilt for this order."));
        jumpTo((int)frames.size() - 1);
    }

    // Start from an empty tree and insert the typed values one by one, animating every step.
    void build() {
        vector<int> keys;
        if (!parseInput(keys)) return;
        tree->clear();
        rec.begin(OP_INFO, "Build a B-Tree of order " + std::to_string(order), 0);
        string list;
        for (size_t i = 0; i < keys.size(); ++i) list += (i ? ", " : "") + std::to_string(keys[i]);
        rec.record(orderRule(order) + " Values to insert, in this order: " + list + ".");
        int first = (int)frames.size() - 1;
        for (int k : keys) { rec.begin(OP_INSERT, "Insert " + std::to_string(k), k); tree->insert(k); }
        playing = true;
        autoFit = true;
        jumpTo(first);
    }

    void jumpTo(int i) {
        if (frames.empty()) return;
        i = Clamp(i, 0, (int)frames.size() - 1);
        cur = i;
        changedAt = GetTime();
        const Snapshot &s = frames[cur];
        layout = computeLayout(s);
        ids = keyIds(s);
        for (auto &a : anims) { a.second.alive = false; }
        for (size_t n = 0; n < s.nodes.size(); ++n)
            for (size_t k = 0; k < s.nodes[n].keys.size(); ++k) {
                KeyId id = ids[n][k];
                Vector2 target = layout.keyPos[n][k];
                auto it = anims.find(id);
                if (it == anims.end()) {
                    KeyAnim a;
                    a.from = a.pos = a.to = target;
                    a.from.y -= 26;           // new keys drop in from slightly above
                    a.alpha = a.alphaFrom = 0; a.alphaTo = 1;
                    anims[id] = a;
                } else {
                    KeyAnim &a = it->second;
                    a.from = a.pos; a.to = target;
                    a.alphaFrom = a.alpha; a.alphaTo = 1;
                    a.alive = true;
                }
            }
        for (auto &a : anims)
            if (!a.second.alive) { a.second.from = a.second.pos; a.second.to = a.second.pos; a.second.to.y += 18; a.second.alphaFrom = a.second.alpha; a.second.alphaTo = 0; }

        // probe bubble follows the node being visited
        int visit = -1;
        for (auto &h : s.nodeHl) if (h.second == HL_VISIT) { visit = h.first; break; }
        bool showProbe = visit >= 0 && (s.kind == OP_INSERT || s.kind == OP_SEARCH || s.kind == OP_DELETE);
        probeFrom = probePos; probeAlphaFrom = probeAlpha;
        if (showProbe) {
            Rectangle r = layout.rect[visit];
            probeTo = {r.x + r.width / 2, r.y - 34};
            if (probeAlpha < .05f) probeFrom = {probeTo.x, probeTo.y - 30};
            probeAlphaTo = 1;
        } else probeAlphaTo = 0;
    }

    void runOps(int kind, const vector<int> &keys) {
        bool atEnd = cur >= (int)frames.size() - 1;
        int first = (int)frames.size();
        for (int k : keys) {
            string ks = std::to_string(k);
            if (kind == OP_INSERT) { rec.begin(kind, "Insert " + ks, k); tree->insert(k); }
            else if (kind == OP_DELETE) { rec.begin(kind, "Delete " + ks, k); tree->remove(k); }
            else { rec.begin(kind, "Search " + ks, k); tree->search(k); }
        }
        if (atEnd && first < (int)frames.size()) { playing = true; if (cur < first) jumpTo(first); }
    }

    void say(const string &s) { toast = s; toastAt = GetTime(); }

    bool parseInput(vector<int> &out) {
        string s = input;
        for (char &c : s) if (c == ',' || c == ';') c = ' ';
        std::istringstream in(s);
        string tok;
        while (in >> tok) {
            char *end = nullptr;
            long v = std::strtol(tok.c_str(), &end, 10);
            if (*end || v < -999 || v > 9999) { say("Use whole numbers from -999 to 9999, separated by spaces."); return false; }
            out.push_back((int)v);
        }
        if (out.empty()) { say("Type one or more numbers first, for example 15 42 8."); return false; }
        return true;
    }

    void act(int kind) {
        vector<int> keys;
        if (!parseInput(keys)) return;
        if (kind == OP_SEARCH && keys.size() > 1) keys.resize(1);
        runOps(kind, keys);
        input.clear();
    }

    void randomKeys(int n) {
        vector<int> keys;
        int guard = 0;
        while ((int)keys.size() < n && guard++ < 1000) {
            int k = GetRandomValue(1, 99);
            if (!tree->contains(k) && std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
        }
        if (keys.empty()) { say("No free keys left between 1 and 99."); return; }
        runOps(OP_INSERT, keys);
    }

    void demo() {
        input = "10 20 5 6 12 30 7 17 3 4 2 25 27 40 50 1";
        build();
        runOps(OP_SEARCH, {17});
        runOps(OP_DELETE, {6, 20, 5, 10, 1, 27});
    }

    void clearTree() {
        tree->clear();
        rec.begin(OP_INFO, "Cleared", 0);
        rec.record("The tree is empty. Type values and press Build & animate.");
        jumpTo((int)frames.size() - 1);
    }

    // ------------------------------------------------------------ per-frame update
    void update(Rectangle view) {
        double now = GetTime();
        float p = easeInOut((float)((now - changedAt) / duration()));
        for (auto &kv : anims) {
            KeyAnim &a = kv.second;
            a.pos = Vector2Lerp(a.from, a.to, p);
            a.alpha = Lerp(a.alphaFrom, a.alphaTo, p);
        }
        for (auto it = anims.begin(); it != anims.end();)
            if (!it->second.alive && p >= 1) it = anims.erase(it); else ++it;
        probePos = Vector2Lerp(probeFrom, probeTo, p);
        probeAlpha = Lerp(probeAlphaFrom, probeAlphaTo, p);

        if (playing && cur < (int)frames.size() - 1 && now - changedAt > duration() + hold()) jumpTo(cur + 1);

        // camera: fit the target layout into the view unless the user moved it
        Vector2 mouse = GetMousePosition();
        bool inView = CheckCollisionPointRec(mouse, view);
        float wheel = GetMouseWheelMove();
        if (inView && wheel != 0) {
            Vector2 before = GetScreenToWorld2D(mouse, cam);
            cam.zoom = Clamp(cam.zoom * (1 + wheel * .1f), .2f, 3.f);
            Vector2 after = GetScreenToWorld2D(mouse, cam);
            cam.target = Vector2Add(cam.target, Vector2Subtract(before, after));
            autoFit = false;
        }
        if (inView && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) dragging = true;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) dragging = false;
        if (dragging) {
            Vector2 d = GetMouseDelta();
            if (Vector2Length(d) > 0) { cam.target = Vector2Subtract(cam.target, Vector2Scale(d, 1 / cam.zoom)); autoFit = false; }
        }
        cam.offset = {view.x + view.width / 2, view.y + view.height / 2};
        if (autoFit) {
            Rectangle b = layout.bounds;
            if (b.width <= 0) b = {-100, 0, 200, NH};
            float topRoom = 70, bottomRoom = 40;
            float zx = (view.width - 80) / b.width;
            float zy = (view.height - topRoom - bottomRoom) / (b.height + 40);
            float z = Clamp(std::min(zx, zy), .25f, 1.35f);
            Vector2 target = {b.x + b.width / 2, b.y + b.height / 2 - (topRoom - bottomRoom) / 2 / z};
            float k = 1 - std::exp(-6 * GetFrameTime());
            cam.zoom = Lerp(cam.zoom, z, k);
            cam.target = Vector2Lerp(cam.target, target, k);
        }
    }

    // ------------------------------------------------------------ tree drawing
    void drawTree() {
        if (cur < 0) return;
        const Snapshot &s = frames[cur];
        double now = GetTime();
        float pulse = .5f + .5f * std::sin((float)now * 5);

        // animated rectangles from animated key positions
        vector<Rectangle> rect(s.nodes.size());
        vector<float> alpha(s.nodes.size(), 1);
        for (size_t n = 0; n < s.nodes.size(); ++n) {
            if (s.nodes[n].keys.empty()) { rect[n] = layout.rect[n]; continue; }
            float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, a = 0;
            for (const KeyId &id : ids[n]) {
                const KeyAnim &k = anims[id];
                minX = std::min(minX, k.pos.x); maxX = std::max(maxX, k.pos.x);
                minY = std::min(minY, k.pos.y); maxY = std::max(maxY, k.pos.y);
                a = std::max(a, k.alpha);
            }
            rect[n] = {minX - KW / 2 - PAD, minY - NH / 2, maxX - minX + KW + 2 * PAD, maxY - minY + NH};
            alpha[n] = a;
        }

        // edges
        for (size_t n = 0; n < s.nodes.size(); ++n) {
            const SNode &node = s.nodes[n];
            for (size_t c = 0; c < node.kids.size(); ++c) {
                int kid = node.kids[c];
                float ax;
                if (node.keys.empty()) ax = rect[n].x + rect[n].width / 2;
                else if (c == 0) ax = anims[ids[n][0]].pos.x - KW / 2;
                else if (c == node.keys.size()) ax = anims[ids[n].back()].pos.x + KW / 2;
                else ax = (anims[ids[n][c - 1]].pos.x + anims[ids[n][c]].pos.x) / 2;
                Vector2 a = {ax, rect[n].y + rect[n].height};
                Vector2 b = {rect[kid].x + rect[kid].width / 2, rect[kid].y};
                float my = (a.y + b.y) / 2;
                bool hot = s.nodeHl.count(kid) && s.nodeHl.at(kid) == HL_NEXT;
                Color col = hot ? theme::cyan : theme::edge;
                DrawCircleV(a, 3, col);
                DrawSplineSegmentBezierCubic(a, {a.x, my}, {b.x, my}, b, hot ? 3.f : 2.f, ColorAlpha(col, std::min(alpha[n], alpha[kid])));
            }
        }

        // nodes
        for (size_t n = 0; n < s.nodes.size(); ++n) {
            Rectangle r = rect[n];
            int hl = s.nodeHl.count((int)n) ? s.nodeHl.at((int)n) : HL_NONE;
            Color edge = hlColor(hl);
            float a = alpha[n];
            if (hl != HL_NONE) {
                rrect(grow(r, 7 + 3 * pulse), 16, ColorAlpha(edge, .10f * a));
                rrect(grow(r, 3), 13, ColorAlpha(edge, .18f * a));
            }
            rrect({r.x, r.y + 4, r.width, r.height}, 11, ColorAlpha(BLACK, .35f * a));
            Color fill = hl != HL_NONE ? ColorLerp(theme::node, edge, .09f) : theme::node;
            rrect(r, 11, ColorAlpha(fill, a));
            rline(r, 11, hl != HL_NONE ? 2.5f : 1.5f, ColorAlpha(edge, a));
            if (s.nodes[n].keys.empty()) {
                text(F.ui, "empty", r.x + (r.width - textW(F.ui, "empty", 15)) / 2, r.y + r.height / 2 - 9, 15, theme::faint);
                continue;
            }
            // separators between keys
            for (size_t k = 1; k < ids[n].size(); ++k) {
                float x = (anims[ids[n][k - 1]].pos.x + anims[ids[n][k]].pos.x) / 2;
                float y = (anims[ids[n][k - 1]].pos.y + anims[ids[n][k]].pos.y) / 2;
                DrawLineEx({x, y - NH / 2 + 12}, {x, y + NH / 2 - 12}, 1.f, ColorAlpha(theme::nodeEdge, a));
            }
        }

        // keys (live and fading)
        map<KeyId, pair<int, int> > where;
        for (size_t n = 0; n < s.nodes.size(); ++n)
            for (size_t k = 0; k < ids[n].size(); ++k) where[ids[n][k]] = {(int)n, (int)k};
        for (auto &kv : anims) {
            const KeyAnim &k = kv.second;
            if (k.alpha <= .01f) continue;
            int khl = HL_NONE;
            auto w = where.find(kv.first);
            if (w != where.end()) {
                auto f = s.keyHl.find(w->second);
                if (f != s.keyHl.end()) khl = f->second;
            }
            Rectangle cell = {k.pos.x - KW / 2 + 3, k.pos.y - NH / 2 + 7, KW - 6, NH - 14};
            Color tc = theme::text;
            if (!kv.second.alive) {
                rrect(cell, 8, ColorAlpha(theme::red, .35f * k.alpha));
            }
            if (khl != HL_NONE) {
                Color c = hlColor(khl);
                if (khl == HL_FOUND) rrect(grow(cell, 4 + 4 * pulse), 12, ColorAlpha(c, .25f * k.alpha));
                rrect(cell, 8, ColorAlpha(c, k.alpha));
                tc = theme::ink;
            }
            string label = std::to_string(kv.first.first);
            float size = label.size() > 3 ? 18.f : 22.f;
            float tw = textW(F.mono, label, size);
            text(F.mono, label, k.pos.x - tw / 2, k.pos.y - size / 2 - 1, size, ColorAlpha(tc, k.alpha));
        }

        // probe bubble: the key the operation is looking for
        if (probeAlpha > .02f) {
            string label = std::to_string(s.probe);
            float size = 20, tw = textW(F.mono, label, size);
            Rectangle b = {probePos.x - tw / 2 - 14, probePos.y - 16, tw + 28, 32};
            Color c = opColor(s.kind);
            rrect(grow(b, 3), 18, ColorAlpha(c, .18f * probeAlpha));
            rrect(b, 16, ColorAlpha(c, probeAlpha));
            DrawTriangle({probePos.x - 7, b.y + b.height - 1}, {probePos.x, b.y + b.height + 8}, {probePos.x + 7, b.y + b.height - 1}, ColorAlpha(c, probeAlpha));
            text(F.mono, label, probePos.x - tw / 2, probePos.y - size / 2 - 1, size, ColorAlpha(theme::ink, probeAlpha));
        }
    }

    // ------------------------------------------------------------ UI
    void draw() {
        g_hand = false;
        int W = GetScreenWidth(), H = GetScreenHeight();
        bool narrow = W < 900;
        float panelW = narrow ? 0 : 360;
        float topH = 72;
        Rectangle view = {0, topH, W - panelW, (float)H - topH};

        update(view);
        handleKeys();

        DrawRectangleGradientV(0, 0, W, H, theme::bgTop, theme::bgBottom);

        // dotted grid in world space
        BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
        BeginMode2D(cam);
        {
            Vector2 tl = GetScreenToWorld2D({view.x, view.y}, cam), br = GetScreenToWorld2D({view.x + view.width, view.y + view.height}, cam);
            float step = 40;
            if (cam.zoom > .35f)
                for (float x = std::floor(tl.x / step) * step; x < br.x; x += step)
                    for (float y = std::floor(tl.y / step) * step; y < br.y; y += step)
                        DrawCircleV({x, y}, 1.2f / cam.zoom, Color{255, 255, 255, 14});
            drawTree();
        }
        EndMode2D();
        if (cur >= 0 && frames[cur].nodes.empty()) {
            string e = "The tree is empty";
            text(F.bold, e, view.x + (view.width - textW(F.bold, e, 26)) / 2, view.y + view.height / 2 - 40, 26, theme::muted);
            string h = "Type values, pick the order and press Build & animate.";
            text(F.ui, h, view.x + (view.width - textW(F.ui, h, 18)) / 2, view.y + view.height / 2, 18, theme::faint);
        }
        drawLegend(view);
        drawMessage(view);
        EndScissorMode();

        drawTopBar(W, topH);
        if (!narrow) drawPanel({(float)W - panelW, topH, panelW, (float)H - topH});

        // toast
        if (GetTime() - toastAt < 3) {
            float a = Clamp((float)(3 - (GetTime() - toastAt)) * 2, 0, 1);
            float tw = textW(F.ui, toast, 17);
            Rectangle r = {view.x + (view.width - tw) / 2 - 18, topH + 16, tw + 36, 40};
            rrect(r, 10, ColorAlpha(Color{60, 24, 34, 255}, a));
            rline(r, 10, 1, ColorAlpha(theme::red, a));
            text(F.ui, toast, r.x + 18, r.y + 10, 17, ColorAlpha(theme::text, a));
        }
        SetMouseCursor(g_hand ? MOUSE_CURSOR_POINTING_HAND : (dragging ? MOUSE_CURSOR_RESIZE_ALL : MOUSE_CURSOR_DEFAULT));
    }

    void handleKeys() {
        int c = GetCharPressed();
        while (c > 0) {
            if (inputFocus && input.size() < 160 && ((c >= '0' && c <= '9') || c == ' ' || c == ',' || c == '-')) input.push_back((char)c);
            else if (!inputFocus || input.empty()) {
                if (c == 'f' || c == 'F') autoFit = true;
            }
            c = GetCharPressed();
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !input.empty()) input.pop_back();
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) act(OP_INSERT);
        if (IsKeyPressed(KEY_DELETE)) act(OP_DELETE);
        if (IsKeyPressed(KEY_SPACE) && input.empty()) togglePlay();
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) { playing = false; jumpTo(cur + 1); }
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) { playing = false; jumpTo(cur - 1); }
        if (IsKeyPressed(KEY_ESCAPE)) input.clear();
    }

    void togglePlay() {
        if (!playing && cur >= (int)frames.size() - 1) {
            int i = cur;   // replay the latest operation from its first step
            while (i > 0 && frames[i - 1].opId == frames[cur].opId) --i;
            jumpTo(i);
        }
        playing = !playing;
    }

    void drawTopBar(int W, float h) {
        DrawRectangle(0, 0, W, (int)h, ColorAlpha(theme::bgTop, .92f));
        DrawLine(0, (int)h, W, (int)h, theme::panelEdge);
        // logo: three linked boxes
        float x = 24, y = h / 2;
        rrect({x, y - 14, 20, 12}, 4, theme::amber);
        rrect({x - 6, y + 4, 14, 10}, 3, theme::cyan);
        rrect({x + 12, y + 4, 14, 10}, 3, theme::cyan);
        text(F.bold, "B-Tree Visualizer", x + 40, y - 21, 24, theme::text);
        string sub = "Insert, search and delete, step by step";
        text(F.ui, sub, x + 40, y + 6, 15, theme::muted);

        if (cur < 0) return;
        const Snapshot &s = frames[cur];
        int nodes = (int)s.nodes.size();
        struct Chip { string label, value; Color c; };
        vector<Chip> chips = {
            {"order", std::to_string(order), theme::amber},
            {"keys / node", std::to_string(tree->minKeys()) + "-" + std::to_string(tree->maxKeys()), theme::muted},
            {"keys", std::to_string(s.keyCount), theme::muted},
            {"nodes", std::to_string(nodes), theme::muted},
            {"height", std::to_string(s.height), theme::muted},
        };
        float cx = W - 24.f;
        if (W < 760) return;
        for (int i = (int)chips.size() - 1; i >= 0; --i) {
            float lw = textW(F.ui, chips[i].label, 14), vw = textW(F.bold, chips[i].value, 18);
            float w = lw + vw + 30;
            cx -= w;
            Rectangle r = {cx, y - 17, w, 34};
            rrect(r, 17, Color{24, 33, 58, 255});
            rline(r, 17, 1, theme::panelEdge);
            text(F.ui, chips[i].label, r.x + 12, y - 8, 14, theme::muted);
            text(F.bold, chips[i].value, r.x + 18 + lw, y - 10, 18, i == 0 ? theme::amber : theme::text);
            cx -= 8;
        }
    }

    void drawLegend(Rectangle view) {
        struct L { const char *t; Color c; };
        L items[] = {{"visiting", theme::amber}, {"full / problem", theme::red}, {"next", theme::cyan},
                     {"new or moved key", theme::green}, {"compared", theme::violet}};
        float x = view.x + 20, y = view.y + 18;
        for (auto &it : items) {
            float w = textW(F.ui, it.t, 14);
            if (x + w + 30 > view.x + view.width - 10) break;
            rrect({x, y + 2, 12, 12}, 3, it.c);
            text(F.ui, it.t, x + 18, y, 14, theme::muted);
            x += w + 36;
        }
        string hint = autoFit ? "drag to pan, scroll to zoom" : "press F to fit the tree";
        float hw = textW(F.ui, hint, 14);
        if (x + hw + 40 < view.x + view.width) text(F.ui, hint, view.x + view.width - hw - 20, y, 14, theme::faint);
    }

    void drawMessage(Rectangle view) {
        if (cur < 0) return;
        const Snapshot &s = frames[cur];
        float maxW = std::min(view.width - 40, 820.f);
        vector<string> lines = wrap(F.ui, s.msg, 19, maxW - 40);
        float h = 58 + lines.size() * 26 + 18;
        Rectangle r = {view.x + (view.width - maxW) / 2, view.y + view.height - h - 20, maxW, h};
        rrect({r.x, r.y + 6, r.width, r.height}, 14, ColorAlpha(BLACK, .35f));
        rrect(r, 14, ColorAlpha(theme::panel, .97f));
        rline(r, 14, 1, theme::panelEdge);

        Color oc = opColor(s.kind);
        float bw = textW(F.bold, s.op, 15) + 24;
        rrect({r.x + 20, r.y + 16, bw, 28}, 14, ColorAlpha(oc, .18f));
        text(F.bold, s.op, r.x + 32, r.y + 21, 15, oc);

        // step counter within the operation
        int first = cur, last = cur;
        while (first > 0 && frames[first - 1].opId == s.opId) --first;
        while (last + 1 < (int)frames.size() && frames[last + 1].opId == s.opId) ++last;
        string step = "step " + std::to_string(cur - first + 1) + " of " + std::to_string(last - first + 1);
        text(F.ui, step, r.x + 32 + bw, r.y + 22, 15, theme::muted);
        string queue = std::to_string((int)frames.size() - 1 - cur) + " steps queued";
        if (cur < (int)frames.size() - 1)
            text(F.ui, queue, r.x + r.width - 20 - textW(F.ui, queue, 15), r.y + 22, 15, theme::faint);

        float y = r.y + 58;
        for (auto &l : lines) { text(F.ui, l, r.x + 20, y, 19, theme::text); y += 26; }

        // progress of this step's animation + hold
        float prog = Clamp((float)((GetTime() - changedAt) / (duration() + hold())), 0, 1);
        if (!playing || cur >= (int)frames.size() - 1) prog = 1;
        Rectangle bar = {r.x + 20, r.y + r.height - 10, r.width - 40, 3};
        rrect(bar, 1.5f, Color{30, 41, 70, 255});
        rrect({bar.x, bar.y, bar.width * ((cur - first) + prog) / (last - first + 1), bar.height}, 1.5f, oc);
    }

    void drawPanel(Rectangle p) {
        DrawRectangleRec(p, theme::panel);
        DrawLine((int)p.x, (int)p.y, (int)p.x, (int)(p.y + p.height), theme::panelEdge);
        float x = p.x + 22, w = p.width - 44, y = p.y + 22;

        auto label = [&](const string &s) { text(F.bold, s, x, y, 13, theme::muted); y += 22; };

        label("VALUES");
        Rectangle box = {x, y, w, 48};
        bool hover = CheckCollisionPointRec(GetMousePosition(), box);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) inputFocus = hover || CheckCollisionPointRec(GetMousePosition(), p);
        rrect(box, 10, theme::field);
        rline(box, 10, inputFocus ? 2.f : 1.f, inputFocus ? theme::amber : theme::panelEdge);
        {
            // long lists scroll so the end (where you type) stays visible
            float inner = w - 32, tw = input.empty() ? 0 : textW(F.mono, input, 20);
            float shift = std::max(0.f, tw + 4 - inner);
            BeginScissorMode((int)(x + 14), (int)y, (int)(inner + 4), 48);
            if (input.empty()) text(F.ui, "e.g. 10 20 5 6 12 30", x + 22, y + 13, 19, theme::faint);
            else text(F.mono, input, x + 16 - shift, y + 13, 20, theme::text);
            if (inputFocus && std::fmod(GetTime(), 1.0) < .55)
                DrawRectangleRec({x + 16 + (input.empty() ? 0 : tw + 2) - shift, y + 12, 2, 24}, theme::amber);
            EndScissorMode();
        }
        y += 56;
        text(F.ui, "Type keys in the order to insert them.", x, y, 14, theme::faint);
        y += 28;

        label("ORDER m");
        if (button({x, y, 42, 40}, "-", theme::amber, false, order > 3) && order > 3) setOrder(order - 1);
        Rectangle ob = {x + 50, y, 56, 40};
        rrect(ob, 9, theme::field);
        rline(ob, 9, 1, theme::panelEdge);
        string os = std::to_string(order);
        text(F.mono, os, ob.x + (ob.width - textW(F.mono, os, 22)) / 2, ob.y + 8, 22, theme::amber);
        if (button({x + 114, y, 42, 40}, "+", theme::amber, false, order < 10) && order < 10) setOrder(order + 1);
        int mn = (order + 1) / 2 - 1;
        text(F.ui, "max " + std::to_string(order - 1) + " keys / node", x + 170, y + 2, 14, theme::muted);
        text(F.ui, "min " + std::to_string(mn) + (mn == 1 ? " key" : " keys") + " (not root)", x + 170, y + 21, 14, theme::muted);
        y += 54;

        if (button({x, y, w, 46}, "Build & animate", theme::amber, true)) build();
        y += 56;

        float bw = (w - 16) / 3;
        if (button({x, y, bw, 40}, "Insert", theme::green, false, true, true)) act(OP_INSERT);
        if (button({x + bw + 8, y, bw, 40}, "Delete", theme::red, false, true, true)) act(OP_DELETE);
        if (button({x + 2 * (bw + 8), y, bw, 40}, "Search", theme::blue, false, true, true)) act(OP_SEARCH);
        y += 48;
        if (button({x, y, bw, 36}, "+5 random", theme::amber)) randomKeys(5);
        if (button({x + bw + 8, y, bw, 36}, "Demo", theme::amber)) demo();
        if (button({x + 2 * (bw + 8), y, bw, 36}, "Clear", theme::amber)) clearTree();
        y += 50;

        label("PLAYBACK");
        float ib = 46, gap = (w - 5 * ib) / 4;
        if (iconButton({x, y, ib, 40}, IC_START, false)) { playing = false; jumpTo(0); }
        if (iconButton({x + (ib + gap), y, ib, 40}, IC_PREV, false)) { playing = false; jumpTo(cur - 1); }
        if (iconButton({x + 2 * (ib + gap), y, ib, 40}, playing && cur < (int)frames.size() - 1 ? IC_PAUSE : IC_PLAY, true)) togglePlay();
        if (iconButton({x + 3 * (ib + gap), y, ib, 40}, IC_NEXT, false)) { playing = false; jumpTo(cur + 1); }
        if (iconButton({x + 4 * (ib + gap), y, ib, 40}, IC_END, false)) jumpTo((int)frames.size() - 1);
        y += 56;
        text(F.ui, "Slow", x, y - 3, 14, theme::faint);
        text(F.ui, "Fast", x + w - textW(F.ui, "Fast", 14), y - 3, 14, theme::faint);
        slider({x + 44, y + 3, w - 88, 6}, &speed, &speedDrag);
        y += 34;

        // steps of the current operation
        DrawLine((int)x, (int)y, (int)(x + w), (int)y, theme::panelEdge);
        y += 18;
        label("STEPS OF THIS OPERATION");
        if (cur < 0) return;
        int first = cur, last = cur;
        while (first > 0 && frames[first - 1].opId == frames[cur].opId) --first;
        while (last + 1 < (int)frames.size() && frames[last + 1].opId == frames[cur].opId) ++last;

        float bottom = p.y + p.height - 16;
        struct Item { int idx; vector<string> lines; float h; };
        vector<Item> items;
        for (int i = first; i <= last; ++i) {
            Item it{i, wrap(F.ui, frames[i].msg, 15, w - 36), 0};
            it.h = it.lines.size() * 20 + 12;
            items.push_back(it);
        }
        // scroll so the current step stays visible
        float avail = bottom - y, used = 0;
        int start = 0;
        for (int i = 0; i < (int)items.size(); ++i) {
            used += items[i].h;
            while (items[i].idx <= cur && used > avail && start < i) used -= items[start++].h;
        }
        BeginScissorMode((int)x - 4, (int)y, (int)w + 8, (int)avail);
        for (int i = start; i < (int)items.size(); ++i) {
            const Item &it = items[i];
            bool now = it.idx == cur, past = it.idx < cur;
            Rectangle r = {x - 4, y - 4, w + 8, it.h};
            if (now) { rrect(r, 8, ColorAlpha(opColor(frames[cur].kind), .12f)); }
            Color dot = now ? opColor(frames[cur].kind) : (past ? theme::muted : theme::panelEdge);
            DrawCircleV({x + 8, y + 9}, 4, dot);
            Color tc = now ? theme::text : (past ? theme::muted : theme::faint);
            float ly = y;
            for (auto &l : it.lines) { text(F.ui, l, x + 24, ly, 15, tc); ly += 20; }
            if (CheckCollisionPointRec(GetMousePosition(), r)) {
                g_hand = true;
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { playing = false; jumpTo(it.idx); }
            }
            y += it.h;
            if (y > bottom) break;
        }
        EndScissorMode();
    }
};

static App *g_app = nullptr;

static void frameLoop() {
#ifdef __EMSCRIPTEN__
    int w = EM_ASM_INT({ return window.innerWidth; });
    int h = EM_ASM_INT({ return window.innerHeight; });
    if (w != GetScreenWidth() || h != GetScreenHeight()) SetWindowSize(w, h);
#endif
    BeginDrawing();
    ClearBackground(theme::bgTop);
    g_app->draw();
    EndDrawing();
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
#ifdef __EMSCRIPTEN__
    int w = EM_ASM_INT({ return window.innerWidth; }), h = EM_ASM_INT({ return window.innerHeight; });
    InitWindow(w, h, "B-Tree Visualizer");
#else
    InitWindow(1360, 820, "B-Tree Visualizer");
    SetWindowMinSize(900, 620);
    SetTargetFPS(60);
#endif
    SetExitKey(KEY_NULL);
    SetRandomSeed((unsigned)time(nullptr));

    F.ui = loadFont("IBMPlexSans-Regular.ttf", 48);
    F.bold = loadFont("IBMPlexSans-SemiBold.ttf", 48);
    F.mono = loadFont("IBMPlexMono-SemiBold.ttf", 48);

    App app;
    g_app = &app;
    app.cam.zoom = 1;
    app.setOrder(3);
    app.build();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frameLoop, 0, 1);
#else
    while (!WindowShouldClose()) frameLoop();
    delete app.tree;
    CloseWindow();
#endif
    return 0;
}
