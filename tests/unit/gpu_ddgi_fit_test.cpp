#include "gpu_ddgi_fit.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

GpuDdgiTriangle triangle(const float a[3], const float b[3], const float c[3]) {
    GpuDdgiTriangle t{};
    for (int axis = 0; axis < 3; ++axis) {
        t.world[0][axis] = a[axis];
        t.world[1][axis] = b[axis];
        t.world[2][axis] = c[axis];
    }
    return t;
}

void nnls_solves_and_clamps() {
    // A = [[1, 0], [0, 1], [1, 1]]: A'A = [[2, 1], [1, 2]].
    const double ata[4] = {2.0, 1.0, 1.0, 2.0};
    const double exact[2] = {1.0 + 3.0, 2.0 + 3.0};  /* b = (1, 2, 3) */
    double x[2];
    psx_ddgi_nnls(ata, exact, 2, x, 200);
    expect(std::fabs(x[0] - 1.0) < 1e-6 && std::fabs(x[1] - 2.0) < 1e-6, "an exact system is solved");
    const double negative[2] = {-1.0 + 1.0, 2.0 + 1.0};  /* b = (-1, 2, 1) */
    psx_ddgi_nnls(ata, negative, 2, x, 200);
    expect(x[0] == 0.0 && x[1] > 0.0, "a coefficient that would go below zero stays at zero");
}

struct Scene {
    std::vector<GpuDdgiTriangle> tris;
    std::vector<DdgiBuildFace> faces;
    std::vector<DdgiNode> nodes;

    void build() {
        faces.resize(tris.size());
        nodes.resize(2 * tris.size() + 1);
        for (int i = 0; i < static_cast<int>(tris.size()); ++i) psx_ddgi_face_of(&faces[i], &tris[i], i);
        DdgiBvhBuild bvh = {nodes.data(), faces.data(), 1};
        psx_ddgi_sah_split(&bvh, 0, 0, static_cast<int>(tris.size()), 0);
    }
};

// A floor of 40 x 40 squares 50 wide facing up (world y grows downward), a wall across it at x = 1200.
Scene room() {
    Scene scene;
    for (int i = 0; i < 40; ++i)
        for (int j = 0; j < 40; ++j) {
            const float x = 50.0f * static_cast<float>(i), z = 50.0f * static_cast<float>(j);
            const float a[3] = {x, 0.0f, z}, b[3] = {x + 50.0f, 0.0f, z}, c[3] = {x, 0.0f, z + 50.0f}, d[3] = {x + 50.0f, 0.0f, z + 50.0f};
            scene.tris.push_back(triangle(a, b, c));
            scene.tris.push_back(triangle(c, b, d));
        }
    for (int k = 0; k < 8; ++k) {
        const float z = 250.0f * static_cast<float>(k);
        const float a[3] = {1200.0f, -3000.0f, z}, b[3] = {1200.0f, 0.0f, z}, c[3] = {1200.0f, -3000.0f, z + 250.0f},
                    d[3] = {1200.0f, 0.0f, z + 250.0f};
        scene.tris.push_back(triangle(a, b, c));
        scene.tris.push_back(triangle(c, b, d));
    }
    scene.build();
    return scene;
}

void floor_faces_up() {
    Scene scene = room();
    float n[3];
    psx_ddgi_face_normal(&scene.tris[0], 1.0f, n);
    float m[3];
    psx_ddgi_face_normal(&scene.tris[1], 1.0f, m);
    expect(n[1] < -0.99f && m[1] < -0.99f, "both halves of a floor square face up, toward -y");
}

void wall_blocks() {
    Scene scene = room();
    const float lamp[3] = {500.0f, -1500.0f, 500.0f};
    const auto blocked = [&](float x, float z) {
        const float from[3] = {x, -8.0f, z};
        float to[3] = {lamp[0] - from[0], lamp[1] - from[1], lamp[2] - from[2]};
        const float dist = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        for (float &v : to) v /= dist;
        return psx_ddgi_bvh_blocked(scene.nodes.data(), scene.faces.data(), scene.tris.data(), from, to, 2.0f, dist * 0.95f - 16.0f);
    };
    expect(!blocked(600.0f, 600.0f), "the floor before the wall sees the lamp");
    expect(blocked(1800.0f, 600.0f), "the floor behind the wall does not");
}

