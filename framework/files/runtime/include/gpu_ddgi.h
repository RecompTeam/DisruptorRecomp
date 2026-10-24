/* Probe lighting prototype: the world triangles of one world draw, kept on the CPU with the camera they were drawn from. */
#ifndef PSXRECOMP_GPU_DDGI_H
#define PSXRECOMP_GPU_DDGI_H

#include <stdint.h>

#include "gte_precision.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDdgiTriangle {
    float world[3][3];
    int16_t screen[3][2]; /* the game's screen place, before the draw offset */
    uint8_t uv[3][2];
    uint8_t colour[3][3];
    uint16_t clut, texpage;
    uint8_t flags; /* 1 textured, 2 semi-transparent, 4 raw texture */
} GpuDdgiTriangle;

#define GPU_DDGI_MASK_ROWS 32

/* A world sprite as an upright card: it spans centre +- across +- down in world units, down toward its picture's bottom. */
typedef struct GpuDdgiCard {
    float centre[3], across[3], down[3];
    float depth;
    int16_t uv[4]; /* u at the left edge, v at the top, u at the right edge, v at the bottom */
    uint8_t colour[3];
    uint16_t clut, texpage;
    uint8_t flags; /* 1 semi-transparent, 2 raw texture, 4 the game's blob shadow, 8 a base actor (enemy or body), 16 an effect */
    float light;   /* the game's gain for a shot's light, 1 for what its picture gives */
    float cover, blended, texels[3];   /* as drawn: the share not clear, the share of those with the mask bit, their colour */
    float bounds[4];                   /* its texels as drawn, in fractions of the card: left, top, right, bottom; with a mask
                                          those that block light, else those not clear */
    float box[4];                      /* the box of its texels that block light, in fractions of the card: left, top, right, bottom */
    uint32_t mask[GPU_DDGI_MASK_ROWS]; /* those texels as drawn over the box, 32 a row, bit 0 at the left; none for 4 or 16 */
} GpuDdgiCard;

#define GPU_DDGI_CASTERS 64
#define GPU_DDGI_LAMPS 24
#define GPU_DDGI_DYNAMIC_LAMPS 8

/* What casts a shadow: an upright ellipsoid fitted to a sprite's opaque texels for the shade under it, the picture itself
 * for the shadow it throws. previous: its place in the frame before, or -1. */
typedef struct GpuDdgiCaster {
    float centre[3], across, tall;
    float picture[3], half_width, half_height, side[2]; /* the card's box: centre, half sizes, the card's left to right */
    uint32_t mask[GPU_DDGI_MASK_ROWS];                  /* the card's mask */
    int previous;
} GpuDdgiCaster;

/* A light: a cluster of the level's emitting faces (dynamic 0), or a shot, an impact or a blast (dynamic 1). */
typedef struct GpuDdgiLamp {
    float position[3], size; /* size: the emitter's radius; reach for a dynamic one */
    float colour[3], power;  /* colour linear and normalised, power what it gives at 1 unit */
    float normal[3], weight; /* weight fades a light in and out of the chosen set */
    uint32_t key;
    int dynamic, previous;
} GpuDdgiLamp;

typedef struct GpuDdgiScene {
    uint32_t pose, before; /* before: the pose of the scene the previous indices point into */
    int casters, lamps;
    GpuDdgiCaster caster[GPU_DDGI_CASTERS];
    GpuDdgiLamp lamp[GPU_DDGI_LAMPS];
} GpuDdgiScene;

typedef struct GpuDdgiStats {
    uint32_t pose;
    int triangles, corners, within_pixel;
    double worst;
    uint32_t stale, frames;
} GpuDdgiStats;

