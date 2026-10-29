#!/usr/bin/env python3
"""Source contract for a world face with a corner at or behind the viewer's plane: the mirror draws it with a real w."""

import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
overlay = read("framework/FILES.txt")
header = read("psxrecomp/runtime/include/gpu_near_face.h")
precision = read("psxrecomp/runtime/include/gte_precision.h")
gte = read("psxrecomp/runtime/src/gte.cpp")
gpu = read("psxrecomp/runtime/src/gpu.c")
backend = read("psxrecomp/runtime/src/gpu_render.c")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
presenter = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
matching = read("psxrecomp/runtime/include/gpu_temporal_match.h")

require(
    overlay.count("+runtime/include/gpu_near_face.h\n") == 1
    and '#include "gpu_near_face.h"\n' in gpu
    and '#include "gpu_near_face.h"\n' in renderer
    and "    disruptor_unit_test(disruptor_gpu_near_face tests/unit/gpu_near_face_test.cpp RUNTIME_HEADERS)\n" in cmake
    and "    disruptor_contract(near_face)\n" in cmake,
    "the header is applied to the framework, and its test and this contract are registered",
)

require(
    "static inline int psx_near_depth(int32_t w, uint16_t focal) { return (int64_t)w * 2 <= (int64_t)focal; }\n"
    in header
    and "        if (corner[i].focal == 0 || corner[i].focal != corner[0].focal || corner[i].centre_x != corner[0].centre_x ||\n"
    "            corner[i].centre_y != corner[0].centre_y)\n"
    "            return 0;\n"
    "        near_one |= pinned[i] || psx_near_depth(corner[i].w, corner[i].focal);\n"
    in header
    and "    const double w = corner->w != 0 ? (double)corner->w : -0.25;\n" in header
    and "    place[0] = (float)(corner->centre_x + shift_x + corner->hx / w);\n" in header,
    "a polygon is near when its corners come from one projection and one lies where the GTE's divide stops or was pinned, "
    "and a corner's place is its exact divide by its own w, a quarter behind the viewer when it lies on the plane",
)

require(
    "    const double x = nx + ((double)place[0] - nx) * share, y = ny + ((double)place[1] - ny) * share;\n"
    "    double w = weight + (1.0 - weight) * share;\n"
    "    if (w == 0.0) w = -1e-6;\n"
    "    place[0] = (float)(x / w);\n"
    "    place[1] = (float)(y / w);\n"
    "    place[2] = (float)((double)place[2] * w);\n"
    in header
    and "    const double ratio = (double)other[2] / (double)place[2];\n"
    "    psx_near_between((double)other[0] * ratio, (double)other[1] * ratio, ratio, share, place);\n"
    in header
    and "    return (found[2] > 0.0f) == (depth > 0.0) && fabs((double)found[2] - depth) <= 0.2 * fabs(depth) + 8.0 &&\n"
    "           fabs((double)found[0] - x) + fabs((double)found[1] - y) <= 0.2 * (fabs(x - 160.0) + fabs(y - 120.0)) + 16.0;\n"
    in header,
    "between frames a corner moves in homogeneous terms, counts as behind where it crosses the viewer's plane, and a corner of the "
    "other frame is its pair only where the camera expects it",
)

require(
    "        if (there[0] != model[0] + sign * step[0] || there[1] != model[1] + sign * step[1] || there[2] != model[2] + sign * step[2] ||\n"
    "            !psx_near_agrees(other[i].place, nx, ny, weight, w))\n"
    "            continue;\n"
    "        miss = psx_near_miss(other[i].place, nx, ny, weight, w);\n"
    "        if (best < 0 || miss < least) {\n"
    in header
    and "    return psx_near_find(own->near_corners,own->near_count,vertex,s->near_corners,s->near_count,step,sign,nx,ny,weight,w,place);\n"
    in presenter
    and "    PsxNearCorner near_corners[TEMP_NEAR_CORNERS];" in presenter,
    "of the other frame's corners with the model a step away that agree with the camera the nearest is the pair, and the presenter "
    "finds a pair by that rule alone",
)

