#include "gpu_uv.h"
#include "gpu_world_texel.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {

void expect(bool condition, const char *message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

struct Face {
    int xs[3], ys[3], us[3], vs[3];
};

struct Drawn {
    int us[3], vs[3], ruled[4], plain[4];
    float bounds[4];
};

// What the renderer does with a polygon's triangle: the framework's rule by whole pixels, then the marks.
Drawn drawn(const Face &face) {
    static const int nowhere[3] = {0, 0, 0};
    Drawn out{};
    for (int i = 0; i < 3; ++i) {
        out.us[i] = face.us[i];
        out.vs[i] = face.vs[i];
    }
    psx_uv_tri_limits(face.xs, face.ys, out.us, out.vs, out.ruled);
    psx_uv_tri_mirror_offset(face.xs, face.ys, out.us, out.vs);
    psx_uv_tri_limits(nowhere, nowhere, face.us, face.vs, out.plain);
    psx_world_texel_mark(out.ruled, out.plain, out.bounds);
    return out;
}

struct Corner {
    float uv[2], bounds[4];
};

Corner settled(const Drawn &triangle, int corner, int take) {
    Corner out{{static_cast<float>(triangle.us[corner]), static_cast<float>(triangle.vs[corner])},
               {triangle.bounds[0], triangle.bounds[1], triangle.bounds[2], triangle.bounds[3]}};
    psx_world_texel_settle(take, out.uv, out.bounds);
    return out;
}

bool mirror_shows_the_games_own(const Face &face, const Drawn &triangle) {
    for (int i = 0; i < 3; ++i) {
        const Corner corner = settled(triangle, i, 1);
        if (corner.uv[0] != static_cast<float>(face.us[i]) || corner.uv[1] != static_cast<float>(face.vs[i])) return false;
        for (int bound = 0; bound < 4; ++bound)
            if (corner.bounds[bound] != static_cast<float>(triangle.plain[bound])) return false;
    }
    return true;
}

bool canonical_keeps_the_rule(const Drawn &triangle) {
    for (int i = 0; i < 3; ++i) {
        const Corner corner = settled(triangle, i, 0);
        if (corner.uv[0] != static_cast<float>(triangle.us[i]) || corner.uv[1] != static_cast<float>(triangle.vs[i])) return false;
        for (int bound = 0; bound < 4; ++bound) {
            if (corner.bounds[bound] != static_cast<float>(triangle.ruled[bound])) return false;
            if (static_cast<int>(triangle.bounds[bound] + 0.5f) != triangle.ruled[bound]) return false;  // the shader's rounding
        }
    }
    return true;
}

// One block's face in a recording of 2026-10-13 at six headings a turn step apart: its two triangles as the game drew them.
constexpr Face kUpperHalf[6] = {
    {{245, 212, 212}, {145, 189, 145}, {64, 95, 95}, {63, 32, 63}}, {{250, 216, 216}, {145, 190, 145}, {64, 95, 95}, {63, 32, 63}},
    {{255, 220, 220}, {146, 191, 145}, {64, 95, 95}, {63, 32, 63}}, {{260, 224, 224}, {146, 192, 146}, {64, 95, 95}, {63, 32, 63}},
    {{266, 228, 228}, {147, 193, 146}, {64, 95, 95}, {63, 32, 63}}, {{271, 232, 232}, {148, 194, 146}, {64, 95, 95}, {63, 32, 63}},
};
constexpr Face kLowerHalf[6] = {
    {{245, 212, 245}, {190, 189, 145}, {64, 95, 64}, {32, 32, 63}}, {{250, 216, 250}, {191, 190, 145}, {64, 95, 64}, {32, 32, 63}},
    {{255, 220, 255}, {192, 191, 146}, {64, 95, 64}, {32, 32, 63}}, {{260, 224, 260}, {194, 192, 146}, {64, 95, 64}, {32, 32, 63}},
    {{266, 228, 266}, {196, 193, 147}, {64, 95, 64}, {32, 32, 63}}, {{271, 232, 271}, {197, 194, 148}, {64, 95, 64}, {32, 32, 63}},
};

void the_rule_flips_a_turning_face_and_the_mirror_does_not() {
    const bool top_corners_share_a_row[6] = {true, true, false, true, false, false};
    for (int heading = 0; heading < 6; ++heading) {
        const Drawn upper = drawn(kUpperHalf[heading]), lower = drawn(kLowerHalf[heading]);
        expect((upper.vs[0] == 64) == top_corners_share_a_row[heading] && upper.vs[0] - 63 == upper.ruled[1] - 32,
               "the rule moves the upper half a texel down its texture, and its lower bound, only while its two top corners share a pixel row");
        expect(upper.us[0] == 65 && lower.us[0] == 65, "and both halves a texel along it at every heading, their upright edge being upright in whole pixels");
        expect(lower.vs[0] == 32 && lower.ruled[1] == 32, "the lower half, whose bottom corners never share a row here, is not moved down");
        expect(mirror_shows_the_games_own(kUpperHalf[heading], upper) && mirror_shows_the_games_own(kLowerHalf[heading], lower),
               "the mirror shows the game's own texture corners and the plain bounds for both halves at every heading");
        expect(canonical_keeps_the_rule(upper) && canonical_keeps_the_rule(lower), "the canonical picture reads what the rule gave");
    }
}

void every_way_the_rule_runs_is_taken_back() {
    int mirrored_u = 0, mirrored_v = 0, forward_u = 0, forward_v = 0, untouched = 0;
    for (const int right : {40, -20}) {
        for (const int low : {50, -30}) {
            for (const bool u_falls : {false, true}) {
                for (const bool v_falls : {false, true}) {
                    for (int shear = 0; shear < 4; ++shear) {
                        for (const bool wound_back : {false, true}) {
                            const int u0 = u_falls ? 95 : 64, u1 = u_falls ? 64 : 95, v0 = v_falls ? 31 : 0, v1 = v_falls ? 0 : 31;
                            Face face = {{10, right, 10 + (shear & 2 ? 1 : 0)}, {7, 7 + (shear & 1), low}, {u0, u1, u0}, {v0, v0, v1}};
                            if (wound_back) {
                                const Face kept = face;
                                for (int i = 0; i < 3; ++i) {
                                    face.xs[i] = kept.xs[2 - i];
                                    face.ys[i] = kept.ys[2 - i];
                                    face.us[i] = kept.us[2 - i];
                                    face.vs[i] = kept.vs[2 - i];
                                }
                            }
                            const Drawn triangle = drawn(face);
                            mirrored_u += triangle.us[0] != face.us[0];
                            mirrored_v += triangle.vs[0] != face.vs[0];
                            forward_u += triangle.ruled[2] != triangle.plain[2];
                            forward_v += triangle.ruled[3] != triangle.plain[3];
                            untouched += triangle.us[0] == face.us[0] && triangle.vs[0] == face.vs[0] &&
                                         triangle.ruled[2] == triangle.plain[2] && triangle.ruled[3] == triangle.plain[3];
                            expect(mirror_shows_the_games_own(face, triangle), "whichever way the rule ran, the mirror shows the game's own corners");
                            expect(canonical_keeps_the_rule(triangle), "and the canonical picture keeps the rule");
                        }
                    }
                }
            }
        }
    }
    expect(mirrored_u > 0 && mirrored_v > 0 && forward_u > 0 && forward_v > 0 && untouched > 0,
           "the cases cover a texture moved along each axis, a bound lowered on each, and a face the rule leaves alone");
}

void a_face_across_a_page_border_is_left_as_the_rule_made_it() {
    const Face face = {{10, 40, 10}, {7, 7, 50}, {280, 240, 280}, {0, 0, 31}};
    const Drawn triangle = drawn(face);
    expect(triangle.us[0] == 281 && triangle.ruled[0] == 0 && triangle.ruled[2] == 255, "the rule moves it and opens the whole page");
    const Corner corner = settled(triangle, 0, 1);
    expect(corner.uv[0] == 281.0f && corner.bounds[0] == 0.0f && corner.bounds[2] == 255.0f, "nothing is marked there, so nothing is taken back");
}

void the_last_texel_of_a_page_carries_its_mark() {
    const Face face = {{10, 40, 10}, {7, 7, 50}, {200, 255, 200}, {255, 255, 200}};
    const Drawn triangle = drawn(face);
    expect(triangle.ruled[2] == 254 && triangle.bounds[2] == 254.25f, "an upper bound lowered from 255 is marked");
    expect(mirror_shows_the_games_own(face, triangle) && canonical_keeps_the_rule(triangle), "and read back by both pictures");
}

}  // namespace

int main() {
    the_rule_flips_a_turning_face_and_the_mirror_does_not();
    every_way_the_rule_runs_is_taken_back();
    a_face_across_a_page_border_is_left_as_the_rule_made_it();
    the_last_texel_of_a_page_carries_its_mark();
    std::puts("GPU world texel: PASS");
    return 0;
}