void gpu_ddgi_set_view(int enabled);
int gpu_ddgi_view(void);
void gpu_ddgi_set_capture(int enabled);
/* The last finished world draw: its triangles and camera. 0 when there is none. */
int gpu_ddgi_frame(const GpuDdgiTriangle **triangles, int *count, GteCameraPose *camera);
/* The world draw the last triangles went to, finished or not. */
int gpu_ddgi_latest(const GpuDdgiTriangle **triangles, int *count, GteCameraPose *camera);
/* The kept world draw whose draw area holds display row y, the newer when both do, else the latest. */
int gpu_ddgi_shown(int y, const GpuDdgiTriangle **triangles, int *count, GteCameraPose *camera);
/* The yaw in radians the presentation turned that pose's world by about the eye, a part of a game step. 0 when not kept. */
float gpu_ddgi_turn(uint32_t pose);
/* The shadow casters and lights of the world draw of that pose. 0 when it is no longer kept. */
int gpu_ddgi_scene(uint32_t pose, GpuDdgiScene *scene);
/* The game's own blob shadows are left out while dynamic shadows are on and this is set (the default). */
void gpu_ddgi_set_hide_blobs(int enabled);
int gpu_ddgi_hide_blobs(void);
void gpu_ddgi_set_shadowing(int enabled);
/* A new level: the emitting faces kept across frames and the scene the next one pairs its casters and lights with go. */
void gpu_ddgi_forget_level(void);
/* The world sprites drawn under one camera pose. 0 when that pose's draw is no longer kept. */
int gpu_ddgi_cards(uint32_t pose, const GpuDdgiCard **cards, int *count);
void gpu_ddgi_stats(GpuDdgiStats *stats);
/* Draws the probe grid around the camera into the frame the last triangles went to. */
void gpu_ddgi_draw_probes(void);

/* The OpenGL renderer's probe lighting: light from the probes multiplies the picture, the average kept. */
void gl_renderer_set_ddgi(int enabled);
/* Whether the first update takes the PSX_DDGI_* variables, on until a game that does not offer the lighting says. */
void gl_renderer_set_ddgi_environment(int taken);
int gl_renderer_ddgi(void);
void gl_renderer_set_ddgi_strength(float strength);
float gl_renderer_ddgi_strength(void);
void gl_renderer_set_ddgi_view(int enabled); /* the probes' light alone, grey where it is the average */
int gl_renderer_ddgi_view(void);
void gl_renderer_set_ddgi_bounce(float bounce);
float gl_renderer_ddgi_bounce(void);
/* How much bright surfaces light their surroundings beyond what they show. */
void gl_renderer_set_ddgi_glow(float glow);
float gl_renderer_ddgi_glow(void);
/* Shadows of enemies, bodies and pickups from the level's lights, and a contact shadow under them. */
void gl_renderer_set_ddgi_shadows(int enabled);
int gl_renderer_ddgi_shadows(void);
void gl_renderer_set_ddgi_shadow_strength(float strength);
float gl_renderer_ddgi_shadow_strength(void);
void gl_renderer_set_ddgi_contact(float strength);
float gl_renderer_ddgi_contact(void);
/* Light from shots, impacts and blasts, with shadows. */
void gl_renderer_set_ddgi_lamps(int enabled);
int gl_renderer_ddgi_lamps(void);
void gl_renderer_set_ddgi_lamp_strength(float strength);
float gl_renderer_ddgi_lamp_strength(void);
/* The shadows grey and the added light alone. */
void gl_renderer_set_ddgi_lamp_view(int enabled);
int gl_renderer_ddgi_lamp_view(void);
/* Sunlight with shadows on missions with open sky, the level's faces learned as they are drawn. */
void gl_renderer_set_ddgi_sun(int enabled);
int gl_renderer_ddgi_sun(void);
/* Every face of the level, from the game whenever it changed: the sun's rays meet walls the frame did not draw. */
int gpu_ddgi_set_level(const GpuDdgiTriangle *tris, int count); /* 0 when it could not be taken */
/* The mission being played, from the game each world draw: a new one forgets the level's faces. */
void gl_renderer_set_ddgi_mission(int mission);
int gl_renderer_ddgi_sun_here(void);
/* The current mission's sun: degrees round, degrees up, strength, the share of baked light its shade takes. */
void gl_renderer_ddgi_sun_get(float sun[4]);
void gl_renderer_ddgi_sun_set(const float sun[4]);
void gl_renderer_ddgi_sun_set_mission(int mission, const float sun[4]);

#ifdef __cplusplus
}
#endif

#endif