require(
    "    int32_t view_x, view_y, view_z;" in gte
    and "        s_precise_sxy[2].view_x = gte->IR1;\n"
    "        s_precise_sxy[2].view_y = gte->IR2;\n"
    "        s_precise_sxy[2].view_z = gte->MAC3;\n"
    "        s_precise_sxy[2].centre_x16 = gte->OFX;\n"
    "        s_precise_sxy[2].centre_y16 = gte->OFY;\n"
    "        s_precise_sxy[2].focal = gte->H;\n"
    "        s_precise_sxy[2].squash_num = do_squash ? s_ws_xnum : 1;\n"
    "        s_precise_sxy[2].squash_den = do_squash ? s_ws_xden : 1;\n"
    "        s_precise_sxy[2].viewed = do_squash || xterm == (int64_t)gte->IR1 * h_div_sz;\n"
    in gte
    and "    if (tag.generation != s_precision_generation || tag.addr != physical || !projection.valid || projection.packed != packed ||\n"
    "        !projection.viewed)\n"
    "        return 0;\n"
    "    view->hx = (double)projection.view_x * projection.focal * (double)projection.squash_num / (double)projection.squash_den;\n"
    in gte
    and "    view->w = projection.view_z;\n" in gte
    and "    p.viewed = 0;\n" in gte
    and "int gte_precision_load_view_word(uint32_t addr, uint32_t packed, GtePrecisionView *view);" in precision,
    "the GTE keeps each corner as it stood before the divide, with the squash it was projected under, and gives it for the stored word alone",
)

require(
    "    s_near_face = 0;\n" in gpu
    and "            if (grown || !gte_precision_load_view_word(addr, word, &view[i])) viewed = 0;\n" in gpu
    and "    if (viewed && psx_near_polygon(view, pinned, count)) {\n" in gpu
    and "            psx_near_place(&view[i], (double)(vx[i] - raw_x[i]), (double)(vy[i] - raw_y[i]), s_near_place[i]);\n"
    "            if (got[i] && !pinned[i] && !psx_near_depth(view[i].w, view[i].focal)) {"
    in gpu
    and "        s_near_face = 1;\n        resolved = count;\n" in gpu
    and "            if (s_near_face && !s_precise_vertex_identity[i] && view[i].identity)\n"
    "                s_precise_vertex_identity[i] = view[i].identity | PSX_NEAR_IDENTITY;\n"
    in gpu
    and "    if (s_near_face) gr_set_near_triangle(s_near_place[a], s_near_place[b], s_near_place[c]);\n" in gpu
    and "    if (g_b->set_near_triangle) g_b->set_near_triangle(a, b, c);\n" in backend,
    "the runtime takes a polygon for near only with every corner's view, keeps an ordinary corner where its neighbours have it, "
    "marks a level point the view alone gave and hands each triangle's corners to the renderer",
)

require(
    "#define PSXGL_DEPTH_CLAMP           0x864F\n" in renderer
    and "    s_near_set = exact && s_next_near_set && s_near_shader;\n    s_next_near_set = 0;\n" in renderer
    and "    s_next_precise = enabled ? 1 : 0;\n    s_next_near_set = 0;" in renderer
    and "        s_near_shader = tex_fs != NULL;\n" in renderer
    and "    const int near_face = s_near_set;\n    const int cut_corners = near_face ? -1 : turn_clip(px, py, visual_flags, cut);\n"
    in renderer
    and "        int world = near_face ? 0 : psx_world_depth_class(s_world_depth, visual_flags, turned_z, s_world_margin);\n"
    in renderer
    and "            if (near_face) { vp[19] = 4.0f; vp[20] = s_near[i][0]; vp[21] = s_near[i][1]; vp[22] = s_near[i][2]; }\n"
    in renderer
    and renderer.count("    glEnable(PSXGL_DEPTH_CLAMP);") == 1
    and presenter.count("    glEnable(PSXGL_DEPTH_CLAMP);") == 1,
    "a near triangle is an exact one of the marked shader whose corners no other triangle can take, is never cut by the turn clip "
    "nor depth-sorted, carries the mark 4 with its place and w, and both contexts clamp depth instead of clipping it",
)

