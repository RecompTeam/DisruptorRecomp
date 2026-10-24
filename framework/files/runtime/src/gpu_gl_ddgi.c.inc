/* Probe lighting prototype, included once by gpu_gl_renderer.c (DDGI, Majercik 2019): the world triangles of the last world
 * draw go into a BVH, a grid of probes that follows the camera casts rays through it, and each probe keeps its irradiance and
 * distances in octahedral tiles. The picture is multiplied by the probes' light relative to its average. */
#include "gpu_ddgi.h"
#include "gpu_ddgi_bvh.h"
#include "gpu_ddgi_turn.h"

#define DDGI_NX 20
#define DDGI_NY 8
#define DDGI_NZ 20
#define DDGI_PROBES (DDGI_NX * DDGI_NY * DDGI_NZ)
#define DDGI_RAYS 128
#define DDGI_SPACING 400.0f
#define DDGI_FARTHEST 4000.0f
#define DDGI_IRR 8
#define DDGI_DIST 16
#define DDGI_ROWS 64
#define DDGI_MAX_TRIS 13000

#define DDGI_GL_RGBA32F 0x8814
#define DDGI_GL_R32UI 0x8236
#define DDGI_GL_RED_INTEGER 0x8D94
#define DDGI_GL_RGBA16F 0x881A
#define DDGI_GL_RG32F 0x8230
#define DDGI_GL_RG 0x8227
#define DDGI_GL_R8 0x8229
#define DDGI_GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define DDGI_GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define DDGI_GL_CURRENT_PROGRAM 0x8B8D
#define DDGI_GL_VERTEX_ARRAY_BINDING 0x85B5
#define DDGI_GL_ACTIVE_TEXTURE 0x84E0
#define DDGI_GL_CONSTANT_ALPHA 0x8003
#define DDGI_GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#define DDGI_GL_FUNC_ADD 0x8006
#define DDGI_GL_BLEND_SRC_RGB 0x80C9
#define DDGI_GL_BLEND_DST_RGB 0x80C8
#define DDGI_GL_BLEND_SRC_ALPHA 0x80CB
#define DDGI_GL_BLEND_DST_ALPHA 0x80CA
#define DDGI_GL_BLEND_EQUATION_RGB 0x8009
#define DDGI_GL_BLEND_EQUATION_ALPHA 0x883D
#define DDGI_GL_BLEND_COLOR 0x8005

#define DDGI_STR2(x) #x
#define DDGI_STR(x) DDGI_STR2(x)
#define DDGI_GLSL_COMMON                                                                                                              \
    "#define NX " DDGI_STR(DDGI_NX) "\n#define NY " DDGI_STR(DDGI_NY) "\n#define NZ " DDGI_STR(DDGI_NZ) "\n"                      \
    "#define RAYS " DDGI_STR(DDGI_RAYS) "\n#define IRR " DDGI_STR(DDGI_IRR) "\n#define DIST " DDGI_STR(DDGI_DIST) "\n"             \
    "vec3 fib(int i){ float phi = 2.399963229728653*float(i); float z = 1.0 - (2.0*float(i) + 1.0)/float(RAYS);\n"                  \
    "  float r = sqrt(max(0.0, 1.0 - z*z)); return vec3(r*cos(phi), r*sin(phi), z); }\n"                                          \
    "vec2 sgn2(vec2 v){ return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0); }\n"                                         \
    "vec2 oct_encode(vec3 v){ v /= abs(v.x) + abs(v.y) + abs(v.z); vec2 e = v.xy; if (v.z < 0.0) e = (1.0 - abs(v.yx))*sgn2(v.xy);\n" \
    "  return e; }\n"                                                                                                              \
    "vec3 oct_decode(vec2 e){ vec3 v = vec3(e, 1.0 - abs(e.x) - abs(e.y)); if (v.z < 0.0) v.xy = (1.0 - abs(v.yx))*sgn2(v.xy);\n"   \
    "  return normalize(v); }\n"                                                                                                   \
    "int posmod(int a, int n){ return a - n*int(floor(float(a)/float(n))); }\n"                                                    \
    "ivec3 slot_of(ivec3 cell){ return ivec3(posmod(cell.x, NX), posmod(cell.y, NY), posmod(cell.z, NZ)); }\n"                     \
    "ivec2 atlas_cell(ivec3 slot){ return ivec2(slot.x + NX*slot.z, slot.y); }\n"                                                  \
    "vec2 tile_uv(ivec3 cell, vec3 dir, int n, vec2 size){ vec2 e = oct_encode(dir)*0.5 + 0.5;\n"                                  \
    "  return (vec2(atlas_cell(slot_of(cell))*(n + 2)) + 1.0 + e*float(n))/size; }\n"

/* The world triangles' BVH and their textures in VRAM, for rays. */
#define DDGI_GLSL_BVH                                                                                                                 \
    "uniform sampler2D u_nodes, u_tris;\n"                                                                                           \
    "uniform usampler2D u_vram;\n"                                                                                                   \
    "vec4 fetch(sampler2D s, int i){ return texelFetch(s, ivec2(i & 1023, i >> 10), 0); }\n"                                       \
    "int vram_at(int x, int y){ return int(texelFetch(u_vram, ivec2(x & 1023, y & 511), 0).r); }\n"                               \
    "int texel(vec2 uv, int tpage, int clut){\n"                                                                                     \
    "  int u = int(floor(uv.x)) & 255, v = int(floor(uv.y)) & 255;\n"                                                                \
    "  int tx = (tpage & 15)*64, ty = ((tpage >> 4) & 1)*256, depth = (tpage >> 7) & 3;\n"                                          \
    "  int cx = (clut & 63)*16, cy = (clut >> 6) & 511;\n"                                                                           \
    "  if (depth == 0) { int px = vram_at(tx + (u >> 2), ty + v); return vram_at(cx + ((px >> ((u & 3)*4)) & 15), cy); }\n"       \
    "  if (depth == 1) { int px = vram_at(tx + (u >> 1), ty + v); return vram_at(cx + ((px >> ((u & 1)*8)) & 255), cy); }\n"      \
    "  return vram_at(tx + u, ty + v); }\n"                                                                                          \
    "vec3 col5(int raw){ return vec3(float(raw & 31), float((raw >> 5) & 31), float((raw >> 10) & 31))/31.0; }\n"                  \
    "vec3 rgb8(float c){ int i = int(c); return vec3(float(i & 255), float((i >> 8) & 255), float((i >> 16) & 255))/255.0; }\n"   \
    "vec2 uv8(float w){ int i = int(w); return vec2(float(i & 255), float((i >> 8) & 255)) + 0.5; }\n"                             \
    "/* The nearest face along o + d t, tmin < t < best, that is no transparent texel; solid_only skips blended texels. */\n"        \
    "int bvh_hit_in(sampler2D nodes, sampler2D tris, vec3 o, vec3 d, float tmin, inout float best, out float hu, out float hv,\n"    \
    "               bool solid_only, bool any){\n"                                                                                  \
    "  vec3 safe = vec3(abs(d.x) < 1e-7 ? 1e-7 : d.x, abs(d.y) < 1e-7 ? 1e-7 : d.y, abs(d.z) < 1e-7 ? 1e-7 : d.z), inv = 1.0/safe;\n" \
    "  int hit = -1; hu = 0.0; hv = 0.0;\n"                                                                                          \
    "  int stack[32]; int sp = 0; stack[sp++] = 0;\n"                                                                                \
    "  while (sp > 0) {\n"                                                                                                           \
    "    int ni = stack[--sp];\n"                                                                                                    \
    "    vec4 a = fetch(nodes, ni*2), b = fetch(nodes, ni*2 + 1);\n"                                                             \
    "    vec3 t0 = (a.xyz - o)*inv, t1 = (b.xyz - o)*inv, tn3 = min(t0, t1), tf3 = max(t0, t1);\n"                                  \
    "    float tn = max(max(tn3.x, tn3.y), max(tn3.z, 0.0)), tf = min(min(tf3.x, tf3.y), min(tf3.z, best));\n"                     \
    "    if (tn > tf) continue;\n"                                                                                                   \
    "    int count = int(b.w);\n"                                                                                                    \
    "    if (count == 0) { if (sp < 31) { stack[sp++] = int(a.w); stack[sp++] = int(a.w) + 1; } continue; }\n"                     \
    "    for (int k = 0; k < count; ++k) {\n"                                                                                        \
    "      int i = int(a.w) + k;\n"                                                                                                  \
    "      vec4 v0 = fetch(tris, i*5), v1 = fetch(tris, i*5 + 1), v2 = fetch(tris, i*5 + 2);\n"                               \
    "      vec3 e1 = v1.xyz - v0.xyz, e2 = v2.xyz - v0.xyz, p = cross(d, e2);\n"                                                    \
    "      float det = dot(e1, p); if (abs(det) < 1e-9) continue;\n"                                                                 \
    "      float id = 1.0/det; vec3 s = o - v0.xyz; float u = dot(s, p)*id; if (u < 0.0 || u > 1.0) continue;\n"                    \
    "      vec3 q = cross(s, e1); float v = dot(d, q)*id; if (v < 0.0 || u + v > 1.0) continue;\n"                                   \
    "      float t = dot(e2, q)*id; if (t <= tmin || t >= best) continue;\n"                                                         \
    "      vec4 m = fetch(tris, i*5 + 3);\n"                                                                                         \
    "      int raw = (int(m.z) & 1) != 0 ? texel(uv8(v0.w)*(1.0 - u - v) + uv8(v1.w)*u + uv8(v2.w)*v, int(m.y), int(m.x)) : 0x8000;\n" \
    "      if (raw == 0) continue;\n"                                                                                                \
    "      /* A blended command blends only texels with the mask bit: the rest of its face is solid. */\n"                          \
    "      if (solid_only && (int(m.z) & 2) != 0 && (raw & 0x8000) != 0) continue;\n"                                               \
    "      best = t; hit = i; hu = u; hv = v;\n"                                                                                     \
    "      if (any) return hit; }\n"                                                                                                 \
    "  }\n"                                                                                                                          \
    "  return hit; }\n"                                                                                                              \
    "uniform sampler2D u_level_nodes, u_level_tris;\n"                                                                              \
    "/* Toward the sun and its strength; its linear colour and how much of the baked light it takes back in shade; 1 with the\n"   \
    " * level's own BVH. */\n"                                                                                                      \
    "uniform vec4 u_sun, u_sun_colour, u_level;\n"                                                                                 \
    "int bvh_hit(vec3 o, vec3 d, float tmin, inout float best, out float hu, out float hv, bool solid_only, bool any){\n"            \
    "  return bvh_hit_in(u_nodes, u_tris, o, d, tmin, best, hu, hv, solid_only, any); }\n"                                         \
    "bool sun_blocked(vec3 o){\n"                                                                                                   \
    "  float best = 40000.0, hu, hv;\n"                                                                                             \
    "  if (bvh_hit(o, u_sun.xyz, 1.0, best, hu, hv, true, true) >= 0) return true;\n"                                               \
    "  best = 40000.0;\n"                                                                                                           \
    "  return u_level.x > 0.5 && bvh_hit_in(u_level_nodes, u_level_tris, o, u_sun.xyz, 1.0, best, hu, hv, true, true) >= 0; }\n"

static const char *DDGI_VS =
    "#version 330\n"
    "void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2); gl_Position = vec4(p*2.0 - 1.0, 0.0, 1.0); }\n";

/* One fragment a ray: x the ray, y the probe. rgb what it brings back, a its length, negative for a back face, past the
 * farthest for a miss nobody saw. */
static const char *DDGI_TRACE_FS =
    "#version 330\n" DDGI_GLSL_COMMON DDGI_GLSL_BVH
    "out vec4 frag;\n"
    "uniform sampler2D u_irr, u_frame, u_state;\n"
    "uniform vec4 u_frame_at;  /* the frame's size and its picture's centre, in native pixels */\n"
    "uniform vec4 u_origin;  /* the world cell of the grid's corner, spacing */\n"
    "uniform vec4 u_rot0, u_rot1, u_rot2;  /* columns of this update's turn of the rays */\n"
    "uniform vec4 u_camera;  /* where the camera stood, how much a hit is lit by the probes */\n"
    "uniform vec4 u_sky;     /* how much a bright surface glows, the farthest a ray goes */\n"
    "uniform vec4 u_row0, u_row1, u_row2, u_offset;  /* the camera: rows of its turn and its shift, world = vertex + offset */\n"
    "uniform vec4 u_view;    /* focal, half the picture's width and height, which half of the probes is traced */\n"
    "ivec2 state_texel(ivec3 cell){ ivec3 s = slot_of(cell); int p = s.x + NX*(s.z + NZ*s.y); return ivec2(p % 64, p / 64); }\n"
    "vec3 probe_irr(ivec3 cell, vec3 dir){ return texture(u_irr, tile_uv(cell, dir, IRR, vec2(textureSize(u_irr, 0)))).rgb; }\n"
    "vec3 probe_light(vec3 p, vec3 n){\n"
    "  vec3 g = p/u_origin.w; ivec3 base = ivec3(floor(g)); vec3 f = g - vec3(base);\n"
    "  ivec3 lo = ivec3(u_origin.xyz), hi = lo + ivec3(NX, NY, NZ) - 1; vec3 sum = vec3(0.0); float ws = 0.0;\n"
    "  for (int i = 0; i < 8; ++i) {\n"
    "    ivec3 off = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1), c = clamp(base + off, lo, hi);\n"
    "    vec3 w3 = mix(1.0 - f, f, vec3(off)); vec3 to = vec3(c)*u_origin.w - p;\n"
    "    float w = w3.x*w3.y*w3.z*max(0.05, dot(normalize(to + vec3(0.001)), n));\n"
    "    if (texelFetch(u_state, state_texel(c), 0).r < 0.5) continue;\n"
    "    vec3 e = probe_irr(c, n); if (dot(e, vec3(1.0)) <= 1e-5) continue;\n"
    "    sum += e*w; ws += w; }\n"
    "  return ws > 1e-5 ? sum/ws : vec3(0.0); }\n"
    "/* Where a ray that hits nothing leaves the picture: the frame there is what it brings back. */\n"
    "bool seen(vec3 p, out vec3 colour){\n"
    "  vec3 v = p - u_offset.xyz;\n"
    "  vec3 c = vec3(dot(u_row0.xyz, v) + u_row0.w, dot(u_row1.xyz, v) + u_row1.w, dot(u_row2.xyz, v) + u_row2.w);\n"
    "  colour = vec3(0.0);\n"
    "  if (c.z <= 16.0) return false;\n"
    "  vec2 s = c.xy*u_view.x/c.z;\n"
    "  if (abs(s.x) >= u_view.y || abs(s.y) >= u_view.z) return false;\n"
    "  colour = texture(u_frame, (u_frame_at.zw + s)/u_frame_at.xy).rgb;\n"
    "  return true; }\n"
    "void main(){\n"
    "  ivec2 at = ivec2(gl_FragCoord.xy); int ray = at.x, probe = at.y*2 + int(u_view.w);\n"
    "  ivec3 slot = ivec3(probe % NX, probe / (NX*NZ), (probe / NX) % NZ), origin = ivec3(u_origin.xyz);\n"
    "  ivec3 cell = origin + ivec3(posmod(slot.x - origin.x, NX), posmod(slot.y - origin.y, NY), posmod(slot.z - origin.z, NZ));\n"
    "  vec3 o = vec3(cell)*u_origin.w, d = mat3(u_rot0.xyz, u_rot1.xyz, u_rot2.xyz)*fib(ray);\n"
    "  float best = u_sky.w, hu, hv; int hit = bvh_hit(o, d, 1.0, best, hu, hv, false, false);\n"
    "  if (hit < 0) { vec3 sky; frag = seen(o + d*u_sky.w, sky) ? vec4(sky, u_sky.w) : vec4(0.0, 0.0, 0.0, u_sky.w + 1.0); return; }\n"
    "  vec4 v0 = fetch(u_tris, hit*5), v1 = fetch(u_tris, hit*5 + 1), v2 = fetch(u_tris, hit*5 + 2);\n"
    "  vec4 m = fetch(u_tris, hit*5 + 3), c = fetch(u_tris, hit*5 + 4);\n"
    "  vec3 n = cross(v1.xyz - v0.xyz, v2.xyz - v0.xyz); if (dot(n, u_camera.xyz - v0.xyz) < 0.0) n = -n;\n"
    "  if (dot(n, d) > 0.0) { frag = vec4(0.0, 0.0, 0.0, -0.2*best); return; }\n"
    "  float w0 = 1.0 - hu - hv; vec3 vc = rgb8(c.x)*w0 + rgb8(c.y)*hu + rgb8(c.z)*hv;\n"
    "  int flags = int(m.z); vec3 albedo = vc, lit = vc;\n"
    "  if ((flags & 1) != 0) {\n"
    "    albedo = col5(texel(uv8(v0.w)*w0 + uv8(v1.w)*hu + uv8(v2.w)*hv, int(m.y), int(m.x)));\n"
    "    lit = (flags & 4) != 0 ? albedo : albedo*vc*2.0; }\n"
    "  /* A lamp, a light strip, a screen, a glowing pool, an added blend: light of its own. */\n"
    "  float bright = max(max(lit.r, lit.g), lit.b), abr = float((int(m.y) >> 5) & 3);\n"
    "  float glow = smoothstep(0.55, 0.95, bright) + ((flags & 3) == 3 && (abr == 1.0 || abr == 3.0) ? 1.0 : 0.0);\n"
    "  lit += lit*u_sky.x*min(glow, 1.0);\n"
    "  if (u_sun.w > 0.0) { vec3 nn = normalize(n); float s = dot(nn, u_sun.xyz);\n"
    "    if (s > 0.0 && !sun_blocked(o + d*best + nn*8.0)) lit += albedo*u_sun_colour.rgb*u_sun.w*s; }\n"
    "  if (u_camera.w > 0.0) lit += albedo*probe_light(o + d*best, normalize(n))*u_camera.w;\n"
    "  frag = vec4(lit, best); }\n";