void fit_finds_the_lamp() {
    Scene scene = room();
    DdgiFit fit{};
    fit.lamps = 2;
    fit.lamp[0] = DdgiLevelLamp{{500.0f, -1500.0f, 500.0f}, {0.0f, 1.0f, 0.0f}, 100.0f, {1.0f, 1.0f, 1.0f}, 0.0f};
    fit.lamp[1] = DdgiLevelLamp{{1700.0f, -1500.0f, 1500.0f}, {0.0f, 1.0f, 0.0f}, 100.0f, {1.0f, 1.0f, 1.0f}, 0.0f};  /* a guess that lights nothing */
    const float ambient = 0.2f, power = 4.0e6f;
    // The baked light the lamp and the ambient give, as the game would store it: 128 times its gamma, to a byte.
    for (GpuDdgiTriangle &t : scene.tris) {
        float n[3], centre[3], from[3];
        psx_ddgi_face_normal(&t, 1.0f, n);
        for (int a = 0; a < 3; ++a) {
            centre[a] = (t.world[0][a] + t.world[1][a] + t.world[2][a]) / 3.0f;
            from[a] = centre[a] + n[a] * 8.0f;
        }
        float to[3] = {fit.lamp[0].position[0] - from[0], fit.lamp[0].position[1] - from[1], fit.lamp[0].position[2] - from[2]};
        const float dist = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        for (float &v : to) v /= dist;
        const bool seen = !psx_ddgi_bvh_blocked(scene.nodes.data(), scene.faces.data(), scene.tris.data(), from, to, 2.0f,
                                                dist * 0.95f - 16.0f);
        const float lin = ambient + (seen ? power * psx_ddgi_lamp_share(centre, n, &fit.lamp[0]) : 0.0f);
        const float byte = std::fmin(255.0f, std::round(128.0f * std::pow(lin, 1.0f / 2.2f)));
        for (auto &corner : t.colour)
            for (uint8_t &channel : corner) channel = static_cast<uint8_t>(byte);
    }
    static DdgiFitWork work;
    psx_ddgi_fit_level(scene.nodes.data(), scene.faces.data(), scene.tris.data(), static_cast<int>(scene.tris.size()), 1.0f, nullptr,
                       &fit, &work);
    std::cout << "fit: ambient " << fit.ambient << " power " << fit.lamp[0].power << " other " << fit.lamp[1].power << " error "
              << fit.residual << '\n';
    expect(std::fabs(fit.ambient - ambient) < 0.03f * ambient, "the ambient comes back within 3 percent");
    expect(std::fabs(fit.lamp[0].power - power) < 0.05f * power, "the lamp's power within 5 percent");
    expect(fit.lamp[1].power < 0.05f * power, "a lamp that explains nothing gets next to none");
}

void baked_is_linear_by_channel() {
    GpuDdgiTriangle red{};
    for (auto &corner : red.colour) corner[0] = 128;
    expect(std::fabs(psx_ddgi_baked(&red) - 0.2126f) < 1e-4f, "a neutral red face is red's weight of linear light, not its power");
}

void a_plateau_still_seeds_lamps() {
    Scene scene = room();
    for (GpuDdgiTriangle &t : scene.tris)
        for (auto &corner : t.colour)
            for (uint8_t &channel : corner) channel = 128;
    static DdgiLevelLamp lamps[DDGI_FIT_LAMPS];
    const int made = psx_ddgi_hot_lamps(scene.nodes.data(), scene.faces.data(), scene.tris.data(), static_cast<int>(scene.tris.size()),
                                        1.0f, lamps, DDGI_FIT_LAMPS);
    expect(made > 0, "faces all of one brightness still stand lamps before them");
}

}  // namespace

int main() {
    nnls_solves_and_clamps();
    floor_faces_up();
    wall_blocks();
    fit_finds_the_lamp();
    baked_is_linear_by_channel();
    a_plateau_still_seeds_lamps();
    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "gpu_ddgi_fit: ok\n";
    return 0;
}