require(
    '    "  bool close = corrected && a_visual_flags > 3.5 && a_visual_flags < 4.5;\\n"\n'
    '    "  float hw = close ? a_q : 1.0;\\n"\n'
    in renderer
    and '    "    if (close && abs(den) < 0.01) den = den < 0.0 ? -0.01 : 0.01;  /* as psx_near_turn */\\n"\n'
    '    "    if (abs(den) > 0.01 || close) {\\n"\n'
    in renderer
    and '    "      if (close) hw *= den;\\n"\n' in renderer
    and "static inline float psx_near_turn(float den) { return fabsf(den) < 0.01f ? (den < 0.0f ? -0.01f : 0.01f) : den; }\n"
    in header
    and "        if (near_corner) den = psx_near_turn(den);\n        if (fabsf(den) > 0.01f || near_corner) {\n"
    in presenter
    and "            if (near_corner) v[22] *= den;" in presenter
    and '    "  if (close) { v_uvq = vec3(0.0); v_perspective = 2; }\\n"\n    "  v_uvw = v_uv;\\n"\n' in renderer
    and '    "  if (v_perspective == 2) sample_uv = v_uvw;\\n"\n' in renderer
    and '    static const char closing[] = "  if (close) { v_deep = 1.0; v_fade = vec2(-1.0, 0.0);\\n"\n' in renderer
    and '"    if (u_visual_options.w <= 0.0) { gl_Position.z = 1.0 - 32.0*(1.0 + u_pull)/hw; gl_Position *= hw; } '
    'else gl_Position = vec4(2.0, 2.0, 2.0, 1.0); }\\n";\n'
    in renderer,
    "the shader multiplies a near corner's position by its w, turns it at any angle with the w taking the turn's sign as the "
    "presenter does, reads the texture by the rasterizer's own perspective, orders the face by its w and draws it in the scene's passes alone",
)

require(
    "            triangle->q[vertex] < 0.0f) return 0;\n" in matching
    and "        if (textured && s->vertices[(s->nv+i)*TEXV+19]>3.5f && s->vertices[(s->nv+i)*TEXV+19]<4.5f) t->q[0]=t->q[1]=t->q[2]=-1.0f;\n"
    in presenter
    and "        t->modelled = !sprite && !partial && !cut && identities && identities[i] && identities[i+1] && identities[i+2] &&\n"
    "            !((identities[i] | identities[i+1] | identities[i+2]) & PSX_NEAR_IDENTITY);\n"
    in presenter,
    "a near face takes no part in the matching by look, and no face is paired by a level point the view alone gave",
)

require(
    "            if (textured && v[19]>3.5f && v[19]<4.5f && identities && identities[i+j]>>63 && s->near_count<TEMP_NEAR_CORNERS) {\n"
    "                s->near_corners[s->near_count].vertex=s->nv+i+j;\n"
    in presenter
    and "                s->near_corners[s->near_count].place[0]=v[0]; s->near_corners[s->near_count].place[1]=v[1];\n"
    "                s->near_corners[s->near_count++].place[2]=v[22];\n"
    in presenter
    and "    for (int i=0; i<s->near_count; ++i) s->near_corners[i].place[1]-=y;\n" in presenter
    and "    s->near_count = 0;\n" in presenter,
    "a scene keeps each near corner's level point, place and w in the order drawn, in rows counted from its own origin",
)