/* One texel of the irradiance or distance atlas: its direction's share of every ray of its probe, blended into what was. */
static const char *DDGI_UPDATE_FS =
    "#version 330\n" DDGI_GLSL_COMMON
    "out vec4 frag;\n"
    "uniform sampler2D u_rays, u_fresh;\n"
    "uniform vec4 u_rot0, u_rot1, u_rot2;\n"
    "uniform vec4 u_mode;  /* texels a side inside the border, 1 for distances, how much of the old is kept, the farthest */\n"
    "uniform float u_half;\n"
    "ivec2 interior(ivec2 l, int n){\n"
    "  bool left = l.x < 0, right = l.x >= n, top = l.y < 0, bottom = l.y >= n;\n"
    "  if ((left || right) && (top || bottom)) return ivec2(left ? n - 1 : 0, top ? n - 1 : 0);\n"
    "  if (top || bottom) return ivec2(n - 1 - l.x, top ? 0 : n - 1);\n"
    "  if (left || right) return ivec2(left ? 0 : n - 1, n - 1 - l.y);\n"
    "  return l; }\n"
    "void main(){\n"
    "  int n = int(u_mode.x), tile = n + 2;\n"
    "  ivec2 px = ivec2(gl_FragCoord.xy), cellxy = px / tile;\n"
    "  vec3 dir = oct_decode((vec2(interior(px - cellxy*tile - 1, n)) + 0.5)/float(n)*2.0 - 1.0);\n"
    "  int probe = cellxy.x % NX + NX*(cellxy.x / NX + NZ*cellxy.y);\n"
    "  bool fresh = texelFetch(u_fresh, ivec2(probe % 64, probe / 64), 0).r > 0.5;\n"
    "  if (probe % 2 != int(u_half)) { frag = vec4(0.0, 0.0, 0.0, fresh ? 1.0 : 0.0); return; }\n"
    "  mat3 rot = mat3(u_rot0.xyz, u_rot1.xyz, u_rot2.xyz);\n"
    "  vec4 sum = vec4(0.0);\n"
    "  for (int r = 0; r < RAYS; ++r) {\n"
    "    vec4 ray = texelFetch(u_rays, ivec2(r, probe / 2), 0);\n"
    "    float w = max(0.0, dot(dir, rot*fib(r)));\n"
    "    if (u_mode.y > 0.5) { w = pow(w, 30.0); float dist = min(abs(ray.a), u_mode.w); sum += vec4(dist*w, dist*dist*w, 0.0, w); }\n"
    "    else if (ray.a >= 0.0 && ray.a <= u_mode.w) sum += vec4(ray.rgb*w, w); }\n"
    "  if (sum.w <= 1e-5) { frag = vec4(0.0, 0.0, 0.0, fresh ? 1.0 : 0.0); return; }\n"
    "  frag = vec4(u_mode.y > 0.5 ? vec3(sum.xy/sum.w, 0.0) : sum.rgb/sum.w, fresh ? 1.0 : 1.0 - u_mode.z); }\n";

/* One texel a probe: 0 where more than a quarter of its rays met the back of a face, it stands inside or behind a wall. */
static const char *DDGI_CLASSIFY_FS =
    "#version 330\n" DDGI_GLSL_COMMON
    "out vec4 frag;\n"
    "uniform sampler2D u_rays;\n"
    "uniform float u_half;\n"
    "void main(){\n"
    "  ivec2 px = ivec2(gl_FragCoord.xy); int probe = px.x + 64*px.y;\n"
    "  if (probe >= NX*NY*NZ || probe % 2 != int(u_half)) { frag = vec4(0.0); return; }\n"
    "  float back = 0.0;\n"
    "  for (int r = 0; r < RAYS; ++r) if (texelFetch(u_rays, ivec2(r, probe / 2), 0).a < 0.0) back += 1.0;\n"
    "  frag = vec4(vec3(back > 0.25*float(RAYS) ? 0.0 : 1.0), 1.0); }\n";

/* What both the measure and the mix need: a screen pixel's world place, normal and the probes' light there. */
#define DDGI_GLSL_APPLY                                                                                                               \
    "uniform sampler2D u_tex, u_depth, u_irr, u_dist, u_state;\n"                                                                   \
    "uniform vec4 u_ddgi;    /* strength, 1 for the light alone, spacing, the farthest */\n"                                         \
    "uniform vec4 u_proj;    /* focal x, focal y, centre x, centre y, in texels of the depth */\n"                                   \
    "uniform vec4 u_texel;   /* one texel in uv, one native pixel in texels */\n"                                                    \
    "uniform vec4 u_back0, u_back1, u_back2, u_offset, u_origin, u_fade;  /* u_back: inverse turn, w the shift */\n"                \
    "float cam(vec2 uv){ float z = texture(u_depth, uv).r; return (z < 0.00002 || z > 0.99998) ? 0.0 : 16.0/(1.0 - z); }\n"         \
    "float flat_surf(vec2 uv){\n"                                                                                                    \
    "  float mark = texelFetch(u_tex, clamp(ivec2(uv*vec2(textureSize(u_tex, 0))), ivec2(0), textureSize(u_tex, 0) - 1), 0).a;\n"   \
    "  return ((mark > 0.4 && mark < 0.6) || (mark > 0.7 && mark < 0.8) || abs(mark - 0.95) < 0.02) ? 0.0 : cam(uv); }\n"          \
    "bool flat_picture(float mark){ return abs(mark - 0.75) < 0.02 || abs(mark - 0.95) < 0.02; }\n"                                  \
    "vec3 at(vec2 uv, float d){ return vec3((uv/u_texel.xy - u_proj.zw)/u_proj.xy*d, d); }\n"                                        \
    "vec3 edge(vec2 uv, vec2 step, vec3 p, float d){\n"                                                                              \
    "  float a = flat_surf(uv - step), b = flat_surf(uv + step);\n"                                                                  \
    "  if (a == 0.0 && b == 0.0) return vec3(0.0);\n"                                                                                \
    "  return (b == 0.0 || (a != 0.0 && abs(a - d) < abs(b - d))) ? p - at(uv - step, a) : at(uv + step, b) - p; }\n"               \
    "ivec2 state_texel(ivec3 cell){ ivec3 s = slot_of(cell); int p = s.x + NX*(s.z + NZ*s.y); return ivec2(p % 64, p / 64); }\n"    \
    "vec3 back_turn(vec3 c){ return u_back0.xyz*c.x + u_back1.xyz*c.y + u_back2.xyz*c.z; }\n"                                        \
    "vec3 probe_light(vec3 p, vec3 n, vec3 eye){\n"                                                                                  \
    "  float spacing = u_ddgi.z; vec2 irr_size = vec2(textureSize(u_irr, 0)), dist_size = vec2(textureSize(u_dist, 0));\n"          \
    "  vec3 biased = p + (n*0.25 + eye*0.75)*(0.2*spacing);\n"                                                                       \
    "  vec3 g = biased/spacing; ivec3 base = ivec3(floor(g)); vec3 f = g - vec3(base);\n"                                            \
    "  ivec3 lo = ivec3(u_origin.xyz), hi = lo + ivec3(NX, NY, NZ) - 1;\n"                                                           \
    "  vec3 sum = vec3(0.0); float ws = 0.0;\n"                                                                                      \
    "  for (int i = 0; i < 8; ++i) {\n"                                                                                              \
    "    ivec3 off = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1), c = clamp(base + off, lo, hi);\n"                                     \
    "    if (texelFetch(u_state, state_texel(c), 0).r < 0.5) continue;\n"                                                            \
    "    vec3 probe = vec3(c)*spacing, to_probe = probe - p; float len = length(to_probe);\n"                                        \
    "    vec3 dir = len > 0.001 ? to_probe/len : n;\n"                                                                               \
    "    float w = (dot(dir, n) + 1.0)*0.5; w = w*w + 0.2;\n"                                                                        \
    "    vec3 back = biased - probe; float dist = length(back);\n"                                                                   \
    "    vec2 m = texture(u_dist, tile_uv(c, dist > 0.001 ? back/dist : n, DIST, dist_size)).rg;\n"                                 \
    "    float cheb = 1.0;\n"                                                                                                        \
    "    if (dist > m.x) { float var = abs(m.y - m.x*m.x), dd = dist - m.x; cheb = var/(var + dd*dd); cheb = cheb*cheb*cheb; }\n"    \
    "    w *= max(0.05, cheb);\n"                                                                                                    \
    "    if (w < 0.2) w *= w*w*25.0;\n"                                                                                              \
    "    vec3 w3 = mix(1.0 - f, f, vec3(off)); w *= w3.x*w3.y*w3.z;\n"                                                               \
    "    vec3 e = texture(u_irr, tile_uv(c, n, IRR, irr_size)).rgb;\n"                                                               \
    "    if (dot(e, vec3(1.0)) <= 1e-5) continue;\n"                                                                                 \
    "    sum += e*w; ws += w; }\n"                                                                                                   \
    "  return ws > 1e-5 ? sum/ws : vec3(-1.0); }\n"                                                                                  \
    "/* world_only leaves out sprites and blended pixels, which have no surface of their own. */\n"                                  \
    "bool pixel_light(vec2 uv, bool world_only, out vec3 e){\n"                                                                      \
    "  e = vec3(0.0);\n"                                                                                                             \
    "  float d = world_only ? flat_surf(uv) : cam(uv);\n"                                                                            \
    "  if (d <= 0.0) return false;\n"                                                                                                \
    "  vec3 pc = at(uv, d);\n"                                                                                                       \
    "  vec3 nc = cross(edge(uv, vec2(0.0, u_texel.w*u_texel.y), pc, d), edge(uv, vec2(u_texel.w*u_texel.x, 0.0), pc, d));\n"         \
    "  if (dot(nc, nc) <= 1e-12) return false;\n"                                                                                    \
    "  nc = normalize(nc); if (dot(nc, pc) > 0.0) nc = -nc;\n"                                                                       \
    "  vec3 v = back_turn(pc - vec3(u_back0.w, u_back1.w, u_back2.w));\n"                                                            \
    "  e = probe_light(v + u_offset.xyz, normalize(back_turn(nc)), normalize(back_turn(-pc)));\n"                                    \
    "  return e.x >= 0.0; }\n"

#define DDGI_SAMPLES_X 32
#define DDGI_SAMPLES_Y 18

/* The probes' light at a grid of the picture's world pixels. */
static const char *DDGI_SAMPLE_FS =
    "#version 330\n" DDGI_GLSL_COMMON DDGI_GLSL_APPLY
    "out vec4 frag;\n"
    "uniform vec4 u_rect;  /* the part of the textures shown */\n"
    "void main(){\n"
    "  vec2 uv = mix(u_rect.xy, u_rect.zw, gl_FragCoord.xy/vec2(" DDGI_STR(DDGI_SAMPLES_X) ".0, " DDGI_STR(DDGI_SAMPLES_Y) ".0));\n"
    "  vec3 e; frag = pixel_light(uv, true, e) ? vec4(e, 1.0) : vec4(0.0); }\n";

/* Their average, blended slowly into one texel with its weight in alpha: the light the picture keeps on the whole. */
static const char *DDGI_AVERAGE_FS =
    "#version 330\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_samples;\n"
    "void main(){\n"
    "  vec4 sum = vec4(0.0);\n"
    "  for (int y = 0; y < " DDGI_STR(DDGI_SAMPLES_Y) "; ++y) for (int x = 0; x < " DDGI_STR(DDGI_SAMPLES_X) "; ++x)\n"
    "    sum += texelFetch(u_samples, ivec2(x, y), 0);\n"
    "  if (sum.w < 1.0) discard;\n"
    "  frag = vec4(sum.rgb/sum.w, 1.0); }\n";

/* The shading's mix with the probes' light and the lamps': SSAO_MIX_FS, the probes' light at the pixel's world place relative to
 * the picture's average, then the casters' shadow and the dynamic lights' light from the half-size lamp pass, in linear light. */
