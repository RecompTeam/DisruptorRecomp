#include "gpu_ddgi_bvh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct Random {
    uint32_t state;
    float next(float lo, float hi) {
        state = state * 1664525u + 1013904223u;
        return lo + (hi - lo) * static_cast<float>(state >> 8) / 16777216.0f;
    }
};

GpuDdgiTriangle triangle(float x, float y, float z, float size) {
    GpuDdgiTriangle t{};
    const float corners[3][3] = {{x, y, z}, {x + size, y, z + size * 0.5f}, {x, y + size, z - size * 0.25f}};
    for (int k = 0; k < 3; ++k)
        for (int a = 0; a < 3; ++a) t.world[k][a] = corners[k][a];
    return t;
}

std::vector<GpuDdgiTriangle> scattered(int count, uint32_t seed) {
    Random random{seed};
    std::vector<GpuDdgiTriangle> out;
    for (int i = 0; i < count; ++i)
        out.push_back(triangle(random.next(-10000, 10000), random.next(-2000, 2000), random.next(-10000, 10000), random.next(1, 300)));
    return out;
}

// Faces at powers of two along x, as far apart as a level allows: unbalanced for SAH.
std::vector<GpuDdgiTriangle> crowded(int count) {
    std::vector<GpuDdgiTriangle> out;
    for (int i = 0; i < count; ++i)
        out.push_back(triangle(std::ldexp(1.0f, i % 24), 0.0f, static_cast<float>(3 * (i / 24)), 1.0f));
    return out;
}

struct Tree {
    std::vector<DdgiNode> nodes;
    std::vector<DdgiBuildFace> faces;
    int node_count = 0;
};

enum class Builder { Median, Sah };

Tree build(const std::vector<GpuDdgiTriangle> &tris, Builder builder, int depth = 0) {
    Tree tree;
    tree.nodes.resize(2 * tris.size() + 1);
    tree.faces.resize(tris.size() + 1);
    for (size_t i = 0; i < tris.size(); ++i) psx_ddgi_face_of(&tree.faces[i], &tris[i], static_cast<int>(i));
    DdgiBvhBuild work = {tree.nodes.data(), tree.faces.data(), 1};
    if (builder == Builder::Median) psx_ddgi_median_split(&work, 0, 0, static_cast<int>(tris.size()));
    else psx_ddgi_sah_split(&work, 0, 0, static_cast<int>(tris.size()), depth);
    tree.node_count = work.node_count;
    return tree;
}

// Written so that a NaN on either side fails.
bool box_holds(const float lo[3], const float hi[3], const float in_lo[3], const float in_hi[3]) {
    for (int a = 0; a < 3; ++a)
        if (!(in_lo[a] >= lo[a] && in_hi[a] <= hi[a] && std::isfinite(lo[a]) && std::isfinite(hi[a]))) return false;
    return true;
}

struct Walk {
    bool each_face_once = true, each_node_once = true, boxes_hold = true, leaves_small = true, links_inside = true;
    int depth = 0;
};

bool holds_corners(const DdgiNode &n, const GpuDdgiTriangle &t) {
    for (int k = 0; k < 3; ++k)
        for (int a = 0; a < 3; ++a)
            if (!(t.world[k][a] >= n.lo[a] && t.world[k][a] <= n.hi[a])) return false;
    return true;
}