require(
    "                int carried=s->camera.inliers>=GPU_TEMPORAL_CAMERA_MIN_INLIERS;\n"
    "                for (int k=0;k<3 && carried;++k) {\n"
    "                    const float *v=&s_temp_scratch[(j+k)*TEXV];\n"
    "                    carried=gpu_temporal_camera_project_h(&s->camera,v[20],v[21],v[22],&nx[k],&ny[k],&weight[k]);\n"
    "                }\n"
    "                for (int k=0;k<3 && carried;++k) {\n"
    "                    float *v=&s_temp_scratch[(j+k)*TEXV], before[3];\n"
    "                    if (temporal_near_find(s,c->first+j+k,&s_temp_prev,s->camera.shift,1,nx[k],ny[k],weight[k],v[22],before))\n"
    "                        psx_near_towards(before,alpha,&v[20]);\n"
    "                    else psx_near_between(nx[k],ny[k],weight[k],alpha,&v[20]);\n"
    in presenter
    and "                show=p->gone[ti] && opaque && s->onward_ok;\n"
    "                for (int k=0;k<3 && show;++k) {\n"
    "                    const float *v=&s_temp_under[(j+k)*TEXV];\n"
    "                    show=gpu_temporal_camera_project_h(&s->onward,v[20],v[21],v[22],&nx[k],&ny[k],&weight[k]);\n"
    "                }\n"
    in presenter
    and "                    else if (temporal_near_find(p,c->first+j+k,s,s->camera.shift,-1,nx[k],ny[k],weight[k],v[22],after)) "
    "psx_near_towards(after,1.0f-alpha,&v[20]);\n"
    "                    else psx_near_between(nx[k],ny[k],weight[k],1.0f-alpha,&v[20]);\n"
    "                    if (covered) psx_near_farther(&v[20],TEMP_ORDER_CLEARANCE);\n"
    "                    v[0]=v[20]; v[1]=v[21];\n"
    in presenter
    and "        s->onward_ok=s->camera.inliers>=GPU_TEMPORAL_CAMERA_MIN_INLIERS && gpu_temporal_camera_invert(&s->camera,&s->onward);\n"
    in presenter
    and "                    if (!show) psx_near_hide(&v[20]);\n" in presenter
    and "    place[0] = place[1] = 0.0f;\n    place[2] = 1.0f;\n" in header,
    "between frames a near face moves only when the camera carries all three corners, each from its own place in the frame before "
    "or by the camera, and a near face the game dropped goes under the scene the same way by the camera's inverse or not at all",
)


def window_step(order: float) -> int:
    single = struct.unpack("f", struct.pack("f", order))[0]
    return round(struct.unpack("f", struct.pack("f", single * 0.5 + 0.5))[0] * (2**24 - 1))


nearest = re.search(r"#define TEMP_ORDER_NEAR \((-0\.\d+)f\)\n", presenter)
over = re.search(r"#define TEMP_ORDER_OVER_NEAREST (0\.\d+) ", presenter)
clearance = re.search(r"#define TEMP_ORDER_CLEARANCE (1\.\d+)f\n", presenter)
require(
    nearest is not None
    and over is not None
    and clearance is not None
    and "static inline void psx_near_farther(float place[3], float clearance) { place[2] *= clearance; }\n" in header
    and "    glDepthFunc(covered ? GL_LESS : GL_ALWAYS); glDepthMask(covered ? GL_FALSE : GL_TRUE);\n"
    "    if (covered) glDepthRange(TEMP_ORDER_OVER_NEAREST,1.0);\n"
    in presenter
    and "    glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE); glDepthRange(0.0,1.0);\n"
    "    s_temp_scene_depth=1; s_temp_underlaying=0;\n"
    in presenter
    and presenter.count("glDepthRange(") == 2
    and presenter.count("psx_near_farther(") == 1
    and window_step(float(nearest[1])) < round(float(over[1]) * (2**24 - 1))
    and float(over[1]) < (1.0 - 1.0 / float(clearance[1])) * 16.0 / 65535.0,
    "a near face the game dropped is drawn again over the scene with its w larger by the clearance, in a depth range that starts "
    "above the depth step of what has no depth and takes less of the farthest face's depth than the clearance does",
)

print("Disruptor near face source contract: PASS")