static const char *DDGI_MIX_FS =
    "#version 330\n" DDGI_GLSL_COMMON DDGI_GLSL_APPLY
    "in vec2 v_uv; out vec4 frag;\n"
    "uniform sampler2D u_shade, u_exposure, u_lamps;\n"
    "uniform vec4 u_span, u_uv_rect;\n"
    "uniform vec4 u_lit;  /* 1 with the lamps' texture, 1 to show it alone, 1 with the probes' light */\n"
    "uniform vec4 u_fog;  /* the game's fog as in the lamp pass */\n"
    "float fog_at(vec2 uv, float d){\n"
    "  if (u_fog.w < 0.5 || d <= 0.0) return 0.0;\n"
    "  float side = abs(2.0*(uv.x - u_uv_rect.x)/(u_uv_rect.z - u_uv_rect.x) - 1.0);\n"
    "  return clamp((d*(1.0 + u_fog.z*side) - u_fog.x)/u_fog.y, 0.0, 1.0); }\n"
    "vec3 soft(vec3 r){ return mix(r, 1.0 + (r - 1.0)/(1.0 + 0.5*(r - 1.0)), step(1.0, r)); }\n"
    "/* The half-size lamp texture at uv, its four texels weighed by how near their depth is to the pixel's. */\n"
    "vec4 lamp_at(vec2 uv){\n"
    "  float d = cam(uv);\n"
    "  vec2 size = vec2(textureSize(u_lamps, 0)), span = u_uv_rect.zw - u_uv_rect.xy;\n"
    "  vec2 place = (uv - u_uv_rect.xy)/span*size - 0.5; ivec2 base = ivec2(floor(place)); vec2 f = place - vec2(base);\n"
    "  vec4 sum = vec4(0.0); float ws = 0.0;\n"
    "  for (int i = 0; i < 4; ++i) {\n"
    "    ivec2 q = clamp(base + ivec2(i & 1, i >> 1), ivec2(0), ivec2(size) - 1);\n"
    "    float dq = cam(u_uv_rect.xy + (vec2(q) + 0.5)/size*span);\n"
    "    float w = ((i & 1) != 0 ? f.x : 1.0 - f.x)*((i >> 1) != 0 ? f.y : 1.0 - f.y) + 1e-3;\n"
    "    w *= (d > 0.0 && dq > 0.0) ? exp(-abs(dq - d)/(0.02*d + 2.0)) : (d <= 0.0 && dq <= 0.0 ? 1.0 : 1e-3);\n"
    "    sum += texelFetch(u_lamps, q, 0)*w; ws += w; }\n"
    "  return sum/ws; }\n"
    "/* The fog's share of a pixel cannot be taken out of it (the game's vertex light scales the fog colour too, and a\n"
    "   * subtraction flattened half-fogged walls), so the lamps' factor and light fade out with the fog instead. */\n"
    "vec3 lamped(vec3 c, vec3 gain, vec4 lamp, float f){\n"
    "  c = max(c, vec3(0.0))*max(gain, vec3(0.0));\n"
    "  float a = mix(1.0, max(lamp.a, 0.0), 1.0 - f);\n"
    "  return pow(max(pow(c, vec3(2.2))*a + lamp.rgb*(1.0 - f), vec3(0.0)), vec3(1.0/2.2)); }\n"
    "void main(){\n"
    "  ivec2 at_shade = ivec2((gl_FragCoord.xy - u_span.xy) / max(u_span.w, 1.0)), most = textureSize(u_shade, 0) - 1;\n"
    "  vec4 here = texelFetch(u_shade, clamp(at_shade, ivec2(0), most), 0);\n"
    "  vec4 seen = texture(u_tex, v_uv);\n"
    "  vec3 c = seen.rgb;\n"
    "  if (u_span.z > 4.5) { frag = vec4(here.g == 0.0 ? vec3(0.0, 0.0, 0.25) : vec3(here.r), 1.0); return; }\n"
    "  if (u_span.z > 2.5) { frag = vec4(here.g == 0.0 ? vec3(0.0, 0.0, 0.25) : here.rgb, 1.0); return; }\n"
    "  /* A flat picture over the world (the weapon in hand, the HUD) is no surface of it: it keeps its colours. */\n"
    "  bool over = flat_picture(seen.a);\n"
    "  vec4 lamp = u_lit.x > 0.5 && !over ? lamp_at(v_uv) : vec4(0.0, 0.0, 0.0, 1.0);\n"
    "  if (u_lit.y > 0.5) { frag = vec4(vec3(lamp.a*0.5) + pow(max(lamp.rgb, vec3(0.0)), vec3(1.0/2.2)), 1.0); return; }\n"
    "  if (over && u_span.z < 1.5) { frag = seen; return; }\n"
    "  float f = u_lit.x > 0.5 ? fog_at(v_uv, cam(v_uv)) : 0.0;\n"
    "  frag = vec4(u_span.z > 1.5 ? vec3(0.0, 0.0, 0.25) : lamped(c, vec3(1.0), lamp, f), seen.a);\n"
    "  if (here.g == 0.0) { if (u_ddgi.y > 0.5) frag = vec4(0.0, 0.0, 0.25, seen.a); return; }\n"
    "  float lit = 0.0, count = 0.0;\n"
    "  for (int y = -2; y < 2; ++y) for (int x = -2; x < 2; ++x) {\n"
    "    vec4 near = texelFetch(u_shade, clamp(at_shade + ivec2(x, y), ivec2(0), most), 0);\n"
    "    if (near.g == 0.0 || abs(near.g - here.g) > 2.5/255.0) continue;\n"
    "    lit += near.r; count += 1.0; }\n"
    "  lit = count > 0.0 ? lit/count : here.r;\n"
    "  vec3 light = vec3(1.0), e;\n"
    "  if (u_lit.z > 0.5) {\n"
    "    vec4 kept = texelFetch(u_exposure, ivec2(0), 0);\n"
    "    vec3 average = kept.rgb/max(kept.w, 1e-4); float mean = dot(average, vec3(0.2126, 0.7152, 0.0722));\n"
    "    if (kept.w > 0.05 && mean > 1e-4 && pixel_light(v_uv, true, e)) {\n"
    "      float lum = dot(e, vec3(0.2126, 0.7152, 0.0722)), d = cam(v_uv);\n"
    "      vec3 hue = (e/max(lum, 1e-4))/max(average/mean, vec3(0.05));\n"
    "      vec3 relative = soft(clamp(lum/mean*mix(vec3(1.0), hue, 0.5), 0.0, 8.0));\n"
    "      float clear = u_fade.y > u_fade.x ? 1.0 - clamp((d - u_fade.x)/(u_fade.y - u_fade.x), 0.0, 1.0) : 1.0;\n"
    "      light = max(mix(vec3(1.0), relative, u_ddgi.x*clear), vec3(0.0));\n"
    "      if (u_ddgi.y > 0.5) { frag = vec4(clamp(relative*0.5, 0.0, 1.0), seen.a); return; }\n"
    "    } else if (u_ddgi.y > 0.5) { frag = vec4(0.25, 0.0, 0.25, seen.a); return; }\n"
    "  }\n"
    "  frag = vec4(u_span.z > 1.5 ? vec3(lit) : lamped(c, lit*light, lamp, f), seen.a); }\n";

/* Half size: the casters' shadow from the level's lights and under themselves (a), the dynamic lights' light times the
 * surface's own colour (rgb, linear). A caster is an ellipsoid for the shade under it and its own picture, turned to face the
 * light, for the shadow it throws; a light's share of a pixel is its estimate over all of them. */
static const char *DDGI_LAMP_FS =
    "#version 330\n" DDGI_GLSL_COMMON DDGI_GLSL_APPLY DDGI_GLSL_BVH
    "#define CASTERS " DDGI_STR(GPU_DDGI_CASTERS) "\n#define LAMPS " DDGI_STR(GPU_DDGI_LAMPS) "\n"
    "out vec4 frag;\n"
    "uniform vec4 u_rect, u_size, u_eye;  /* the part shown, this target's size, the camera's world place */\n"
    "uniform vec4 u_count;   /* casters, lamps, unused, the light every pixel has besides the lamps */\n"
    "uniform vec4 u_shadow;  /* shadow strength, contact strength, the most a shadow takes, the dynamic lights' strength */\n"
    "uniform vec4 u_fog;     /* the game's fog: start and length in camera depth, the side term, 1 when known */\n"
    "/* A caster: centre and half width, half height. A lamp: place and size (a dynamic one's reach), linear colour and power,\n"
    " * normal and weight, negative for a dynamic one. */\n"
    "uniform vec4 u_caster[CASTERS*2], u_lamp[LAMPS*3];\n"
    "/* A caster's picture, two texels a row: half width, half height and its level left to right; its centre. Its mask: a\n"
    " * row of 32 texels a texel, bit 0 at the left. */\n"
    "uniform sampler2D u_cast;\n"
    "uniform usampler2D u_mask;\n"
    "float cap_overlap(float ra, float rb, float d){\n"
    "  if (d >= ra + rb) return 0.0;\n"
    "  float whole = min(1.0, (1.0 - cos(min(ra, rb)))/max(1.0 - cos(ra), 1e-6)), inner = abs(ra - rb);\n"
    "  if (d <= inner) return whole;\n"
    "  return whole*smoothstep(0.0, 1.0, 1.0 - (d - inner)/max(ra + rb - inner, 1e-5)); }\n"
    "vec3 caster_scale(int i){ return vec3(1.0/u_caster[2*i].w, 1.0/u_caster[2*i + 1].x, 1.0/u_caster[2*i].w); }\n"
    "/* The share of a light of radius size at q that caster i hides from p, in the space where the caster is a unit sphere. */\n"
    "float hidden(vec3 p, vec3 q, float size, int i){\n"
    "  vec3 k = caster_scale(i), ps = (p - u_caster[2*i].xyz)*k, qs = (q - u_caster[2*i].xyz)*k;\n"
    "  float dc = length(ps); if (dc <= 1.0) return 1.0;\n"
    "  vec3 dir = qs - ps; float dq = length(dir); if (dq < 1e-4) return 0.0; dir /= dq;\n"
    "  float along = dot(-ps, dir); if (along <= 0.0 || along >= dq) return 0.0;\n"
    "  float rs = asin(1.0/dc), phi = acos(clamp(dot(dir, -ps/dc), -1.0, 1.0));\n"
    "  float rl = clamp(atan(size*(k.x + k.y + k.z)/3.0, dq), 0.02, 1.2);\n"
    "  return cap_overlap(rl, rs, phi); }\n"
    "/* The share of a far light of angular radius angle along dir that caster i hides from p. */\n"
    "float hidden_far(vec3 p, vec3 dir, float angle, int i){\n"
    "  vec3 k = caster_scale(i), ps = (p - u_caster[2*i].xyz)*k, ds = normalize(dir*k);\n"
    "  float dc = length(ps); if (dc <= 1.0) return 1.0;\n"
    "  if (dot(-ps, ds) <= 0.0) return 0.0;\n"
    "  return cap_overlap(angle, asin(1.0/dc), acos(clamp(dot(ds, -ps/dc), -1.0, 1.0))); }\n"
    "/* What caster i takes of the light round p, n: a sphere's form factor, cut off a few radii out. */\n"
    "float contact(vec3 p, vec3 n, int i){\n"
    "  vec3 k = caster_scale(i), ps = (p - u_caster[2*i].xyz)*k;\n"
    "  float dc = length(ps); if (dc <= 1.0) return 1.0;\n"
    "  return max(0.0, dot(normalize(n/k), -ps/dc))/(dc*dc)*(1.0 - smoothstep(2.0, 3.5, dc)); }\n"
    "/* Caster i's mask at f over its box, bilinear between its cells. */\n"
    "float mask_at(int i, vec2 f){\n"
    "  vec2 g = f*32.0 - 0.5; ivec2 c = ivec2(floor(g)); vec2 w = g - vec2(c); float v = 0.0;\n"
    "  for (int dy = 0; dy < 2; ++dy) {\n"
    "    int row = c.y + dy; if (row < 0 || row > 31) continue;\n"
    "    uint bits = texelFetch(u_mask, ivec2(row, i), 0).r; float wy = dy == 0 ? 1.0 - w.y : w.y;\n"
    "    for (int dx = 0; dx < 2; ++dx) {\n"
    "      int col = c.x + dx;\n"
    "      if (col >= 0 && col <= 31 && ((bits >> uint(col)) & 1u) != 0u) v += wy*(dx == 0 ? 1.0 - w.x : w.x); } }\n"
    "  return v; }\n"
    "/* What caster i's picture hides from p of a light toward l of angular radius spread, reach away: the picture stands at\n"
    " * the caster turned to face the light, four taps over the penumbra soften its edge. own: p is on a sprite, none of its own. */\n"
    "float picture_hides(vec3 p, vec3 l, float spread, float reach, int i, bool own){\n"
    "  vec4 a = texelFetch(u_cast, ivec2(0, i), 0);\n"
    "  vec3 centre = texelFetch(u_cast, ivec2(1, i), 0).xyz; float level = length(l.xz);\n"
    "  if (level < 1e-3 || a.x <= 0.0 || a.y <= 0.0) return 0.0;\n"
    "  vec2 facing = l.xz/level, side = vec2(-facing.y, facing.x), rel = p.xz - centre.xz;\n"
    "  if (dot(side, a.zw) < 0.0) side = -side;\n"
    "  if (own && abs(dot(rel, side)) < a.x && abs(p.y - centre.y) < a.y && abs(dot(rel, facing)) < 0.5*a.x) return 0.0;\n"
    "  float t = -dot(rel, facing)/level; if (t <= 0.0 || t >= reach) return 0.0;\n"
    "  vec3 q = p + l*t;\n"
    "  vec2 at = vec2(dot(q.xz - centre.xz, side)/a.x, (q.y - centre.y)/a.y)*0.5 + 0.5, blur = vec2(spread*t*0.5)/a.xy;\n"
    "  float hide = 0.0;\n"
    "  for (int k = 0; k < 4; ++k) {\n"
    "    vec2 f = at + blur*vec2((k & 1) != 0 ? 0.5 : -0.5, (k & 2) != 0 ? 0.5 : -0.5);\n"
    "    if (f.x <= -0.05 || f.x >= 1.05 || f.y <= -0.05 || f.y >= 1.05) continue;\n"
    "    hide += 0.25*mask_at(i, f); }\n"
    "  return hide; }\n"
    "/* A light straight above has no upright picture to meet: the ellipsoid takes over as the light climbs. */\n"
    "float caster_hides(vec3 p, vec3 q, float size, int i, bool own){\n"
    "  vec3 d = q - p; float dist = length(d); vec3 l = d/max(dist, 1e-3);\n"
    "  float upright = smoothstep(0.2, 0.4, length(l.xz));\n"
    "  return mix(upright < 1.0 ? hidden(p, q, size, i) : 0.0, upright > 0.0 ? picture_hides(p, l, size/max(dist, 1.0), dist, i, own) : 0.0, upright); }\n"
    "float caster_hides_far(vec3 p, vec3 dir, float angle, int i){\n"
    "  float upright = smoothstep(0.2, 0.4, length(dir.xz));\n"
    "  return mix(upright < 1.0 ? hidden_far(p, dir, angle, i) : 0.0, upright > 0.0 ? picture_hides(p, dir, angle, 1e9, i, false) : 0.0, upright); }\n"
    "vec3 oct16(int q){ return oct_decode(vec2(float(q & 255), float((q >> 8) & 255))/127.5 - 1.0); }\n"
    "/* The smooth normal at a point of face i of the frame's BVH, zero where the face has none. */\n"
    "vec3 smooth_at(int i, float hu, float hv){\n"
    "  int a = int(fetch(u_tris, i*5 + 3).w), b = int(fetch(u_tris, i*5 + 4).w);\n"
    "  if (a == 0 && b == 0) return vec3(0.0);\n"
    "  vec3 s = oct16(a & 65535)*(1.0 - hu - hv) + oct16(((a >> 16) & 255) | ((b & 255) << 8))*hu + oct16((b >> 8) & 65535)*hv;\n"
    "  return dot(s, s) > 1e-6 ? normalize(s) : vec3(0.0); }\n"
    "bool blocked(vec3 o, vec3 q){\n"
    "  vec3 d = q - o; float len = length(d); if (len < 40.0) return false;\n"
    "  float best = len*0.95 - 16.0, hu, hv;\n"
    "  return bvh_hit(o, d/len, 2.0, best, hu, hv, true, true) >= 0; }\n"
    "void main(){\n"
    "  vec2 uv = mix(u_rect.xy, u_rect.zw, gl_FragCoord.xy/u_size.xy);\n"
    "  ivec2 tsize = textureSize(u_tex, 0);\n"
    "  float mark = texelFetch(u_tex, clamp(ivec2(uv*vec2(tsize)), ivec2(0), tsize - 1), 0).a;\n"
    "  bool sprite = mark > 0.4 && mark < 0.6;\n"
    "  float d = sprite ? cam(uv) : flat_surf(uv);\n"
    "  frag = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "  if (d <= 0.0) return;\n"
    "  float fog = 0.0;\n"
    "  if (u_fog.w > 0.5) fog = clamp((d*(1.0 + u_fog.z*abs(2.0*(uv.x - u_rect.x)/(u_rect.z - u_rect.x) - 1.0)) - u_fog.x)/u_fog.y, 0.0, 1.0);\n"
    "  if (fog >= 1.0) return;\n"
    "  vec3 pc = at(uv, d), p = back_turn(pc - vec3(u_back0.w, u_back1.w, u_back2.w)) + u_offset.xyz, n;\n"
    "  if (sprite) n = normalize(u_eye.xyz - p);\n"
    "  else {\n"
    "    vec3 nc = cross(edge(uv, vec2(0.0, u_texel.w*u_texel.y), pc, d), edge(uv, vec2(u_texel.w*u_texel.x, 0.0), pc, d));\n"
    "    if (dot(nc, nc) <= 1e-12) return;\n"
    "    nc = normalize(nc); if (dot(nc, pc) > 0.0) nc = -nc;\n"
    "    n = normalize(back_turn(nc)); }\n"
    "  /* The face under the pixel along the camera's ray: its colour, and its smooth normal for the light it takes. */\n"
    "  int seen = -1; float seen_u = 0.0, seen_v = 0.0; vec3 lit_n = n;\n"
    "  if (!sprite) {\n"
    "    vec3 dir = p - u_eye.xyz; float len = length(dir), best = len + 100.0; dir /= max(len, 1e-3);\n"
    "    seen = bvh_hit(u_eye.xyz, dir, max(len - 200.0, 1.0), best, seen_u, seen_v, true, false);\n"
    "    if (seen >= 0) { vec3 s = smooth_at(seen, seen_u, seen_v); if (dot(s, n) > 0.5) lit_n = s; } }\n"
    "  int casters = int(u_count.x), lamps = int(u_count.y), near[CASTERS], nearby = 0;\n"
    "  for (int j = 0; j < casters; ++j) { vec3 to = u_caster[2*j].xyz - p; if (dot(to, to) < 1.6e7) near[nearby++] = j; }\n"
    "  float shadow = 1.0;\n"
    "  if (!sprite && nearby > 0) {\n"
    "    float share[LAMPS], total = u_count.w, shade = 0.0, open = 1.0;\n"
    "    for (int i = 0; i < lamps; ++i) {\n"
    "      share[i] = 0.0;\n"
    "      vec4 a = u_lamp[3*i], b = u_lamp[3*i + 1], c = u_lamp[3*i + 2];\n"
    "      if (c.w <= 0.0) continue;\n"
    "      vec3 l = a.xyz - p; float dist = length(l); l /= max(dist, 1e-3);\n"
    "      float ndl = dot(n, l); if (ndl <= 0.0) continue;\n"
    "      float side = dot(c.xyz, c.xyz) > 0.25 ? max(0.15, dot(c.xyz, -l)) : 1.0;\n"
    "      share[i] = b.w*ndl*side*c.w/(dist*dist + a.w*a.w + 1.44e6);\n"
    "      total += share[i]; }\n"
    "    for (int i = 0; i < lamps; ++i) {\n"
    "      if (share[i] <= 0.03*total) continue;\n"
    "      vec4 a = u_lamp[3*i]; float h = 0.0;\n"
    "      for (int j = 0; j < nearby; ++j) h = max(h, caster_hides(p, a.xyz, a.w, near[j], false));\n"
    "      if (h > 0.01 && blocked(p + n*8.0, a.xyz)) h = 0.0;\n"
    "      shade += share[i]/total*h; }\n"
    "    for (int j = 0; j < nearby; ++j) open *= 1.0 - min(1.0, contact(p, n, near[j]));\n"
    "    shadow = 1.0 - clamp(u_shadow.x*shade + u_shadow.y*(1.0 - open), 0.0, u_shadow.z); }\n"
    "  vec3 added = vec3(0.0);\n"
    "  /* The sun takes its share of the game's own light and gives it back by how the surface faces it: sunlit ground stays\n"
    "   * as the game lit it, shade keeps the rest, a wall turned to the sun gets more. */\n"
    "  if (u_sun.w > 0.0 && u_sun_colour.w > 0.0) {\n"
    "    float facing = sprite ? 0.7 : dot(lit_n, u_sun.xyz), lit = 0.0;\n"
    "    if (facing > 0.0 && (sprite || dot(n, u_sun.xyz) > -0.2) && !sun_blocked(p + n*8.0)) {\n"
    "      lit = facing;\n"
    "      if (!sprite) for (int j = 0; j < nearby && lit > 0.0; ++j) lit *= 1.0 - caster_hides_far(p, u_sun.xyz, 0.05, near[j]); }\n"
    "    shadow *= 1.0 - u_sun_colour.w + u_sun_colour.w*u_sun.w*min(lit/max(abs(u_sun.y), 0.2), 1.6); }\n"
    "  for (int i = 0; i < lamps && u_shadow.w > 0.0; ++i) {\n"
    "    vec4 a = u_lamp[3*i], b = u_lamp[3*i + 1], c = u_lamp[3*i + 2];\n"
    "    if (c.w >= 0.0) continue;\n"
    "    vec3 l = a.xyz - p; float dist = length(l); if (dist >= a.w) continue; l /= max(dist, 1e-3);\n"
    "    float ndl = sprite ? 0.6 + 0.4*dot(n, l) : dot(lit_n, l); if (ndl <= 0.0) continue;\n"
    "    float x = dist/a.w, r = dist/(0.35*a.w), fall = (1.0 - x*x)*(1.0 - x*x)/(1.0 + r*r), vis = 1.0;\n"
    "    if (!sprite && blocked(p + n*8.0, a.xyz)) vis = 0.0;\n"
    "    for (int j = 0; j < nearby && vis > 0.0; ++j) vis *= 1.0 - caster_hides(p, a.xyz, 60.0, near[j], sprite);\n"
    "    float lum = dot(b.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
    "    added += b.rgb/max(lum, 1e-3)*u_shadow.w*(b.w/(b.w + 20000.0))*2.0*(-c.w)*fall*ndl*vis; }\n"
    "  if (dot(added, vec3(1.0)) > 1e-4) {\n"
    "    /* The surface's own colour: the texel the camera's ray meets, before the game's vertex light. */\n"
    "    vec3 albedo = texture(u_tex, uv).rgb;\n"
    "    if (seen >= 0) {\n"
    "      int hit = seen; float hu = seen_u, hv = seen_v;\n"
    "      vec4 v0 = fetch(u_tris, hit*5), v1 = fetch(u_tris, hit*5 + 1), v2 = fetch(u_tris, hit*5 + 2);\n"
    "      vec4 m = fetch(u_tris, hit*5 + 3), col = fetch(u_tris, hit*5 + 4);\n"
    "      float w0 = 1.0 - hu - hv;\n"
    "      albedo = (int(m.z) & 1) != 0 ? col5(texel(uv8(v0.w)*w0 + uv8(v1.w)*hu + uv8(v2.w)*hv, int(m.y), int(m.x)))\n"
    "                                   : rgb8(col.x)*w0 + rgb8(col.y)*hu + rgb8(col.z)*hv; }\n"
    "    added *= pow(albedo, vec3(2.2)); }\n"
    "  frag = vec4(added, shadow); }\n";

