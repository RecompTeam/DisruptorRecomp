#include "gpu_ddgi_turn.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr double kStep = 6.28318530717958647692 / 256.0;

// The game's rotation for a heading and a pitch, 4096 a unit, rounded as the game's tables are.
void game_rotation(double heading, double pitch, int16_t rotation[9]) {
    const double ch = std::cos(heading), sh = std::sin(heading), cp = std::cos(pitch), sp = std::sin(pitch);
    const double rows[3][3] = {{ch, 0.0, -sh}, {sh * sp, cp, ch * sp}, {sh * cp, -sp, ch * cp}};
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) rotation[r * 3 + k] = static_cast<int16_t>(std::lround(rows[r][k] * 4096.0));
}

// How far back times rows is from the identity, the largest element's error.
double inverse_error(const float rows[3][3], const float back[3][3]) {
    double worst = 0.0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) sum += static_cast<double>(back[k][i]) * rows[k][j];
            worst = std::fmax(worst, std::fabs(sum - (i == j ? 1.0 : 0.0)));
        }
    return worst;
}

void rows_of(const int16_t rotation[9], float rows[3][3]) {
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) rows[r][k] = static_cast<float>(rotation[r * 3 + k]) / 4096.0f;
}

void inverse_of_the_game_turn() {
    // Rows a few thousandths long, as the game's are at some headings: the transpose is off, the inverse is not.
    const float rows[3][3] = {{1.0022f, 0.0f, 0.0031f}, {0.0007f, 0.9989f, -0.0012f}, {-0.0028f, 0.0015f, 1.0018f}};
    float back[3][3], transpose[3][3];
    psx_ddgi_back_turn(rows, back);
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 3; ++i) transpose[k][i] = rows[k][i];
    expect(inverse_error(rows, back) < 1e-5, "the inverse undoes a turn whose rows are not unit long");
    expect(inverse_error(rows, transpose) > 3e-3, "the transpose does not undo it: the test tells the two apart");

    double worst = 0.0, worst_transpose = 0.0;
    for (int heading = 0; heading < 256; ++heading)
        for (const double pitch : {0.0, 0.2, -0.35}) {
            int16_t rotation[9];
            game_rotation(heading * kStep, pitch, rotation);
            float game_rows[3][3], game_back[3][3], game_transpose[3][3];
            rows_of(rotation, game_rows);
            psx_ddgi_pose_back(rotation, game_back);
            for (int k = 0; k < 3; ++k)
                for (int i = 0; i < 3; ++i) game_transpose[k][i] = game_rows[k][i];
            worst = std::fmax(worst, inverse_error(game_rows, game_back));
            worst_transpose = std::fmax(worst_transpose, inverse_error(game_rows, game_transpose));
        }
    expect(worst < 1e-5, "every heading's rounded rotation is undone by its inverse");
    expect(worst_transpose > worst * 10.0, "the rounded rotations are not orthonormal, so the transpose is worse");
}

void inverse_of_an_exact_turn() {
    const double angle = 0.7;
    const float rows[3][3] = {{static_cast<float>(std::cos(angle)), 0.0f, static_cast<float>(-std::sin(angle))},
                              {0.0f, 1.0f, 0.0f},
                              {static_cast<float>(std::sin(angle)), 0.0f, static_cast<float>(std::cos(angle))}};
    float back[3][3];
    psx_ddgi_back_turn(rows, back);
    double worst = 0.0;
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 3; ++i) worst = std::fmax(worst, std::fabs(back[k][i] - rows[k][i]));
    expect(worst < 1e-6, "an orthonormal turn's inverse is its transpose");

    const int16_t identity[9] = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
    psx_ddgi_pose_back(identity, back);
    bool exact = true;
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 3; ++i) exact = exact && back[k][i] == (k == i ? 1.0f : 0.0f);
    expect(exact, "the GTE's identity, 4096 a unit, turns nothing");
}

void singular_turn_keeps_the_transpose() {
    const float rows[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    float back[3][3];
    psx_ddgi_back_turn(rows, back);
    bool same = true;
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 3; ++i) same = same && back[k][i] == rows[k][i];
    expect(same, "a turn with no inverse keeps its transpose, no division by zero");
}

// The eye: the world point the camera's rows take to the origin.
void eye_of(const float rows[3][4], double eye[3]) {
    const float turn[3][3] = {{rows[0][0], rows[0][1], rows[0][2]}, {rows[1][0], rows[1][1], rows[1][2]},
                              {rows[2][0], rows[2][1], rows[2][2]}};
    float back[3][3];
    psx_ddgi_back_turn(turn, back);
    for (int a = 0; a < 3; ++a) eye[a] = -(back[0][a] * rows[0][3] + back[1][a] * rows[1][3] + back[2][a] * rows[2][3]);
}