Walk walk(const Tree &tree, const std::vector<GpuDdgiTriangle> &tris) {
    const size_t count = tris.size();
    Walk out;
    std::vector<int> seen(count, 0), reached(tree.node_count, 0);
    std::vector<std::pair<int, int>> stack = {{0, 0}};
    while (!stack.empty()) {
        const auto [index, depth] = stack.back();
        stack.pop_back();
        out.depth = depth > out.depth ? depth : out.depth;
        if (++reached[index] > 1) {
            out.each_node_once = false;
            continue;
        }
        const DdgiNode &n = tree.nodes[index];
        if (n.count == 0) {
            if (n.link < 1 || n.link + 1 >= tree.node_count) {
                out.links_inside = false;
                continue;
            }
            for (int child = n.link; child <= n.link + 1; ++child) {
                out.boxes_hold = out.boxes_hold && box_holds(n.lo, n.hi, tree.nodes[child].lo, tree.nodes[child].hi);
                stack.push_back({child, depth + 1});
            }
            continue;
        }
        if (n.count < 0 || n.link < 0 || n.link + n.count > static_cast<int>(count)) {
            out.links_inside = false;
            continue;
        }
        float clo[3] = {1e30f, 1e30f, 1e30f}, chi[3] = {-1e30f, -1e30f, -1e30f};
        for (int i = n.link; i < n.link + n.count; ++i) {
            const DdgiBuildFace &f = tree.faces[i];
            if (f.index < 0 || f.index >= static_cast<int>(count)) {
                out.links_inside = false;
                continue;
            }
            ++seen[f.index];
            out.boxes_hold = out.boxes_hold && box_holds(n.lo, n.hi, f.lo, f.hi) && holds_corners(n, tris[f.index]);
            for (int a = 0; a < 3; ++a) clo[a] = std::fmin(clo[a], f.centre[a]), chi[a] = std::fmax(chi[a], f.centre[a]);
        }
        const bool one_place = chi[0] - clo[0] < 1e-3f && chi[1] - clo[1] < 1e-3f && chi[2] - clo[2] < 1e-3f;
        out.leaves_small = out.leaves_small && (n.count <= DDGI_LEAF_FACES || one_place);
    }
    for (const int times : seen) out.each_face_once = out.each_face_once && times == 1;
    for (const int times : reached) out.each_node_once = out.each_node_once && times == 1;
    return out;
}

void check(const char *what, const std::vector<GpuDdgiTriangle> &tris, Builder builder) {
    const Tree tree = build(tris, builder);
    const Walk result = walk(tree, tris);
    const std::string name = std::string(what) + (builder == Builder::Median ? " (median)" : " (SAH)");
    expect(result.each_face_once, (name + ": every face is in exactly one leaf").c_str());
    expect(result.each_node_once, (name + ": every node made is reached once, as the shader walks it: no leaf is empty").c_str());
    expect(result.boxes_hold, (name + ": a node's box holds its children, a leaf's its triangles' corners").c_str());
    expect(result.leaves_small, (name + ": a leaf holds at most four faces, unless they share one centre").c_str());
    expect(result.links_inside, (name + ": an inner node's children are nodes that were made, a leaf's faces are faces").c_str());
    expect(tree.node_count <= static_cast<int>(2 * tris.size()), (name + ": at most two nodes a face").c_str());
    expect(result.depth <= DDGI_LEVEL_DEPTH, (name + ": no leaf deeper than the rays' stack takes").c_str());
}

// The walk itself must refuse a broken tree, and promptly: a loop, a face out of range, a box of NaN.
void walker_refuses() {
    const std::vector<GpuDdgiTriangle> tris = scattered(4, 7);
    Tree tree = build(tris, Builder::Median);
    expect(walk(tree, tris).each_node_once, "the walk takes a built tree");
    Tree loop;
    loop.faces = tree.faces;
    loop.nodes = {DdgiNode{{-1e9f, -1e9f, -1e9f}, {1e9f, 1e9f, 1e9f}, 1, 0}, DdgiNode{{-1e9f, -1e9f, -1e9f}, {1e9f, 1e9f, 1e9f}, 1, 0},
                  DdgiNode{{-1e9f, -1e9f, -1e9f}, {1e9f, 1e9f, 1e9f}, 0, 4}};
    loop.node_count = 3;
    expect(!walk(loop, tris).each_node_once, "a node that is its own child is caught, not walked for ever");
    Tree outside = tree;
    outside.faces[0].index = 99;
    expect(!walk(outside, tris).links_inside, "a face index past the faces is caught before it is used");
    Tree nan = tree;
    nan.nodes[0].hi[1] = std::nanf("");
    expect(!walk(nan, tris).boxes_hold, "a box of NaN holds nothing");
}

void builds() {
    for (const Builder builder : {Builder::Median, Builder::Sah}) {
        check("one face", scattered(1, 1), builder);
        check("four faces", scattered(4, 2), builder);
        check("five faces", scattered(5, 3), builder);
        check("a room", scattered(700, 4), builder);
        check("a level", scattered(20000, 5), builder);
        check("faces crowded to a corner", crowded(3000), builder);
        std::vector<GpuDdgiTriangle> stacked(50, triangle(10.0f, 20.0f, 30.0f, 40.0f));
        check("faces on one place", stacked, builder);
    }
    const Tree sah = build(std::vector<GpuDdgiTriangle>(50, triangle(10.0f, 20.0f, 30.0f, 40.0f)), Builder::Sah);
    bool small = true;
    for (int i = 0; i < sah.node_count; ++i) small = small && sah.nodes[i].count <= DDGI_LEAF_FACES;
    expect(small, "the level's build splits even faces on one place, by halves");
    const Tree one_leaf = build(std::vector<GpuDdgiTriangle>(50, triangle(10.0f, 20.0f, 30.0f, 40.0f)), Builder::Median);
    expect(one_leaf.node_count == 1 && one_leaf.nodes[0].count == 50, "the frame's build keeps faces on one place in one leaf");
    const std::vector<GpuDdgiTriangle> level = scattered(3000, 6);
    const Walk deep = walk(build(level, Builder::Sah, DDGI_LEVEL_DEPTH - 10), level);
    expect(deep.each_face_once && deep.depth <= 10, "near the stack's depth it splits by medians: 3000 faces in 10 more levels");
}