typedef struct {
    GLint nodes, tris, irr, vram, origin, rot0, rot1, rot2, camera, sky, row0, row1, row2, offset, view, frame, frame_at, state,
        level_nodes, level_tris, sun, sun_colour, level;
} DdgiTraceUniforms;
typedef struct {
    GLint rays, fresh, rot0, rot1, rot2, mode, half;
} DdgiUpdateUniforms;
typedef struct {
    GLint tex, shade, depth, irr, dist, exposure, state, span, ddgi, proj, texel, back0, back1, back2, offset, origin, fade, uv_rect,
        rect;
} DdgiApplyUniforms;
typedef struct {
    GLuint samples_tex, samples_fbo, average_tex, average_fbo;
    int frames;
} DdgiScreen;
typedef struct {
    GLint nodes, tris, vram, rect, size, eye, count, shadow, caster, lamp, level_nodes, level_tris, sun, sun_colour, level, fog,
        cast, mask;
} DdgiLampUniforms;
/* A camera, the BVH drawn under it and its casters and lights: what one context shows. */
typedef struct DdgiFrame {
    int valid, bvh;
    uint32_t pose; /* the camera's: in-between frames move the camera only between two of them */
    float rows[3][4], offset[3], eye[3];
    float back[3][3]; /* the inverse of the rows' turn, a column each: camera to world */
    int level;                   /* the level BVH it was drawn with, -1 for none */
    float sun[4], sun_colour[4]; /* as the shaders' u_sun and u_sun_colour, strength 0 for no sun */
    GpuDdgiScene scene;
} DdgiFrame;
/* Programs are made in each context: a program's uniforms are shared between contexts with it. */
typedef struct {
    int built, failed;
    GLuint sample_prog, average_prog, mix_prog, lamp_prog;
    DdgiApplyUniforms su, mu, lu;
    DdgiLampUniforms la;
    GLint average_samples, mix_lamps, mix_lit, mix_fog;
    DdgiScreen screen;
    GLuint lamp_tex, lamp_fbo, cast_tex, mask_tex;
    int lamp_w, lamp_h, lamp_ready;
    DdgiFrame frame;
} DdgiContext;

static int s_ddgi_light = 0, s_ddgi_light_view = 0, s_ddgi_ready = 0, s_ddgi_failed = 0, s_ddgi_applies = 0;
static float s_ddgi_strength = 1.0f, s_ddgi_bounce = 0.0f, s_ddgi_glow = 2.0f;
static int s_ddgi_shadows = 0, s_ddgi_lamps = 0, s_ddgi_lamp_view = 0;
/* A sun for a mission with open sky: degrees round from +z, degrees up, strength, the share of baked light shade takes. */
typedef struct {
    float azimuth, elevation, strength, shade, colour[3];
} DdgiSun;
static DdgiSun s_ddgi_suns[16] = {[2] = {35.0f, 50.0f, 1.1f, 0.5f, {1.0f, 0.93f, 0.8f}}};
static int s_ddgi_sun = 0, s_ddgi_mission = -1;
static int ddgi_sun_active(void) {
    return s_ddgi_sun && s_ddgi_mission >= 0 && s_ddgi_mission < 16 && s_ddgi_suns[s_ddgi_mission].strength > 0.0f &&
           s_ddgi_suns[s_ddgi_mission].shade > 0.0f;
}
static float s_ddgi_shadow_strength = 1.0f, s_ddgi_contact = 1.0f, s_ddgi_lamp_strength = 1.5f;
static DdgiContext s_ddgi_ctx[2]; /* the frame loop's context, the in-between presenter's */
static DdgiFrame s_ddgi_frame_now;
static int s_ddgi_bvh_next;
static GLuint s_ddgi_query;
static int s_ddgi_query_open;
static GLuint s_ddgi_trace_prog, s_ddgi_update_prog, s_ddgi_classify_prog;
static GLuint s_ddgi_state_tex, s_ddgi_state_fbo;
static GLint s_ddgi_classify_rays = -1, s_ddgi_classify_half = -1;
static DdgiTraceUniforms s_ddgi_tu;
static DdgiUpdateUniforms s_ddgi_uu;
static GLuint s_ddgi_nodes_tex[2], s_ddgi_tris_tex[2], s_ddgi_fresh_tex, s_ddgi_rays_tex, s_ddgi_rays_fbo;
static GLuint s_ddgi_irr_tex, s_ddgi_irr_fbo, s_ddgi_dist_tex, s_ddgi_dist_fbo;
static GLsync s_ddgi_probe_fence;
static uint32_t s_ddgi_last_pose, s_ddgi_updates;
static int s_ddgi_slot_cell[DDGI_PROBES][3], s_ddgi_slots_known;
static DdgiNode *s_ddgi_nodes;
static int s_ddgi_node_count;
static DdgiBuildFace *s_ddgi_frame_faces;
static float *s_ddgi_staging;
static unsigned char s_ddgi_fresh[64 * 64], s_ddgi_pending[DDGI_PROBES];
static int s_ddgi_origin[3];

static void ssao_mode(void);
void gl_renderer_set_ddgi(int enabled);

static GLint ddgi_uniform(GLuint prog, const char *name) { return p_glGetUniformLocation(prog, name); }

static DdgiApplyUniforms ddgi_apply_locations(GLuint m) {
    return (DdgiApplyUniforms){ddgi_uniform(m, "u_tex"),   ddgi_uniform(m, "u_shade"),  ddgi_uniform(m, "u_depth"),
                               ddgi_uniform(m, "u_irr"),   ddgi_uniform(m, "u_dist"),   ddgi_uniform(m, "u_exposure"),
                               ddgi_uniform(m, "u_state"), ddgi_uniform(m, "u_span"),   ddgi_uniform(m, "u_ddgi"),
                               ddgi_uniform(m, "u_proj"),  ddgi_uniform(m, "u_texel"),  ddgi_uniform(m, "u_back0"),
                               ddgi_uniform(m, "u_back1"), ddgi_uniform(m, "u_back2"),  ddgi_uniform(m, "u_offset"),
                               ddgi_uniform(m, "u_origin"), ddgi_uniform(m, "u_fade"),  ddgi_uniform(m, "u_uv_rect"),
                               ddgi_uniform(m, "u_rect")};
}

static int ddgi_target(GLuint *tex, GLuint *fbo, GLenum internal, int w, int h, GLenum fmt, int linear) {
    *tex = make_tex(internal, w, h, fmt, GL_FLOAT);
    if (!*tex) return 0;
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    if (!make_fbo(fbo, *tex, 0)) return 0;
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, *fbo);
    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    return 1;
}

static int ddgi_init(void) {
    s_ddgi_trace_prog = build_program(DDGI_VS, DDGI_TRACE_FS);
    s_ddgi_update_prog = build_program(DDGI_VS, DDGI_UPDATE_FS);
    s_ddgi_classify_prog = build_program(DDGI_VS, DDGI_CLASSIFY_FS);
    if (!s_ddgi_trace_prog || !s_ddgi_update_prog || !s_ddgi_classify_prog) return 0;
    s_ddgi_classify_rays = ddgi_uniform(s_ddgi_classify_prog, "u_rays");
    s_ddgi_classify_half = ddgi_uniform(s_ddgi_classify_prog, "u_half");
    const GLuint t = s_ddgi_trace_prog;
    s_ddgi_tu = (DdgiTraceUniforms){ddgi_uniform(t, "u_nodes"), ddgi_uniform(t, "u_tris"),   ddgi_uniform(t, "u_irr"),
                                    ddgi_uniform(t, "u_vram"),  ddgi_uniform(t, "u_origin"), ddgi_uniform(t, "u_rot0"),
                                    ddgi_uniform(t, "u_rot1"),  ddgi_uniform(t, "u_rot2"),   ddgi_uniform(t, "u_camera"),
                                    ddgi_uniform(t, "u_sky"),   ddgi_uniform(t, "u_row0"),   ddgi_uniform(t, "u_row1"),
                                    ddgi_uniform(t, "u_row2"),  ddgi_uniform(t, "u_offset"), ddgi_uniform(t, "u_view"),
                                    ddgi_uniform(t, "u_frame"), ddgi_uniform(t, "u_frame_at"), ddgi_uniform(t, "u_state"),
                                    ddgi_uniform(t, "u_level_nodes"), ddgi_uniform(t, "u_level_tris"), ddgi_uniform(t, "u_sun"),
                                    ddgi_uniform(t, "u_sun_colour"), ddgi_uniform(t, "u_level")};
    const GLuint u = s_ddgi_update_prog;
    s_ddgi_uu = (DdgiUpdateUniforms){ddgi_uniform(u, "u_rays"), ddgi_uniform(u, "u_fresh"), ddgi_uniform(u, "u_rot0"),
                                     ddgi_uniform(u, "u_rot1"), ddgi_uniform(u, "u_rot2"),  ddgi_uniform(u, "u_mode"),
                                     ddgi_uniform(u, "u_half")};
    for (int i = 0; i < 2; ++i) {
        s_ddgi_nodes_tex[i] = make_tex(DDGI_GL_RGBA32F, 1024, DDGI_ROWS, GL_RGBA, GL_FLOAT);
        s_ddgi_tris_tex[i] = make_tex(DDGI_GL_RGBA32F, 1024, DDGI_ROWS, GL_RGBA, GL_FLOAT);
        if (!s_ddgi_nodes_tex[i] || !s_ddgi_tris_tex[i]) return 0;
    }
    s_ddgi_fresh_tex = make_tex(DDGI_GL_R8, 64, 64, GL_RED, GL_UNSIGNED_BYTE);
    s_ddgi_nodes = (DdgiNode *)malloc(sizeof(DdgiNode) * 2 * DDGI_MAX_TRIS);
    s_ddgi_frame_faces = (DdgiBuildFace *)malloc(sizeof(DdgiBuildFace) * DDGI_MAX_TRIS);
    s_ddgi_staging = (float *)malloc(sizeof(float) * 4 * 1024 * DDGI_ROWS);
    if (!s_ddgi_fresh_tex || !s_ddgi_nodes || !s_ddgi_frame_faces ||
        !s_ddgi_staging)
        return 0;
    if (!ddgi_target(&s_ddgi_rays_tex, &s_ddgi_rays_fbo, DDGI_GL_RGBA32F, DDGI_RAYS, DDGI_PROBES / 2, GL_RGBA, 0) ||
        !ddgi_target(&s_ddgi_irr_tex, &s_ddgi_irr_fbo, DDGI_GL_RGBA16F, DDGI_NX * DDGI_NZ * (DDGI_IRR + 2),
                     DDGI_NY * (DDGI_IRR + 2), GL_RGBA, 1) ||
        !ddgi_target(&s_ddgi_dist_tex, &s_ddgi_dist_fbo, DDGI_GL_RG32F, DDGI_NX * DDGI_NZ * (DDGI_DIST + 2),
                     DDGI_NY * (DDGI_DIST + 2), DDGI_GL_RG, 1) ||
        !ddgi_target(&s_ddgi_state_tex, &s_ddgi_state_fbo, DDGI_GL_RGBA16F, 64, 64, GL_RGBA, 0))
        return 0;
    s_ddgi_ready = 1;
    return 1;
}