void smooth_turn_about_the_eye() {
    int16_t rotation[9];
    game_rotation(37 * kStep, 0.1, rotation);
    float rows[3][4];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) rows[r][k] = static_cast<float>(rotation[r * 3 + k]) / 4096.0f;
    rows[0][3] = 812.0f;
    rows[1][3] = -140.0f;
    rows[2][3] = 2350.0f;

    float same[3][4];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k) same[r][k] = rows[r][k];
    psx_ddgi_turn_rows(same, 0.0f);
    bool untouched = true;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k) untouched = untouched && same[r][k] == rows[r][k];
    expect(untouched, "no turn leaves the rows as they are");

    double before[3], after[3];
    eye_of(rows, before);
    float turned[3][4];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k) turned[r][k] = rows[r][k];
    psx_ddgi_turn_rows(turned, static_cast<float>(0.43 * kStep));
    eye_of(turned, after);
    double moved = 0.0;
    for (int a = 0; a < 3; ++a) moved = std::fmax(moved, std::fabs(after[a] - before[a]));
    expect(moved < 0.01, "a part of a step turns the camera about its eye: the eye stays");

    bool kept_up = true;
    for (int k = 0; k < 4; ++k) kept_up = kept_up && turned[1][k] == rows[1][k];
    expect(kept_up, "the turn is about the vertical: the second row stays");

    double sideways = 0.0;
    for (int k = 0; k < 3; ++k) sideways += static_cast<double>(turned[0][k]) * rows[0][k];
    const double length = std::sqrt(rows[0][0] * rows[0][0] + rows[0][1] * rows[0][1] + rows[0][2] * rows[0][2]);
    expect(std::fabs(std::acos(sideways / (length * length)) - 0.43 * kStep) < 1e-3, "the first row turns by the angle given");
    double toward = 0.0, square = 0.0;
    for (int k = 0; k < 3; ++k) {
        toward += static_cast<double>(turned[0][k]) * rows[2][k];
        square += static_cast<double>(turned[0][k]) * turned[2][k] - static_cast<double>(rows[0][k]) * rows[2][k];
    }
    expect(toward > 0.009, "a positive angle turns the first row toward the third");
    expect(std::fabs(square) < 1e-5, "a turn keeps the rows square to each other");

    float round[3][4];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k) round[r][k] = rows[r][k];
    for (int i = 0; i < 4; ++i) psx_ddgi_turn_rows(round, 1.57079632679f);
    double back_home = 0.0;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k) back_home = std::fmax(back_home, std::fabs(round[r][k] - rows[r][k]) / (k == 3 ? 1000.0 : 1.0));
    expect(back_home < 1e-5, "four quarter turns bring the rows back");
}

void camera_between() {
    int16_t one[9], two[9];
    game_rotation(40 * kStep, 0.1, one);
    game_rotation(41.3 * kStep, 0.12, two);
    float before[3][4], after[3][4];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) before[r][k] = one[r * 3 + k] / 4096.0f, after[r][k] = two[r * 3 + k] / 4096.0f;
    before[0][3] = 300.0f, before[1][3] = -80.0f, before[2][3] = 1900.0f;
    after[0][3] = 320.0f, after[1][3] = -82.0f, after[2][3] = 1960.0f;
    const float before_offset[3] = {1000.0f, 0.0f, -500.0f}, after_offset[3] = {1010.0f, 0.0f, -490.0f};
    float rows[3][4], offset[3], back[3][3], eye[3], ends[2][3];
    for (int end = 0; end < 2; ++end) {
        float end_back[3][3];
        psx_ddgi_camera_back(end ? after : before, end ? after_offset : before_offset, end_back, ends[end]);
    }
    psx_ddgi_camera_between(before, before_offset, after, after_offset, 1.0f, rows, offset, back, eye);
    bool at_before = true;
    for (int a = 0; a < 3; ++a) at_before = at_before && std::fabs(eye[a] - ends[0][a]) < 1e-3f;
    expect(at_before, "keeping all of the way back is the frame before's eye");
    psx_ddgi_camera_between(before, before_offset, after, after_offset, 0.0f, rows, offset, back, eye);
    bool at_after = true;
    for (int a = 0; a < 3; ++a) at_after = at_after && std::fabs(eye[a] - ends[1][a]) < 1e-3f;
    expect(at_after, "keeping none is the frame after's");

    psx_ddgi_camera_between(before, before_offset, after, after_offset, 0.5f, rows, offset, back, eye);
    float turn[3][3], lerped[3][3], back_before[3][3], back_after[3][3];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) turn[r][k] = rows[r][k];
    float b[3][3], a[3][3];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) b[r][k] = before[r][k], a[r][k] = after[r][k];
    psx_ddgi_back_turn(b, back_before);
    psx_ddgi_back_turn(a, back_after);
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 3; ++i) lerped[k][i] = 0.5f * (back_before[k][i] + back_after[k][i]);
    expect(inverse_error(turn, back) < 1e-5, "half way the inverse undoes the rows shown");
    expect(inverse_error(turn, lerped) > 1e-5, "the mean of the two inverses would not: the test tells them apart");
    double camera[3];
    for (int r = 0; r < 3; ++r)
        camera[r] = rows[r][0] * (eye[0] - offset[0]) + rows[r][1] * (eye[1] - offset[1]) + rows[r][2] * (eye[2] - offset[2]) + rows[r][3];
    expect(std::fabs(camera[0]) < 1e-2 && std::fabs(camera[1]) < 1e-2 && std::fabs(camera[2]) < 1e-2,
           "the eye half way is where the rows shown put the camera's origin");
}

}  // namespace

int main() {
    inverse_of_the_game_turn();
    inverse_of_an_exact_turn();
    singular_turn_keeps_the_transpose();
    smooth_turn_about_the_eye();
    camera_between();
    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "gpu_ddgi_turn: ok\n";
    return 0;
}