void select_trials() {
    Random random{21};
    bool split = true;
    for (int trial = 0; trial < 3000 && split; ++trial) {
        const int count = 2 + static_cast<int>(random.next(0, 120)), nth = static_cast<int>(random.next(0, static_cast<float>(count)));
        const float values = trial % 2 ? 4.0f : 1000.0f;
        std::vector<DdgiBuildFace> faces(count);
        for (DdgiBuildFace &f : faces) f.centre[0] = std::floor(random.next(0, values));
        psx_ddgi_select(faces.data(), 0, count - 1, nth, 0);
        for (int i = 0; i < count; ++i)
            split = split && (i < nth ? faces[i].centre[0] <= faces[nth].centre[0] : faces[i].centre[0] >= faces[nth].centre[0]);
    }
    expect(split, "the select holds for any count and nth, with many ties and with few");
}

void face_boxes() {
    const float a[3] = {5, -1, 9}, b[3] = {-3, 4, 9}, c[3] = {2, 2, -6};
    GpuDdgiTriangle t{};
    for (int k = 0; k < 3; ++k) t.world[0][k] = a[k], t.world[1][k] = b[k], t.world[2][k] = c[k];
    DdgiBuildFace f{};
    psx_ddgi_face_of(&f, &t, 17);
    expect(f.centre[0] == 4.0f / 3.0f && f.centre[1] == 5.0f / 3.0f && f.centre[2] == 4.0f, "a face's centre is its corners' mean");
    expect(f.lo[0] == -3 && f.lo[1] == -1 && f.lo[2] == -6 && f.hi[0] == 5 && f.hi[1] == 4 && f.hi[2] == 9, "its box spans its corners");
    expect(f.index == 17, "it keeps its place in the list it came from");
}

void select_halves() {
    Random random{9};
    std::vector<DdgiBuildFace> faces(301);
    for (DdgiBuildFace &f : faces)
        for (float &c : f.centre) c = std::floor(random.next(0, 40));
    psx_ddgi_select(faces.data(), 0, 300, 150, 2);
    bool split = true;
    for (int i = 0; i < 150; ++i) split = split && faces[i].centre[2] <= faces[150].centre[2];
    for (int i = 151; i <= 300; ++i) split = split && faces[i].centre[2] >= faces[150].centre[2];
    expect(split, "the median select leaves the smaller centres before the nth and the larger after, ties included");
}