static void ddgi_upload(GLuint tex, int texels) {
    const int rows = (texels + 1023) / 1024;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, rows, GL_RGBA, GL_FLOAT, s_ddgi_staging);
}

static float ddgi_pack_colour(const uint8_t c[3]) { return (float)(c[0] | (c[1] << 8) | (c[2] << 16)); }

static void ddgi_pack_triangle(float *texel, const GpuDdgiTriangle *t) {
    for (int k = 0; k < 3; ++k) {
        texel[k * 4 + 0] = t->world[k][0];
        texel[k * 4 + 1] = t->world[k][1];
        texel[k * 4 + 2] = t->world[k][2];
        texel[k * 4 + 3] = (float)(t->uv[k][0] | (t->uv[k][1] << 8));
    }
    texel[12] = (float)t->clut;
    texel[13] = (float)t->texpage;
    texel[14] = (float)t->flags;
    texel[16] = ddgi_pack_colour(t->colour[0]);
    texel[17] = ddgi_pack_colour(t->colour[1]);
    texel[18] = ddgi_pack_colour(t->colour[2]);
}

static void ddgi_pack_node(float *texel, const DdgiNode *n) {
    texel[0] = n->lo[0];
    texel[1] = n->lo[1];
    texel[2] = n->lo[2];
    texel[3] = (float)n->link;
    texel[4] = n->hi[0];
    texel[5] = n->hi[1];
    texel[6] = n->hi[2];
    texel[7] = (float)n->count;
}

static float s_ddgi_face[DDGI_MAX_TRIS][4]; /* the face's normal toward the eye, its length twice the area; w its unit length */
static int s_ddgi_corner_head[DDGI_CORNER_SLOTS], s_ddgi_corner_next[DDGI_MAX_TRIS * 3], s_ddgi_corner_slot[DDGI_MAX_TRIS * 3];
static uint64_t s_ddgi_corner_key[DDGI_CORNER_SLOTS];
static uint16_t s_ddgi_corner_normal[DDGI_MAX_TRIS][3];

static void ddgi_build(const GpuDdgiTriangle *tris, int count, int bvh, const float eye[3]) {
    if (count > DDGI_MAX_TRIS) count = DDGI_MAX_TRIS;
    for (int i = 0; i < count; ++i) psx_ddgi_face_of(&s_ddgi_frame_faces[i], &tris[i], i);
    const DdgiCorners corners = {s_ddgi_face, s_ddgi_corner_head, s_ddgi_corner_next, s_ddgi_corner_slot, s_ddgi_corner_key,
                                 s_ddgi_corner_normal};
    psx_ddgi_corner_normals(&corners, tris, count, eye);
    DdgiBvhBuild build = {s_ddgi_nodes, s_ddgi_frame_faces, 1};
    psx_ddgi_median_split(&build, 0, 0, count);
    s_ddgi_node_count = build.node_count;
    memset(s_ddgi_staging, 0, sizeof(float) * 4 * 1024 * DDGI_ROWS);
    for (int i = 0; i < count; ++i) {
        float *texel = s_ddgi_staging + (size_t)i * 5 * 4;
        const uint16_t *n = s_ddgi_corner_normal[s_ddgi_frame_faces[i].index];
        ddgi_pack_triangle(texel, &tris[s_ddgi_frame_faces[i].index]);
        texel[15] = (float)(n[0] | (n[1] & 255u) << 16);
        texel[19] = (float)(n[1] >> 8 | (uint32_t)n[2] << 8);
    }
    ddgi_upload(s_ddgi_tris_tex[bvh], count * 5);
    memset(s_ddgi_staging, 0, sizeof(float) * 4 * 1024 * DDGI_ROWS);
    for (int i = 0; i < s_ddgi_node_count; ++i) ddgi_pack_node(s_ddgi_staging + (size_t)i * 8, &s_ddgi_nodes[i]);
    ddgi_upload(s_ddgi_nodes_tex[bvh], s_ddgi_node_count * 2);
}


/* Every face of the level's rooms as the game hands them over: a ray to the sun meets walls the frame did not draw. */
#define DDGI_LEVEL_TRIS 98304
#define DDGI_LEVEL_ROWS 480

static GpuDdgiTriangle *s_ddgi_level, *s_ddgi_level_incoming;
static DdgiBuildFace *s_ddgi_level_faces;
static DdgiNode *s_ddgi_level_nodes;
static float *s_ddgi_level_tri_staging, *s_ddgi_level_node_staging;
static GLuint s_ddgi_level_nodes_tex[2], s_ddgi_level_tris_tex[2];
static int s_ddgi_level_next, s_ddgi_level_built = -1, s_ddgi_level_node_count, s_ddgi_level_failed, s_ddgi_level_set_up;
static int s_ddgi_level_textures; /* 1 made, -1 could not be made */
/* The BVH is made on its own thread (11 ms for mission 2), the GL thread only uploads it. The rest is under the mutex. */
static SDL_Thread *s_ddgi_level_thread;
static SDL_mutex *s_ddgi_level_mutex;
static SDL_cond *s_ddgi_level_cond;
static int s_ddgi_level_run, s_ddgi_level_incoming_count = -1, s_ddgi_level_generation, s_ddgi_level_staged, s_ddgi_level_uploading;
static int s_ddgi_level_staged_faces, s_ddgi_level_staged_nodes, s_ddgi_level_staged_generation;
static int s_ddgi_level_made_count = -1, s_ddgi_level_made_generation = -1; /* the level thread's own */

static void ddgi_level_clear(void) {
    if (s_ddgi_level_mutex) SDL_LockMutex(s_ddgi_level_mutex);
    s_ddgi_level_incoming_count = -1;
    s_ddgi_level_staged = 0;
    ++s_ddgi_level_generation;
    if (s_ddgi_level_mutex) {
        SDL_CondBroadcast(s_ddgi_level_cond);
        SDL_UnlockMutex(s_ddgi_level_mutex);
    }
    s_ddgi_level_built = -1;
}

static void ddgi_level_make(int count);

/* The faces handed over have the corners of the ones before, in their order: only materials changed. */
static int ddgi_level_same_shape(int count) {
    for (int i = 0; i < count; ++i)
        if (memcmp(s_ddgi_level[i].world, s_ddgi_level_incoming[i].world, sizeof s_ddgi_level[i].world)) return 0;
    return 1;
}

/* The BVH as it was, each face's texture packed anew: an animated texture's entry changes every frame. */
static void ddgi_level_repack(int count) {
    for (int i = 0; i < count; ++i)
        ddgi_pack_triangle(s_ddgi_level_tri_staging + (size_t)i * 5 * 4, &s_ddgi_level[s_ddgi_level_faces[i].index]);
}

/* Waits for faces, makes their BVH into the staging and leaves it for the GL thread, unless the level changed meanwhile. */
static int ddgi_level_main(void *opaque) {
    (void)opaque;
    SDL_LockMutex(s_ddgi_level_mutex);
    for (;;) {
        while (s_ddgi_level_run && (s_ddgi_level_incoming_count < 0 || s_ddgi_level_staged || s_ddgi_level_uploading))
            SDL_CondWait(s_ddgi_level_cond, s_ddgi_level_mutex);
        if (!s_ddgi_level_run) break;
        GpuDdgiTriangle *swap = s_ddgi_level;
        s_ddgi_level = s_ddgi_level_incoming;
        s_ddgi_level_incoming = swap;
        const int count = s_ddgi_level_incoming_count, generation = s_ddgi_level_generation;
        s_ddgi_level_incoming_count = -1;
        /* Under the lock: the next hand-over is copied into the faces before. */
        const int same = count > 0 && count == s_ddgi_level_made_count && generation == s_ddgi_level_made_generation &&
                         ddgi_level_same_shape(count);
        SDL_UnlockMutex(s_ddgi_level_mutex);
        const uint64_t began = SDL_GetPerformanceCounter();
        if (same) ddgi_level_repack(count);
        else ddgi_level_make(count);
        s_ddgi_level_made_count = count;
        s_ddgi_level_made_generation = generation;
        if (getenv("PSX_DDGI"))
            fprintf(stderr, "ddgi level: %d faces, %d nodes, %s in %.2f ms\n", count, s_ddgi_level_node_count,
                    same ? "materials repacked" : "made",
                    (double)(SDL_GetPerformanceCounter() - began) * 1000.0 / (double)SDL_GetPerformanceFrequency());
        SDL_LockMutex(s_ddgi_level_mutex);
        if (generation == s_ddgi_level_generation) {
            s_ddgi_level_staged = 1;
            s_ddgi_level_staged_faces = count;
            s_ddgi_level_staged_nodes = count ? s_ddgi_level_node_count : 0;
            s_ddgi_level_staged_generation = generation;
        }
    }
    SDL_UnlockMutex(s_ddgi_level_mutex);
    return 0;
}

static void ddgi_level_stop(void) {
    if (s_ddgi_level_thread) {
        SDL_LockMutex(s_ddgi_level_mutex);
        s_ddgi_level_run = 0;
        SDL_CondBroadcast(s_ddgi_level_cond);
        SDL_UnlockMutex(s_ddgi_level_mutex);
        SDL_WaitThread(s_ddgi_level_thread, NULL);
        s_ddgi_level_thread = NULL;
    }
}

/* Memory and the thread, made by the first hand-over. */
static int ddgi_level_start(void) {
    if (s_ddgi_level_set_up) return 1;
    if (s_ddgi_level_failed) return 0;
    s_ddgi_level_mutex = SDL_CreateMutex();
    s_ddgi_level_cond = SDL_CreateCond();
    s_ddgi_level = (GpuDdgiTriangle *)malloc(sizeof(GpuDdgiTriangle) * DDGI_LEVEL_TRIS);
    s_ddgi_level_incoming = (GpuDdgiTriangle *)malloc(sizeof(GpuDdgiTriangle) * DDGI_LEVEL_TRIS);
    s_ddgi_level_faces = (DdgiBuildFace *)malloc(sizeof(DdgiBuildFace) * DDGI_LEVEL_TRIS);
    s_ddgi_level_nodes = (DdgiNode *)malloc(sizeof(DdgiNode) * 2 * DDGI_LEVEL_TRIS);
    s_ddgi_level_tri_staging = (float *)malloc(sizeof(float) * 4 * 1024 * DDGI_LEVEL_ROWS);
    s_ddgi_level_node_staging = (float *)malloc(sizeof(float) * 4 * 1024 * DDGI_LEVEL_ROWS);
    if (s_ddgi_level_mutex && s_ddgi_level_cond && s_ddgi_level && s_ddgi_level_incoming && s_ddgi_level_faces &&
        s_ddgi_level_nodes && s_ddgi_level_tri_staging && s_ddgi_level_node_staging) {
        s_ddgi_level_run = 1;
        s_ddgi_level_thread = SDL_CreateThread(ddgi_level_main, "psx-gl-level", NULL);
    }
    if (!s_ddgi_level_thread) {
        fprintf(stdout, "psxrecomp: GL probe lighting has no room or thread for the level's faces, the sun sees only the frame\n");
        s_ddgi_level_run = 0;
        s_ddgi_level_failed = 1;
        return 0;
    }
    s_ddgi_level_set_up = 1;
    return 1;
}

/* Game thread: the faces are copied, the level thread makes their BVH. 0 when they could not be taken. */
int gpu_ddgi_set_level(const GpuDdgiTriangle *tris, int count) {
    if (count < 0 || (!tris && count)) return 0;
    if (count > DDGI_LEVEL_TRIS) {
        fprintf(stdout, "psxrecomp: GL probe lighting got a level of %d faces, more than %d, the sun sees only the frame\n", count,
                DDGI_LEVEL_TRIS);
        return 0;
    }
    if (!ddgi_level_start()) return 0;
    SDL_LockMutex(s_ddgi_level_mutex);
    if (count) memcpy(s_ddgi_level_incoming, tris, sizeof(GpuDdgiTriangle) * (size_t)count);
    s_ddgi_level_incoming_count = count;
    SDL_CondBroadcast(s_ddgi_level_cond);
    SDL_UnlockMutex(s_ddgi_level_mutex);
    return 1;
}

/* The level thread: the BVH of the first count faces of s_ddgi_level, packed into the two stagings. */
static void ddgi_level_make(int count) {
    const int kept = count;
    if (!kept) return;
    for (int i = 0; i < kept; ++i) psx_ddgi_face_of(&s_ddgi_level_faces[i], &s_ddgi_level[i], i);
    DdgiBvhBuild build = {s_ddgi_level_nodes, s_ddgi_level_faces, 1};
    psx_ddgi_sah_split(&build, 0, 0, kept, 0);
    s_ddgi_level_node_count = build.node_count;
    const int tri_rows = (kept * 5 + 1023) / 1024, node_rows = (s_ddgi_level_node_count * 2 + 1023) / 1024;
    memset(s_ddgi_level_tri_staging, 0, sizeof(float) * 4 * 1024 * (size_t)tri_rows);
    for (int i = 0; i < kept; ++i)
        ddgi_pack_triangle(s_ddgi_level_tri_staging + (size_t)i * 5 * 4, &s_ddgi_level[s_ddgi_level_faces[i].index]);
    memset(s_ddgi_level_node_staging, 0, sizeof(float) * 4 * 1024 * (size_t)node_rows);
    for (int i = 0; i < s_ddgi_level_node_count; ++i)
        ddgi_pack_node(s_ddgi_level_node_staging + (size_t)i * 8, &s_ddgi_level_nodes[i]);
}

/* Uploads a BVH the level thread made. The BVH being read by the other context is never the one written. */
static void ddgi_level_update(void) {
    if (!s_ddgi_level_set_up || s_ddgi_level_textures < 0) return;
    if (!s_ddgi_level_textures) {
        for (int i = 0; i < 2; ++i) {
            s_ddgi_level_nodes_tex[i] = make_tex(DDGI_GL_RGBA32F, 1024, DDGI_LEVEL_ROWS, GL_RGBA, GL_FLOAT);
            s_ddgi_level_tris_tex[i] = make_tex(DDGI_GL_RGBA32F, 1024, DDGI_LEVEL_ROWS, GL_RGBA, GL_FLOAT);
        }
        s_ddgi_level_textures = s_ddgi_level_nodes_tex[0] && s_ddgi_level_nodes_tex[1] && s_ddgi_level_tris_tex[0] &&
                                s_ddgi_level_tris_tex[1] ? 1 : -1;
        if (s_ddgi_level_textures < 0) {
            fprintf(stdout, "psxrecomp: GL probe lighting has no textures for the level's faces, the sun sees only the frame\n");
            return;
        }
    }
    SDL_LockMutex(s_ddgi_level_mutex);
    const int staged = s_ddgi_level_staged, faces = s_ddgi_level_staged_faces, nodes = s_ddgi_level_staged_nodes,
              generation = s_ddgi_level_staged_generation;
    s_ddgi_level_uploading = staged;
    s_ddgi_level_staged = 0;
    SDL_UnlockMutex(s_ddgi_level_mutex);
    if (!staged) return;
    const int target = s_ddgi_level_next;
    if (faces) {
        glBindTexture(GL_TEXTURE_2D, s_ddgi_level_tris_tex[target]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, (faces * 5 + 1023) / 1024, GL_RGBA, GL_FLOAT, s_ddgi_level_tri_staging);
        glBindTexture(GL_TEXTURE_2D, s_ddgi_level_nodes_tex[target]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, (nodes * 2 + 1023) / 1024, GL_RGBA, GL_FLOAT, s_ddgi_level_node_staging);
    }
    SDL_LockMutex(s_ddgi_level_mutex);
    /* A level change while uploading leaves this BVH unused, a level of no faces leaves none. */
    if (generation == s_ddgi_level_generation) {
        s_ddgi_level_built = faces ? target : -1;
        if (faces) s_ddgi_level_next ^= 1;
    }
    s_ddgi_level_uploading = 0;
    SDL_CondBroadcast(s_ddgi_level_cond);
    SDL_UnlockMutex(s_ddgi_level_mutex);
}

static int ddgi_posmod(int a, int n) { return ((a % n) + n) % n; }

static void ddgi_turn(float rot[3][3], uint32_t seed) {
    float r[3];
    for (int i = 0; i < 3; ++i) {
        seed = seed * 1664525u + 1013904223u;
        r[i] = (float)(seed >> 8) / 16777216.0f;
    }
    const float a = sqrtf(1.0f - r[0]), b = sqrtf(r[0]), t1 = 6.2831853f * r[1], t2 = 6.2831853f * r[2];
    const float x = a * sinf(t1), y = a * cosf(t1), z = b * sinf(t2), w = b * cosf(t2);
    rot[0][0] = 1 - 2 * (y * y + z * z); rot[0][1] = 2 * (x * y + w * z);     rot[0][2] = 2 * (x * z - w * y);
    rot[1][0] = 2 * (x * y - w * z);     rot[1][1] = 1 - 2 * (x * x + z * z); rot[1][2] = 2 * (y * z + w * x);
    rot[2][0] = 2 * (x * z + w * y);     rot[2][1] = 2 * (y * z - w * x);     rot[2][2] = 1 - 2 * (x * x + y * y);
}

static void ddgi_draw(GLuint fbo, int w, int h) {
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void gl_renderer_set_ddgi(int enabled);
void gl_renderer_set_ddgi_strength(float strength);
void gl_renderer_set_ddgi_bounce(float bounce);
void gl_renderer_set_ddgi_glow(float glow);
void gl_renderer_set_ddgi_shadows(int enabled);
void gl_renderer_set_ddgi_lamps(int enabled);
void gl_renderer_set_ddgi_shadow_strength(float strength);
void gl_renderer_set_ddgi_lamp_strength(float strength);

/* frame: the picture just drawn, w and h its size and cx, cy its shown centre, in native pixels. */
static int s_ddgi_environment = 1;
void gl_renderer_set_ddgi_environment(int taken) { s_ddgi_environment = taken != 0; }

static void ddgi_update(GLuint frame, float w, float h, float cx, float cy) {
    static int asked;
    if (!asked && s_ddgi_environment) {
        asked = 1;
        const char *light = getenv("PSX_DDGI_LIGHT"), *strength = getenv("PSX_DDGI_STRENGTH"), *bounce = getenv("PSX_DDGI_BOUNCE");
        if (getenv("PSX_DDGI_GLOW")) gl_renderer_set_ddgi_glow((float)atof(getenv("PSX_DDGI_GLOW")));
        if (strength) gl_renderer_set_ddgi_strength((float)atof(strength));
        if (bounce) gl_renderer_set_ddgi_bounce((float)atof(bounce));
        if (getenv("PSX_DDGI_VIEW")) s_ddgi_light_view = atoi(getenv("PSX_DDGI_VIEW")) != 0;
        if (light && light[0] == '1') gl_renderer_set_ddgi(1);
        if (getenv("PSX_DDGI_SHADOW_STRENGTH")) gl_renderer_set_ddgi_shadow_strength((float)atof(getenv("PSX_DDGI_SHADOW_STRENGTH")));
        if (getenv("PSX_DDGI_LAMP_STRENGTH")) gl_renderer_set_ddgi_lamp_strength((float)atof(getenv("PSX_DDGI_LAMP_STRENGTH")));
        if (getenv("PSX_DDGI_LAMP_VIEW")) s_ddgi_lamp_view = atoi(getenv("PSX_DDGI_LAMP_VIEW")) != 0;
        if (getenv("PSX_DDGI_BLOBS")) gpu_ddgi_set_hide_blobs(atoi(getenv("PSX_DDGI_BLOBS")) == 0);
        if (getenv("PSX_DDGI_SHADOWS") && getenv("PSX_DDGI_SHADOWS")[0] == '1') gl_renderer_set_ddgi_shadows(1);
        if (getenv("PSX_DDGI_LAMPS") && getenv("PSX_DDGI_LAMPS")[0] == '1') gl_renderer_set_ddgi_lamps(1);
        if (getenv("PSX_DDGI_SUN") && getenv("PSX_DDGI_SUN")[0] == '1') gl_renderer_set_ddgi_sun(1);
    }
    const int probes = s_ddgi_light, sun = ddgi_sun_active(), lamps = s_ddgi_shadows || s_ddgi_lamps || sun;
    if ((!probes && !lamps) || s_ddgi_failed || !s_raster_ok) return;
    const GpuDdgiTriangle *tris;
    int count;
    GteCameraPose cam;
    if (!gpu_ddgi_shown((int)cy, &tris, &count, &cam) || cam.sequence == s_ddgi_last_pose) return;
    s_ddgi_last_pose = cam.sequence;

    GLint draw_fbo, read_fbo, viewport[4], program, vao, active, bound[8], blend_src[2], blend_dst[2], blend_eq[2];
    GLfloat blend_colour[4], clear_colour[4];
    GLboolean masks[4];
    glGetIntegerv(DDGI_GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
    glGetIntegerv(DDGI_GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(DDGI_GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(DDGI_GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(DDGI_GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(DDGI_GL_BLEND_SRC_RGB, &blend_src[0]);
    glGetIntegerv(DDGI_GL_BLEND_SRC_ALPHA, &blend_src[1]);
    glGetIntegerv(DDGI_GL_BLEND_DST_RGB, &blend_dst[0]);
    glGetIntegerv(DDGI_GL_BLEND_DST_ALPHA, &blend_dst[1]);
    glGetIntegerv(DDGI_GL_BLEND_EQUATION_RGB, &blend_eq[0]);
    glGetIntegerv(DDGI_GL_BLEND_EQUATION_ALPHA, &blend_eq[1]);
    glGetFloatv(DDGI_GL_BLEND_COLOR, blend_colour);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_colour);
    glGetBooleanv(GL_COLOR_WRITEMASK, masks);
    for (int i = 0; i < 8; ++i) {
        p_glActiveTexture(PSXGL_TEXTURE0 + (GLenum)i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound[i]);
    }
    const GLboolean blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST), depth = glIsEnabled(GL_DEPTH_TEST),
                    stencil = glIsEnabled(GL_STENCIL_TEST);

    if (!s_ddgi_ready && !ddgi_init()) {
        s_ddgi_failed = 1;
        s_ddgi_light = s_ddgi_shadows = s_ddgi_lamps = 0;
        gpu_ddgi_set_shadowing(0);
        ssao_mode();
        fprintf(stdout, "psxrecomp: GL probe lighting could not start, switched off\n");
    } else {
        const int timing = getenv("PSX_DDGI") != NULL;
        if (timing) {
            if (!s_ddgi_query) p_glGenQueries(1, &s_ddgi_query);
            if (s_ddgi_query_open && s_ddgi_updates % 60u == 31u) {
                GLuint64 spent = 0;
                p_glGetQueryObjectui64v(s_ddgi_query, 0x8866 /* GL_QUERY_RESULT */, &spent);
                fprintf(stderr, "ddgi light: GPU %.3f ms an update\n", (double)spent / 1.0e6);
            }
            p_glBeginQuery(GL_TIME_ELAPSED, s_ddgi_query);
        }
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        /* The presenter may still have a draw queued that reads the BVH or level slot about to be written. */
        if (s_interp_mutex) SDL_LockMutex(s_interp_mutex);
        if (s_interp_draw_fence) p_glWaitSync(s_interp_draw_fence, 0, PSXGL_TIMEOUT_IGNORED);
        if (s_interp_mutex) SDL_UnlockMutex(s_interp_mutex);
        const int bvh = s_ddgi_bvh_next;
        s_ddgi_bvh_next ^= 1;

        DdgiFrame *now = &s_ddgi_frame_now;
        now->valid = 1;
        now->bvh = bvh;
        now->pose = cam.sequence;
        for (int r = 0; r < 3; ++r) {
            for (int k = 0; k < 3; ++k) now->rows[r][k] = cam.rotation[r * 3 + k] / 4096.0f;
            now->rows[r][3] = (float)cam.translation[r];
        }
        /* The picture was drawn turned about the eye by the smooth turn's part of a step: the rows that read it are too. */
        psx_ddgi_turn_rows(now->rows, gpu_ddgi_turn(cam.sequence));
        for (int a = 0; a < 3; ++a) now->offset[a] = (float)cam.offset[a];
        psx_ddgi_camera_back(now->rows, now->offset, now->back, now->eye);
        ddgi_build(tris, count, bvh, now->eye);
        if (!lamps || !gpu_ddgi_scene(cam.sequence, &now->scene)) memset(&now->scene, 0, sizeof now->scene);
        if (sun) ddgi_level_update();
        now->level = sun ? s_ddgi_level_built : -1;
        memset(now->sun, 0, sizeof now->sun);
        memset(now->sun_colour, 0, sizeof now->sun_colour);
        if (sun) {
            const DdgiSun *s = &s_ddgi_suns[s_ddgi_mission];
            const float az = s->azimuth * 0.017453293f, el = s->elevation * 0.017453293f;
            /* World y grows downward. */
            now->sun[0] = cosf(el) * sinf(az);
            now->sun[1] = -sinf(el);
            now->sun[2] = cosf(el) * cosf(az);
            now->sun[3] = s->strength;
            for (int k = 0; k < 3; ++k) now->sun_colour[k] = powf(s->colour[k], 2.2f);
            now->sun_colour[3] = s->shade;
        }
        s_ddgi_ctx[0].frame = *now;
        const float *camera = now->eye;
        if (probes) {
        const int dims[3] = {DDGI_NX, DDGI_NY, DDGI_NZ};
        for (int a = 0; a < 3; ++a) s_ddgi_origin[a] = (int)floorf(camera[a] / DDGI_SPACING) - dims[a] / 2;
        memset(s_ddgi_fresh, 0, sizeof s_ddgi_fresh);
        for (int sy = 0; sy < DDGI_NY; ++sy)
            for (int sz = 0; sz < DDGI_NZ; ++sz)
                for (int sx = 0; sx < DDGI_NX; ++sx) {
                    const int probe = sx + DDGI_NX * (sz + DDGI_NZ * sy), slot[3] = {sx, sy, sz};
                    int changed = !s_ddgi_slots_known;
                    for (int a = 0; a < 3; ++a) {
                        const int cell = s_ddgi_origin[a] + ddgi_posmod(slot[a] - s_ddgi_origin[a], dims[a]);
                        if (cell != s_ddgi_slot_cell[probe][a]) changed = 1;
                        s_ddgi_slot_cell[probe][a] = cell;
                    }
                    if (changed) s_ddgi_pending[probe] = 1;
                    s_ddgi_fresh[probe] = s_ddgi_pending[probe] ? 255 : 0;
                    if ((probe & 1) == (int)((s_ddgi_updates + 1u) & 1u)) s_ddgi_pending[probe] = 0;
                }
        s_ddgi_slots_known = 1;
        glBindTexture(GL_TEXTURE_2D, s_ddgi_fresh_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 64, GL_RED, GL_UNSIGNED_BYTE, s_ddgi_fresh);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

        float rot[3][3];
        ddgi_turn(rot, ++s_ddgi_updates * 2654435761u);
        const int half = (int)(s_ddgi_updates & 1u);
        p_glBindVertexArray(s_present_vao);

        /* The presenter samples the probes: these writes wait for its queued draws, and its next draws wait for s_ddgi_probe_fence. */
        if (s_interp_mutex) SDL_LockMutex(s_interp_mutex);
        if (s_interp_draw_fence) p_glWaitSync(s_interp_draw_fence, 0, PSXGL_TIMEOUT_IGNORED);

        /* Rays. */
        glDisable(GL_BLEND);
        p_glUseProgram(s_ddgi_trace_prog);
        p_glActiveTexture(PSXGL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_ddgi_nodes_tex[bvh]);
        p_glActiveTexture(PSXGL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, s_ddgi_tris_tex[bvh]);
        p_glActiveTexture(PSXGL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, s_raw_tex);
        p_glActiveTexture(PSXGL_TEXTURE0 + 3);
        glBindTexture(GL_TEXTURE_2D, s_ddgi_irr_tex);
        p_glActiveTexture(PSXGL_TEXTURE0 + 4);
        glBindTexture(GL_TEXTURE_2D, frame);
        p_glUniform1i(s_ddgi_tu.frame, 4);
        p_glActiveTexture(PSXGL_TEXTURE0 + 5);
        glBindTexture(GL_TEXTURE_2D, s_ddgi_state_tex);
        p_glUniform1i(s_ddgi_tu.state, 5);
        p_glUniform4f(s_ddgi_tu.frame_at, w, h, cx, cy);
        p_glUniform1i(s_ddgi_tu.nodes, 0);
        p_glUniform1i(s_ddgi_tu.tris, 1);
        p_glUniform1i(s_ddgi_tu.vram, 2);
        p_glUniform1i(s_ddgi_tu.irr, 3);
        p_glUniform4f(s_ddgi_tu.origin, (float)s_ddgi_origin[0], (float)s_ddgi_origin[1], (float)s_ddgi_origin[2], DDGI_SPACING);
        p_glUniform4f(s_ddgi_tu.rot0, rot[0][0], rot[1][0], rot[2][0], 0.0f);
        p_glUniform4f(s_ddgi_tu.rot1, rot[0][1], rot[1][1], rot[2][1], 0.0f);
        p_glUniform4f(s_ddgi_tu.rot2, rot[0][2], rot[1][2], rot[2][2], 0.0f);
        p_glUniform4f(s_ddgi_tu.camera, camera[0], camera[1], camera[2], s_ddgi_updates > 8 ? s_ddgi_bounce : 0.0f);
        p_glUniform4f(s_ddgi_tu.sky, s_ddgi_glow, 0.0f, 0.0f, DDGI_FARTHEST);
        p_glActiveTexture(PSXGL_TEXTURE0 + 6);
        glBindTexture(GL_TEXTURE_2D, now->level >= 0 ? s_ddgi_level_nodes_tex[now->level] : 0);
        p_glActiveTexture(PSXGL_TEXTURE0 + 7);
        glBindTexture(GL_TEXTURE_2D, now->level >= 0 ? s_ddgi_level_tris_tex[now->level] : 0);
        p_glActiveTexture(PSXGL_TEXTURE0);
        p_glUniform1i(s_ddgi_tu.level_nodes, 6);
        p_glUniform1i(s_ddgi_tu.level_tris, 7);
        p_glUniform4f(s_ddgi_tu.level, now->level >= 0 ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        p_glUniform4f(s_ddgi_tu.sun, now->sun[0], now->sun[1], now->sun[2], now->sun[3]);
        p_glUniform4f(s_ddgi_tu.sun_colour, now->sun_colour[0], now->sun_colour[1], now->sun_colour[2], now->sun_colour[3]);
        p_glUniform4f(s_ddgi_tu.row0, now->rows[0][0], now->rows[0][1], now->rows[0][2], now->rows[0][3]);
        p_glUniform4f(s_ddgi_tu.row1, now->rows[1][0], now->rows[1][1], now->rows[1][2], now->rows[1][3]);
        p_glUniform4f(s_ddgi_tu.row2, now->rows[2][0], now->rows[2][1], now->rows[2][2], now->rows[2][3]);
        p_glUniform4f(s_ddgi_tu.offset, now->offset[0], now->offset[1], now->offset[2], 0.0f);
        p_glUniform4f(s_ddgi_tu.view, (float)cam.focal, (g_wide_w > 0 ? (float)g_wide_w : 320.0f) * 0.5f + 16.0f, 136.0f,
                      (float)half);
        ddgi_draw(s_ddgi_rays_fbo, DDGI_RAYS, DDGI_PROBES / 2);
        p_glUseProgram(s_ddgi_classify_prog);
        p_glActiveTexture(PSXGL_TEXTURE0 + 5);
        glBindTexture(GL_TEXTURE_2D, 0);
        p_glActiveTexture(PSXGL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_ddgi_rays_tex);
        p_glUniform1i(s_ddgi_classify_rays, 0);
        p_glUniform1f(s_ddgi_classify_half, (float)half);
        glEnable(GL_BLEND);
        p_glBlendEquationSeparate(DDGI_GL_FUNC_ADD, DDGI_GL_FUNC_ADD);
        p_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        ddgi_draw(s_ddgi_state_fbo, 64, 64);

        /* Irradiance and distances, blended by each fragment's alpha. */
        glEnable(GL_BLEND);
        p_glBlendEquationSeparate(DDGI_GL_FUNC_ADD, DDGI_GL_FUNC_ADD);
        p_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        p_glUseProgram(s_ddgi_update_prog);
        p_glActiveTexture(PSXGL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_ddgi_rays_tex);
        p_glActiveTexture(PSXGL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, s_ddgi_fresh_tex);
        p_glUniform1i(s_ddgi_uu.rays, 0);
        p_glUniform1i(s_ddgi_uu.fresh, 1);
        p_glUniform1f(s_ddgi_uu.half, (float)half);
        p_glUniform4f(s_ddgi_uu.rot0, rot[0][0], rot[1][0], rot[2][0], 0.0f);
        p_glUniform4f(s_ddgi_uu.rot1, rot[0][1], rot[1][1], rot[2][1], 0.0f);
        p_glUniform4f(s_ddgi_uu.rot2, rot[0][2], rot[1][2], rot[2][2], 0.0f);
        p_glUniform4f(s_ddgi_uu.mode, (float)DDGI_IRR, 0.0f, 0.96f, DDGI_FARTHEST);
        ddgi_draw(s_ddgi_irr_fbo, DDGI_NX * DDGI_NZ * (DDGI_IRR + 2), DDGI_NY * (DDGI_IRR + 2));
        p_glUniform4f(s_ddgi_uu.mode, (float)DDGI_DIST, 1.0f, 0.96f, DDGI_FARTHEST);
        ddgi_draw(s_ddgi_dist_fbo, DDGI_NX * DDGI_NZ * (DDGI_DIST + 2), DDGI_NY * (DDGI_DIST + 2));
        if (s_ddgi_probe_fence) p_glDeleteSync(s_ddgi_probe_fence);
        s_ddgi_probe_fence = p_glFenceSync(PSXGL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
        if (s_interp_mutex) SDL_UnlockMutex(s_interp_mutex);

        s_ddgi_applies = s_ddgi_updates > 2;
        }
        if (timing) {
            p_glEndQuery(GL_TIME_ELAPSED);
            s_ddgi_query_open = 1;
        }
        if (s_ddgi_updates % 60u == 30u && getenv("PSX_DDGI")) {
            float average[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            if (s_ddgi_ctx[0].screen.average_tex) {
                glBindTexture(GL_TEXTURE_2D, s_ddgi_ctx[0].screen.average_tex);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, average);
            }
            fprintf(stderr, "ddgi light: update %u triangles %d average %.3f %.3f %.3f weight %.3f fade %.0f..%.0f camera %.0f %.0f %.0f\n",
                    s_ddgi_updates, count, average[0], average[1], average[2], average[3], s_ssao_fade_from, s_ssao_fade_to, camera[0],
                    camera[1], camera[2]);
        }
        glFlush();
    }

    p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER, (GLuint)draw_fbo);
    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER, (GLuint)read_fbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    p_glUseProgram((GLuint)program);
    p_glBindVertexArray((GLuint)vao);
    for (int i = 0; i < 8; ++i) {
        p_glActiveTexture(PSXGL_TEXTURE0 + (GLenum)i);
        glBindTexture(GL_TEXTURE_2D, (GLuint)bound[i]);
    }
    p_glActiveTexture((GLenum)active);
    p_glBlendFuncSeparate((GLenum)blend_src[0], (GLenum)blend_dst[0], (GLenum)blend_src[1], (GLenum)blend_dst[1]);
    p_glBlendEquationSeparate((GLenum)blend_eq[0], (GLenum)blend_eq[1]);
    p_glBlendColor(blend_colour[0], blend_colour[1], blend_colour[2], blend_colour[3]);
    glClearColor(clear_colour[0], clear_colour[1], clear_colour[2], clear_colour[3]);
    glColorMask(masks[0], masks[1], masks[2], masks[3]);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
}

static void ddgi_apply_set(const DdgiApplyUniforms *u, const DdgiFrame *f, float focal_x, float focal_y, float centre_x,
                           float centre_y, int w, int h, int scale) {
    p_glUniform1i(u->tex, 0);
    p_glUniform1i(u->depth, 2);
    p_glUniform1i(u->irr, 3);
    p_glUniform1i(u->dist, 4);
    p_glUniform1i(u->state, 6);
    p_glUniform4f(u->ddgi, s_ddgi_strength, s_ddgi_light_view ? 1.0f : 0.0f, DDGI_SPACING, DDGI_FARTHEST);
    p_glUniform4f(u->proj, focal_x, focal_y, centre_x, centre_y);
    p_glUniform4f(u->texel, 1.0f / (float)w, 1.0f / (float)h, 0.0f, (float)scale);
    p_glUniform4f(u->back0, f->back[0][0], f->back[0][1], f->back[0][2], f->rows[0][3]);
    p_glUniform4f(u->back1, f->back[1][0], f->back[1][1], f->back[1][2], f->rows[1][3]);
    p_glUniform4f(u->back2, f->back[2][0], f->back[2][1], f->back[2][2], f->rows[2][3]);
    p_glUniform4f(u->offset, f->offset[0], f->offset[1], f->offset[2], 0.0f);
    p_glUniform4f(u->origin, (float)s_ddgi_origin[0], (float)s_ddgi_origin[1], (float)s_ddgi_origin[2], 0.0f);
    p_glUniform4f(u->fade, s_ssao_fade_from, s_ssao_fade_to, 0.0f, 0.0f);
}

static void ddgi_bind_probes(GLuint color, GLuint depth) {
    p_glActiveTexture(PSXGL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, depth);
    p_glActiveTexture(PSXGL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, s_ddgi_irr_tex);
    p_glActiveTexture(PSXGL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, s_ddgi_dist_tex);
    p_glActiveTexture(PSXGL_TEXTURE0 + 6);
    glBindTexture(GL_TEXTURE_2D, s_ddgi_state_tex);
    p_glActiveTexture(PSXGL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color);
}

/* This context's programs, made the first time it asks. NULL when they could not be made. */
static DdgiContext *ddgi_context(int thread) {
    DdgiContext *ctx = &s_ddgi_ctx[thread ? 1 : 0];
    if (ctx->failed) return NULL;
    if (ctx->built) return ctx;
    ctx->sample_prog = build_program(DDGI_VS, DDGI_SAMPLE_FS);
    ctx->average_prog = build_program(DDGI_VS, DDGI_AVERAGE_FS);
    ctx->mix_prog = build_program(PRESENT_VS, DDGI_MIX_FS);
    ctx->lamp_prog = build_program(DDGI_VS, DDGI_LAMP_FS);
    if (!ctx->sample_prog || !ctx->average_prog || !ctx->mix_prog || !ctx->lamp_prog) {
        fprintf(stdout, "psxrecomp: GL probe lighting programs could not be made in a context\n");
        ctx->failed = 1;
        return NULL;
    }
    ctx->su = ddgi_apply_locations(ctx->sample_prog);
    ctx->mu = ddgi_apply_locations(ctx->mix_prog);
    ctx->lu = ddgi_apply_locations(ctx->lamp_prog);
    ctx->average_samples = ddgi_uniform(ctx->average_prog, "u_samples");
    ctx->mix_lamps = ddgi_uniform(ctx->mix_prog, "u_lamps");
    ctx->mix_lit = ddgi_uniform(ctx->mix_prog, "u_lit");
    ctx->mix_fog = ddgi_uniform(ctx->mix_prog, "u_fog");
    const GLuint l = ctx->lamp_prog;
    ctx->la = (DdgiLampUniforms){ddgi_uniform(l, "u_nodes"), ddgi_uniform(l, "u_tris"),   ddgi_uniform(l, "u_vram"),
                                 ddgi_uniform(l, "u_rect"),  ddgi_uniform(l, "u_size"),   ddgi_uniform(l, "u_eye"),
                                 ddgi_uniform(l, "u_count"), ddgi_uniform(l, "u_shadow"), ddgi_uniform(l, "u_caster"),
                                 ddgi_uniform(l, "u_lamp"),  ddgi_uniform(l, "u_level_nodes"), ddgi_uniform(l, "u_level_tris"),
                                 ddgi_uniform(l, "u_sun"),   ddgi_uniform(l, "u_sun_colour"), ddgi_uniform(l, "u_level"),
                                 ddgi_uniform(l, "u_fog"),   ddgi_uniform(l, "u_cast"), ddgi_uniform(l, "u_mask")};
    ctx->built = 1;
    return ctx;
}

/* Measures the probes' light over the picture about to be mixed, in the context that mixes it: its average is what the mix
 * divides by, so the picture keeps its brightness on the whole. Runs before the mix's target is bound. */
static void ddgi_screen_measure(int thread, GLuint color, GLuint depth, float focal_x, float focal_y, float centre_x,
                                float centre_y, int w, int h, int scale, float u0, float v0, float u1, float v1) {
    if (!s_ddgi_light || !s_ddgi_ready || !s_ddgi_applies) return;
    DdgiContext *ctx = ddgi_context(thread);
    if (!ctx || !ctx->frame.valid) return;
    DdgiScreen *screen = &ctx->screen;
    GLfloat clear_colour[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_colour);
    if (!screen->samples_fbo) {
        const int made =
            ddgi_target(&screen->samples_tex, &screen->samples_fbo, DDGI_GL_RGBA16F, DDGI_SAMPLES_X, DDGI_SAMPLES_Y, GL_RGBA, 0) &&
            ddgi_target(&screen->average_tex, &screen->average_fbo, DDGI_GL_RGBA16F, 1, 1, GL_RGBA, 0);
        glClearColor(clear_colour[0], clear_colour[1], clear_colour[2], clear_colour[3]);
        if (!made) {
            fprintf(stdout, "psxrecomp: GL probe lighting has no measuring targets, switched off\n");
            s_ddgi_failed = 1;
            gl_renderer_set_ddgi(0);
            return;
        }
    }
    if (!screen->frames) {
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER, screen->average_fbo);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(clear_colour[0], clear_colour[1], clear_colour[2], clear_colour[3]);
    }
    GLint blend_src[2], blend_dst[2], blend_eq[2];
    GLfloat blend_colour[4];
    glGetIntegerv(DDGI_GL_BLEND_EQUATION_RGB, &blend_eq[0]);
    glGetIntegerv(DDGI_GL_BLEND_EQUATION_ALPHA, &blend_eq[1]);
    glGetIntegerv(DDGI_GL_BLEND_SRC_RGB, &blend_src[0]);
    glGetIntegerv(DDGI_GL_BLEND_SRC_ALPHA, &blend_src[1]);
    glGetIntegerv(DDGI_GL_BLEND_DST_RGB, &blend_dst[0]);
    glGetIntegerv(DDGI_GL_BLEND_DST_ALPHA, &blend_dst[1]);
    glGetFloatv(DDGI_GL_BLEND_COLOR, blend_colour);
    glDisable(GL_BLEND);
    ddgi_bind_probes(color, depth);
    p_glUseProgram(ctx->sample_prog);
    ddgi_apply_set(&ctx->su, &ctx->frame, focal_x, focal_y, centre_x, centre_y, w, h, scale);
    p_glUniform4f(ctx->su.rect, u0, v0, u1, v1);
    ddgi_draw(screen->samples_fbo, DDGI_SAMPLES_X, DDGI_SAMPLES_Y);
    glEnable(GL_BLEND);
    p_glBlendEquationSeparate(DDGI_GL_FUNC_ADD, DDGI_GL_FUNC_ADD);
    p_glBlendFuncSeparate(DDGI_GL_CONSTANT_ALPHA, DDGI_GL_ONE_MINUS_CONSTANT_ALPHA, DDGI_GL_CONSTANT_ALPHA,
                          DDGI_GL_ONE_MINUS_CONSTANT_ALPHA);
    p_glBlendColor(0.0f, 0.0f, 0.0f, 0.08f);
    p_glUseProgram(ctx->average_prog);
    glBindTexture(GL_TEXTURE_2D, screen->samples_tex);
    p_glUniform1i(ctx->average_samples, 0);
    ddgi_draw(screen->average_fbo, 1, 1);
    glDisable(GL_BLEND);
    p_glBlendEquationSeparate((GLenum)blend_eq[0], (GLenum)blend_eq[1]);
    p_glBlendFuncSeparate((GLenum)blend_src[0], (GLenum)blend_dst[0], (GLenum)blend_src[1], (GLenum)blend_dst[1]);
    p_glBlendColor(blend_colour[0], blend_colour[1], blend_colour[2], blend_colour[3]);
    ++screen->frames;
}

/* The casters' shadows and the dynamic lights' light at half the picture's size, before the mix's target is bound. */
static void ddgi_lamp_pass(int thread, GLuint color, GLuint depth, float focal_x, float focal_y, float centre_x, float centre_y,
                           int w, int h, int scale, float u0, float v0, float u1, float v1) {
    DdgiContext *ctx = s_ddgi_ready ? ddgi_context(thread) : NULL;
    if (ctx) ctx->lamp_ready = 0;
    if (!ctx || !ctx->frame.valid || !(s_ddgi_shadows || s_ddgi_lamps || ddgi_sun_active())) return;
    const DdgiFrame *f = &ctx->frame;
    const int lw = (int)((u1 - u0) * (float)w * 0.5f + 0.5f), lh = (int)((v1 - v0) * (float)h * 0.5f + 0.5f);
    if (lw <= 0 || lh <= 0) return;
    if (!ctx->lamp_fbo || ctx->lamp_w != lw || ctx->lamp_h != lh) {
        if (ctx->lamp_fbo) p_glDeleteFramebuffers(1, &ctx->lamp_fbo);
        if (ctx->lamp_tex) glDeleteTextures(1, &ctx->lamp_tex);
        ctx->lamp_fbo = ctx->lamp_tex = 0;
        GLfloat clear_colour[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_colour);
        const int made = ddgi_target(&ctx->lamp_tex, &ctx->lamp_fbo, DDGI_GL_RGBA16F, lw, lh, GL_RGBA, 0);
        glClearColor(clear_colour[0], clear_colour[1], clear_colour[2], clear_colour[3]);
        if (!made) return;
        ctx->lamp_w = lw;
        ctx->lamp_h = lh;
    }
    /* Both presenting threads come here: a shared buffer raced. */
    float casters[GPU_DDGI_CASTERS * 2 * 4], pictures[GPU_DDGI_CASTERS * 2 * 4], lamps[GPU_DDGI_LAMPS * 3 * 4];
    uint32_t masks[GPU_DDGI_CASTERS * GPU_DDGI_MASK_ROWS];
    const GpuDdgiScene *scene = &f->scene;
    const int shadows = s_ddgi_shadows, lit = s_ddgi_lamps;
    int caster_count = scene->casters, lamp_count = 0;
    for (int i = 0; i < caster_count; ++i) {
        const GpuDdgiCaster *c = &scene->caster[i];
        float *at = &casters[i * 8];
        at[0] = c->centre[0];
        at[1] = c->centre[1];
        at[2] = c->centre[2];
        at[3] = c->across;
        at[4] = c->tall;
        at[5] = at[6] = at[7] = 0.0f;
        float *picture = &pictures[i * 8];
        picture[0] = c->half_width;
        picture[1] = c->half_height;
        picture[2] = c->side[0];
        picture[3] = c->side[1];
        memcpy(picture + 4, c->picture, sizeof c->picture);
        picture[7] = 0.0f;
        memcpy(&masks[i * GPU_DDGI_MASK_ROWS], c->mask, sizeof c->mask);
    }
    for (int i = 0; i < scene->lamps; ++i) {
        const GpuDdgiLamp *l = &scene->lamp[i];
        if (l->dynamic ? !lit : !shadows) continue;
        float *at = &lamps[lamp_count++ * 12];
        memcpy(at, l->position, sizeof l->position);
        at[3] = l->size;
        memcpy(at + 4, l->colour, sizeof l->colour);
        at[7] = l->power;
        memcpy(at + 8, l->normal, sizeof l->normal);
        at[11] = l->dynamic ? -l->weight : l->weight;
    }
    glDisable(GL_BLEND);
    ddgi_bind_probes(color, depth);
    p_glActiveTexture(PSXGL_TEXTURE0 + 7);
    glBindTexture(GL_TEXTURE_2D, s_ddgi_nodes_tex[f->bvh]);
    p_glActiveTexture(PSXGL_TEXTURE0 + 8);
    glBindTexture(GL_TEXTURE_2D, s_ddgi_tris_tex[f->bvh]);
    p_glActiveTexture(PSXGL_TEXTURE0 + 9);
    glBindTexture(GL_TEXTURE_2D, s_raw_tex);
    p_glActiveTexture(PSXGL_TEXTURE0);
    p_glUseProgram(ctx->lamp_prog);
    ddgi_apply_set(&ctx->lu, f, focal_x, focal_y, centre_x, centre_y, w, h, scale);
    p_glUniform1i(ctx->la.nodes, 7);
    p_glUniform1i(ctx->la.tris, 8);
    p_glUniform1i(ctx->la.vram, 9);
    p_glActiveTexture(PSXGL_TEXTURE0 + 10);
    glBindTexture(GL_TEXTURE_2D, f->level >= 0 ? s_ddgi_level_nodes_tex[f->level] : 0);
    p_glActiveTexture(PSXGL_TEXTURE0 + 11);
    glBindTexture(GL_TEXTURE_2D, f->level >= 0 ? s_ddgi_level_tris_tex[f->level] : 0);
    p_glActiveTexture(PSXGL_TEXTURE0 + 12);
    if (!ctx->cast_tex) ctx->cast_tex = make_tex(DDGI_GL_RGBA32F, 2, GPU_DDGI_CASTERS, GL_RGBA, GL_FLOAT);
    glBindTexture(GL_TEXTURE_2D, ctx->cast_tex);
    if (caster_count) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, caster_count, GL_RGBA, GL_FLOAT, pictures);
    p_glActiveTexture(PSXGL_TEXTURE0 + 13);
    if (!ctx->mask_tex) ctx->mask_tex = make_tex(DDGI_GL_R32UI, GPU_DDGI_MASK_ROWS, GPU_DDGI_CASTERS, DDGI_GL_RED_INTEGER, GL_UNSIGNED_INT);
    glBindTexture(GL_TEXTURE_2D, ctx->mask_tex);
    if (caster_count)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_DDGI_MASK_ROWS, caster_count, DDGI_GL_RED_INTEGER, GL_UNSIGNED_INT, masks);
    p_glActiveTexture(PSXGL_TEXTURE0);
    p_glUniform1i(ctx->la.cast, 12);
    p_glUniform1i(ctx->la.mask, 13);
    p_glUniform1i(ctx->la.level_nodes, 10);
    p_glUniform1i(ctx->la.level_tris, 11);
    p_glUniform4f(ctx->la.level, f->level >= 0 ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    p_glUniform4f(ctx->la.sun, f->sun[0], f->sun[1], f->sun[2], f->sun[3]);
    p_glUniform4f(ctx->la.sun_colour, f->sun_colour[0], f->sun_colour[1], f->sun_colour[2], f->sun_colour[3]);
    const float fog_length = s_fade_step * (float)s_fade_rows;
    p_glUniform4f(ctx->la.fog, s_fade_start, fog_length, s_fade_side, s_fade_rows > 1 && fog_length > 0.0f ? 1.0f : 0.0f);
    p_glUniform4f(ctx->la.rect, u0, v0, u1, v1);
    p_glUniform4f(ctx->la.size, (float)lw, (float)lh, 0.0f, 0.0f);
    p_glUniform4f(ctx->la.eye, f->eye[0], f->eye[1], f->eye[2], 0.0f);
    /* The light a pixel has from elsewhere, against which a lamp's share is taken: tuned by eye, about a lamp 300 units off. */
    p_glUniform4f(ctx->la.count, (float)caster_count, (float)lamp_count, 0.0f, 0.03f);
    p_glUniform4f(ctx->la.shadow, shadows ? 0.85f * s_ddgi_shadow_strength : 0.0f, shadows ? 0.45f * s_ddgi_contact : 0.0f, 0.8f,
                  lit ? s_ddgi_lamp_strength : 0.0f);
    if (caster_count) p_glUniform4fv(ctx->la.caster, caster_count * 2, casters);
    if (lamp_count) p_glUniform4fv(ctx->la.lamp, lamp_count * 3, lamps);
    static GLuint timer[2];
    static unsigned passes[2];
    const int slot = thread ? 1 : 0, timing = getenv("PSX_DDGI") != NULL;
    if (timing) {
        if (!timer[slot]) p_glGenQueries(1, &timer[slot]);
        else if (passes[slot] % 120u == 119u) {
            GLuint64 spent = 0;
            p_glGetQueryObjectui64v(timer[slot], 0x8866 /* GL_QUERY_RESULT */, &spent);
            fprintf(stderr, "ddgi lamps %d: GPU %.3f ms a pass at %dx%d\n", slot, (double)spent / 1.0e6, lw, lh);
        }
        p_glBeginQuery(GL_TIME_ELAPSED, timer[slot]);
    }
    ddgi_draw(ctx->lamp_fbo, lw, lh);
    if (timing) {
        p_glEndQuery(GL_TIME_ELAPSED);
        ++passes[slot];
    }
    p_glActiveTexture(PSXGL_TEXTURE0 + 7);
    glBindTexture(GL_TEXTURE_2D, 0);
    p_glActiveTexture(PSXGL_TEXTURE0 + 8);
    glBindTexture(GL_TEXTURE_2D, 0);
    p_glActiveTexture(PSXGL_TEXTURE0 + 9);
    glBindTexture(GL_TEXTURE_2D, 0);
    p_glActiveTexture(PSXGL_TEXTURE0 + 10);
    glBindTexture(GL_TEXTURE_2D, 0);
    p_glActiveTexture(PSXGL_TEXTURE0 + 11);
    glBindTexture(GL_TEXTURE_2D, 0);
    p_glActiveTexture(PSXGL_TEXTURE0 + 12);
    glBindTexture(GL_TEXTURE_2D, 0);
    p_glActiveTexture(PSXGL_TEXTURE0 + 13);
    glBindTexture(GL_TEXTURE_2D, 0);
    p_glActiveTexture(PSXGL_TEXTURE0);
    ctx->lamp_ready = 1;
}

/* The shading's mix pass with the probes' and the lamps' light, when either has something to give. 0: the plain mix is used. */
static int ddgi_mix_begin(int thread, GLuint color, GLuint depth, float focal_x, float focal_y, float centre_x, float centre_y, int w,
                          int h, int scale, float u0, float v0, float u1, float v1, float span_x, float span_y, float view,
                          float grown) {
    DdgiContext *ctx = s_ddgi_ready ? ddgi_context(thread) : NULL;
    if (!ctx || !ctx->frame.valid) return 0;
    const int probes = s_ddgi_light && s_ddgi_applies && ctx->screen.frames,
              lamps = ctx->lamp_ready && (s_ddgi_shadows || s_ddgi_lamps || ddgi_sun_active());
    if (!probes && !lamps) return 0;
    if (thread && s_ddgi_probe_fence) p_glWaitSync(s_ddgi_probe_fence, 0, PSXGL_TIMEOUT_IGNORED);
    ddgi_bind_probes(color, depth);
    p_glActiveTexture(PSXGL_TEXTURE0 + 5);
    glBindTexture(GL_TEXTURE_2D, ctx->screen.average_tex);
    p_glActiveTexture(PSXGL_TEXTURE0 + 7);
    glBindTexture(GL_TEXTURE_2D, lamps ? ctx->lamp_tex : 0);
    p_glActiveTexture(PSXGL_TEXTURE0);
    p_glUseProgram(ctx->mix_prog);
    ddgi_apply_set(&ctx->mu, &ctx->frame, focal_x, focal_y, centre_x, centre_y, w, h, scale);
    p_glUniform1i(ctx->mu.shade, 1);
    p_glUniform1i(ctx->mu.exposure, 5);
    p_glUniform1i(ctx->mix_lamps, 7);
    p_glUniform4f(ctx->mix_lit, lamps ? 1.0f : 0.0f, lamps && s_ddgi_lamp_view ? 1.0f : 0.0f, probes ? 1.0f : 0.0f, 0.0f);
    const float fog_length = s_fade_step * (float)s_fade_rows;
    p_glUniform4f(ctx->mix_fog, s_fade_start, fog_length, s_fade_side, s_fade_rows > 1 && fog_length > 0.0f ? 1.0f : 0.0f);
    p_glUniform4f(ctx->mu.uv_rect, u0, v0, u1, v1);
    p_glUniform4f(ctx->mu.span, span_x, span_y, view, grown);
    return 1;
}

/* The presenter's frame at alpha between the two it draws between: the camera and the paired casters and lights moved along. */
static void ddgi_present_between(const DdgiFrame *before, const DdgiFrame *after, float alpha) {
    DdgiFrame *shown = &s_ddgi_ctx[1].frame;
    *shown = *after;
    if (!after->valid || !before->valid || alpha >= 1.0f || before->pose == after->pose) return;
    const float keep = 1.0f - (alpha < 0.0f ? 0.0f : alpha);
    psx_ddgi_camera_between(before->rows, before->offset, after->rows, after->offset, keep, shown->rows, shown->offset,
                            shown->back, shown->eye);
    if (after->scene.before != before->scene.pose) return;
    for (int i = 0; i < shown->scene.casters; ++i) {
        GpuDdgiCaster *c = &shown->scene.caster[i];
        if (c->previous < 0 || c->previous >= before->scene.casters) continue;
        const GpuDdgiCaster *was = &before->scene.caster[c->previous];
        for (int a = 0; a < 3; ++a) {
            c->centre[a] = was->centre[a] * keep + c->centre[a] * (1.0f - keep);
            c->picture[a] = was->picture[a] * keep + c->picture[a] * (1.0f - keep);
        }
        c->across = was->across * keep + c->across * (1.0f - keep);
        c->tall = was->tall * keep + c->tall * (1.0f - keep);
    }
    for (int i = 0; i < shown->scene.lamps; ++i) {
        GpuDdgiLamp *l = &shown->scene.lamp[i];
        if (l->previous < 0 || l->previous >= before->scene.lamps) continue;
        const GpuDdgiLamp *was = &before->scene.lamp[l->previous];
        for (int a = 0; a < 3; ++a) l->position[a] = was->position[a] * keep + l->position[a] * (1.0f - keep);
        l->weight = was->weight * keep + l->weight * (1.0f - keep);
    }
}

static void ddgi_capture_set(void) {
    gpu_ddgi_set_capture(s_ddgi_light || s_ddgi_shadows || s_ddgi_lamps || s_ddgi_sun);
    gpu_ddgi_set_shadowing(s_ddgi_shadows);
    s_ddgi_last_pose = 0;
}

void gl_renderer_set_ddgi(int enabled) {
    s_ddgi_light = enabled != 0 && !s_ddgi_failed;
    s_ddgi_ctx[0].screen.frames = s_ddgi_ctx[1].screen.frames = 0;
    ddgi_capture_set();
    if (!s_ddgi_light) {
        s_ddgi_applies = 0;
        s_ddgi_slots_known = 0;
        s_ddgi_updates = 0;
    }
    ssao_mode();
}
int gl_renderer_ddgi(void) { return s_ddgi_light; }
void gl_renderer_set_ddgi_strength(float strength) { s_ddgi_strength = strength < 0.0f ? 0.0f : strength > 2.0f ? 2.0f : strength; }
float gl_renderer_ddgi_strength(void) { return s_ddgi_strength; }
void gl_renderer_set_ddgi_view(int enabled) { s_ddgi_light_view = enabled != 0; }
int gl_renderer_ddgi_view(void) { return s_ddgi_light_view; }
void gl_renderer_set_ddgi_bounce(float bounce) { s_ddgi_bounce = bounce < 0.0f ? 0.0f : bounce > 1.0f ? 1.0f : bounce; }
float gl_renderer_ddgi_bounce(void) { return s_ddgi_bounce; }
void gl_renderer_set_ddgi_glow(float glow) { s_ddgi_glow = glow < 0.0f ? 0.0f : glow > 4.0f ? 4.0f : glow; }
float gl_renderer_ddgi_glow(void) { return s_ddgi_glow; }
void gl_renderer_set_ddgi_shadows(int enabled) {
    s_ddgi_shadows = enabled != 0 && !s_ddgi_failed;
    ddgi_capture_set();
    ssao_mode();
}
int gl_renderer_ddgi_shadows(void) { return s_ddgi_shadows; }
void gl_renderer_set_ddgi_shadow_strength(float strength) {
    s_ddgi_shadow_strength = strength < 0.0f ? 0.0f : strength > 2.0f ? 2.0f : strength;
}
float gl_renderer_ddgi_shadow_strength(void) { return s_ddgi_shadow_strength; }
void gl_renderer_set_ddgi_contact(float strength) { s_ddgi_contact = strength < 0.0f ? 0.0f : strength > 2.0f ? 2.0f : strength; }
float gl_renderer_ddgi_contact(void) { return s_ddgi_contact; }
void gl_renderer_set_ddgi_lamps(int enabled) {
    s_ddgi_lamps = enabled != 0 && !s_ddgi_failed;
    ddgi_capture_set();
    ssao_mode();
}
int gl_renderer_ddgi_lamps(void) { return s_ddgi_lamps; }
void gl_renderer_set_ddgi_lamp_strength(float strength) {
    s_ddgi_lamp_strength = strength < 0.0f ? 0.0f : strength > 4.0f ? 4.0f : strength;
}
float gl_renderer_ddgi_lamp_strength(void) { return s_ddgi_lamp_strength; }
void gl_renderer_set_ddgi_lamp_view(int enabled) { s_ddgi_lamp_view = enabled != 0; }
int gl_renderer_ddgi_lamp_view(void) { return s_ddgi_lamp_view; }
void gl_renderer_set_ddgi_sun(int enabled) {
    s_ddgi_sun = enabled != 0 && !s_ddgi_failed;
    ddgi_capture_set();
    ssao_mode();
}
int gl_renderer_ddgi_sun(void) { return s_ddgi_sun; }
void gl_renderer_set_ddgi_mission(int mission) {
    if (mission == s_ddgi_mission) return;
    s_ddgi_mission = mission;
    if (getenv("PSX_DDGI")) fprintf(stdout, "psxrecomp: DDGI mission %d\n", mission);
    ddgi_level_clear();
    gpu_ddgi_forget_level();
    ssao_mode();
}
int gl_renderer_ddgi_sun_here(void) { return s_ddgi_mission >= 0 && s_ddgi_mission < 16 && s_ddgi_suns[s_ddgi_mission].strength > 0.0f; }
void gl_renderer_ddgi_sun_get(float sun[4]) {
    const DdgiSun *s = &s_ddgi_suns[s_ddgi_mission >= 0 && s_ddgi_mission < 16 ? s_ddgi_mission : 0];
    sun[0] = s->azimuth;
    sun[1] = s->elevation;
    sun[2] = s->strength;
    sun[3] = s->shade;
}
void gl_renderer_ddgi_sun_set(const float sun[4]) { gl_renderer_ddgi_sun_set_mission(s_ddgi_mission, sun); }
void gl_renderer_ddgi_sun_set_mission(int mission, const float sun[4]) {
    if (mission < 0 || mission >= 16) return;
    DdgiSun *s = &s_ddgi_suns[mission];
    s->azimuth = sun[0];
    s->elevation = sun[1] < 1.0f ? 1.0f : sun[1] > 89.0f ? 89.0f : sun[1];
    s->strength = sun[2] < 0.01f ? 0.01f : sun[2] > 4.0f ? 4.0f : sun[2];
    s->shade = sun[3] < 0.0f ? 0.0f : sun[3] > 0.9f ? 0.9f : sun[3];
    ssao_mode(); /* a shade of 0 takes the sun out of the path, a shade above 0 puts it back */
}