// The shaders' oct_decode of a packed normal.
void decode(uint16_t packed, float out[3]) {
    float x = static_cast<float>(packed & 255) / 127.5f - 1.0f, y = static_cast<float>(packed >> 8) / 127.5f - 1.0f;
    float z = 1.0f - std::fabs(x) - std::fabs(y);
    if (z < 0.0f) {
        const float nx = (1.0f - std::fabs(y)) * (x >= 0.0f ? 1.0f : -1.0f), ny = (1.0f - std::fabs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        x = nx;
        y = ny;
    }
    const float length = std::sqrt(x * x + y * y + z * z);
    out[0] = x / length, out[1] = y / length, out[2] = z / length;
}

void packed_normals() {
    Random random{11};
    double worst = 0.0;
    for (int i = 0; i < 20000; ++i) {
        float n[3] = {random.next(-1, 1), random.next(-1, 1), random.next(-1, 1)};
        const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (length < 1e-3f) continue;
        for (float &c : n) c /= length;
        float back[3];
        decode(psx_ddgi_oct16(n), back);
        const double dot = n[0] * back[0] + n[1] * back[1] + n[2] * back[2];
        if (!std::isfinite(dot)) {
            worst = 10.0;
            break;
        }
        worst = std::fmax(worst, std::acos(std::clamp(dot, -1.0, 1.0)));
    }
    expect(worst < 0.035, "a normal comes back through 16 bits within two degrees");
    const float down[3] = {0.0f, 0.0f, -1.0f}, up[3] = {0.0f, 0.0f, 1.0f};
    float back[3];
    decode(psx_ddgi_oct16(down), back);
    expect(back[2] < -0.999f, "straight down survives the fold");
    decode(psx_ddgi_oct16(up), back);
    expect(back[2] > 0.999f, "straight up survives too");
}

void corner_keys() {
    const float a[3] = {100.4f, -20.0f, 7.6f}, b[3] = {99.6f, -20.2f, 8.4f}, c[3] = {101.6f, -20.0f, 7.6f};
    expect(psx_ddgi_corner_key(a) == psx_ddgi_corner_key(b), "corners within half a unit share a key");
    expect(psx_ddgi_corner_key(a) != psx_ddgi_corner_key(c), "a corner a unit away has its own");
    const float negative[3] = {-1.0f, -1.0f, -1.0f};
    expect(psx_ddgi_corner_key(negative) >> 63, "every key has its top bit, an empty slot's 0 never matches");
}

struct Corners {
    std::vector<float> face;
    std::vector<int> head, next, slot;
    std::vector<uint64_t> key;
    std::vector<uint16_t> normal;
    DdgiCorners work;
    explicit Corners(size_t count)
        : face(count * 4), head(DDGI_CORNER_SLOTS), next(count * 3), slot(count * 3), key(DDGI_CORNER_SLOTS), normal(count * 3) {
        work = {reinterpret_cast<float(*)[4]>(face.data()), head.data(), next.data(), slot.data(), key.data(),
                reinterpret_cast<uint16_t(*)[3]>(normal.data())};
    }
};

GpuDdgiTriangle face(const float a[3], const float b[3], const float c[3]) {
    GpuDdgiTriangle t{};
    for (int k = 0; k < 3; ++k) t.world[0][k] = a[k], t.world[1][k] = b[k], t.world[2][k] = c[k];
    return t;
}

double angle_to(uint16_t packed, const float want[3]) {
    float n[3];
    decode(packed, n);
    const double dot = n[0] * want[0] + n[1] * want[1] + n[2] * want[2];
    return std::isfinite(dot) ? std::acos(std::clamp(dot, -1.0, 1.0)) : 10.0;
}

void smooth_corners() {
    // Ground at y 0 in two halves, a wall at x 0 rising from its edge, a gentle slope beyond z 100, and a face with no area.
    const float o[3] = {0, 0, 0}, x[3] = {100, 0, 0}, z[3] = {0, 0, 100}, xz[3] = {100, 0, 100};
    const float wall_top[3] = {0, -100, 0}, wall_far[3] = {0, -100, 100};
    const float slope_x[3] = {100, -18, 200}, slope_o[3] = {0, -18, 200};
    const std::vector<GpuDdgiTriangle> tris = {face(o, x, xz), face(o, z, xz), face(o, wall_top, z), face(z, wall_top, wall_far),
                                               face(z, xz, slope_x), face(z, slope_x, slope_o), face(o, o, x)};
    Corners corners(tris.size());
    const float eye[3] = {50, -50, 50};
    psx_ddgi_corner_normals(&corners.work, tris.data(), static_cast<int>(tris.size()), eye);
    const float up[3] = {0, -1, 0}, wall[3] = {1, 0, 0};
    expect(angle_to(corners.normal[0 * 3 + 1], up) < 0.02, "a ground corner only ground meets keeps the ground's normal");
    expect(angle_to(corners.normal[0 * 3 + 0], up) < 0.02, "the ground where the wall stands keeps its own: the wall is 90 degrees off");
    expect(angle_to(corners.normal[2 * 3 + 0], wall) < 0.02, "and the wall keeps its own there");
    const double ground_to_slope = std::atan2(18.0, 100.0);
    const double blended = angle_to(corners.normal[1 * 3 + 1], up);
    expect(blended > 0.01 && blended < ground_to_slope - 0.01, "where ground meets a gentle slope the normal lies between the two");
    expect(corners.normal[6 * 3 + 0] == 0 && corners.normal[6 * 3 + 2] == 0, "a face with no area has no normal");
    expect(angle_to(corners.normal[1 * 3 + 0], up) < 0.02, "a face wound the other way still faces the eye");
}

}  // namespace

int main() {
    walker_refuses();
    builds();
    select_halves();
    select_trials();
    face_boxes();
    packed_normals();
    corner_keys();
    smooth_corners();
    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "gpu_ddgi_bvh: ok\n";
    return 0;
}
