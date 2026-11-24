static int s_factory_water_active;
static uint32_t s_factory_water_ticks;

void gl_renderer_set_factory_water(int active, uint32_t ticks) {
    int32_t words[] = {active, 0};
    memcpy(&words[1], &ticks, sizeof(ticks));
    if (rth_rec_call(RTH_FACTORY_WATER, words, 2)) return;
    s_factory_water_active = active;
    s_factory_water_ticks = ticks;
}

static int factory_material(uint16_t page, uint16_t clut_x, uint16_t clut_y, const int us[3], const int vs[3]) {
    if (!s_factory_water_active || ((page >> 7) & 3) != 1 || clut_x != 320) return 0;
    if (clut_y >= 288 && clut_y < 312) return clut_y < 300 ? 1 : 2;
    if (clut_y < 216 || clut_y >= 228) return 0;

    const int u = us[0] & 224, v = vs[0] & 224;
    for (int i = 1; i < 3; ++i)
        if ((us[i] & 224) != u || (vs[i] & 224) != v) return 0;
    const int x = ((page & 15) * 64 + u / 2) & 1023;
    const int y = ((page & 16) * 16 + v) & 511;
    static const int corners[4][2] = {{992, 256}, {976, 384}, {976, 352}, {976, 320}};
    for (int corner = 0; corner < 4; ++corner)
        if (x == corners[corner][0] && y == corners[corner][1]) return (corner << 2) | 3;
    return 0;
}

#define FACTORY_SIGN_FRAGMENT \
    "int g_factory_sign; bool g_factory_sign_mirrored;\n" \
    "const int factory_sign_colors[16] = int[16](0, 17, 21, 46, 51, 76, 86, 94, 120, 185, 201, 4, 12, 52, 63, 172);\n" \
    "const uint factory_sign_texels[390] = uint[390](\n" \
    "  0x20000000u, 0x22444444u, 0x20000022u, 0x00025555u, 0x55555500u, 0x00052255u,\n" \
    "  0x00000000u, 0x44442000u, 0x00222244u, 0x55552000u, 0x55000002u, 0x22555555u,\n" \
    "  0x00000005u, 0x20000000u, 0x55444444u, 0x11331111u, 0x66663355u, 0x11115566u,\n" \
    "  0x00051122u, 0x00000000u, 0x44442000u, 0x11115544u, 0x33551133u, 0x55666666u,\n" \
    "  0x11221111u, 0x00000005u, 0x20000000u, 0x4444AFFFu, 0x44FA11AFu, 0x66FFF38Fu,\n" \
    "  0xFF21FFAAu, 0x0008224Fu, 0x00000000u, 0xF44A2000u, 0x11AFA44Au, 0x2A8F4AAAu,\n" \
    "  0x112A6226u, 0x22F21AA1u, 0x00000008u, 0x20000000u, 0xF24FA46Fu, 0x2FAA22A4u,\n" \
    "  0x44442F2Fu, 0x7AA122AAu, 0x000822F2u, 0x00000000u, 0x446F0000u, 0x22F4F24Fu,\n" \
    "  0x2F2FAF2Au, 0xAFFA4AA4u, 0x02AAFAA1u, 0x00000000u, 0x00000000u, 0xAA2F222Fu,\n" \
    "  0xFAAA2AAAu, 0x6AA68F8Fu, 0x2AA2882Au, 0x0000082Fu, 0x00000000u, 0xA00F0000u,\n" \
    "  0x0FA0AA2Au, 0xAA0BF0AAu, 0x002202A0u, 0x00AA2AA2u, 0x00000000u, 0x00000000u,\n" \
    "  0x2F22AFFFu, 0xA0AA0F22u, 0x022AF00Fu, 0x2AA2FFFAu, 0x000000F5u, 0x00000000u,\n" \
    "  0x00000000u, 0x00222000u, 0x00000000u, 0x11000000u, 0x00000002u, 0x00000000u,\n" \
    "  0x00000000u, 0xA2222000u, 0x0000002Au, 0x00000000u, 0x22221100u, 0x00000008u,\n" \
    "  0x00000000u, 0x20000000u, 0x002AA222u, 0x00000000u, 0x11000000u, 0x00082222u,\n" \
    "  0x00000000u, 0x00000000u, 0xAA222000u, 0x0000008Au, 0x00000000u, 0x22A13300u,\n" \
    "  0x00000009u, 0x00000000u, 0x20000000u, 0x008AAA22u, 0x00000000u, 0x33000000u,\n" \
    "  0x000922A1u, 0x00000000u, 0x00000000u, 0xAB222000u, 0x0000008Au, 0x00000000u,\n" \
    "  0x44B43300u, 0x00000002u, 0x00000000u, 0x20000000u, 0x008AAF22u, 0x00000000u,\n" \
    "  0x33000000u, 0x000244F4u, 0x00000000u, 0x00000000u, 0xAF222000u, 0x0000008Au,\n" \
    "  0x00000000u, 0x44F16600u, 0x00000005u, 0x00000000u, 0x20000000u, 0x008BBA22u,\n" \
    "  0xAAAAAA00u, 0xA6000006u, 0x000544CAu, 0x00000000u, 0x00000000u, 0xBA222000u,\n" \
    "  0xAA00002Bu, 0x0006AAAAu, 0x44CAA700u, 0x00000001u, 0x00000000u, 0x20000000u,\n" \
    "  0x2AADDA22u, 0xFFFFDDF2u, 0xD7666ECCu, 0x000144ADu, 0x00000000u, 0x00000000u,\n" \
    "  0xDA222000u, 0xDDF22AADu, 0x6ECCFFFFu, 0x99ADD366u, 0x00000004u, 0x00000000u,\n" \
    "  0x20000000u, 0xDDDDDAAAu, 0x118822FDu, 0xDDDEE666u, 0x000499ADu, 0x00000000u,\n" \
    "  0x00000000u, 0xDDDDFF00u, 0xAAFDDDDDu, 0xEA440000u, 0xDDDDDDDEu, 0x0000AABBu,\n" \
    "  0x00000000u, 0xFF222220u, 0xDDDDDDDDu, 0x7777AAFDu, 0xDDDEEA44u, 0xAABBDDDDu,\n" \
    "  0x00000888u, 0xA2200000u, 0xAAAABBFFu, 0xDDDDDDDAu, 0xEECC22AAu, 0x228AACCEu,\n" \
    "  0x081ABB22u, 0x80000000u, 0xBBFFA222u, 0xDDDAAAAAu, 0x22AADDDDu, 0xACCEEECCu,\n" \
    "  0xBB22228Au, 0x0008881Au, 0xB2222000u, 0xA2222222u, 0xFFF22BBAu, 0x3EAA8899u,\n" \
    "  0x888AAAA7u, 0x22111111u, 0x20000002u, 0x2222B222u, 0x2BBAA222u, 0x8899FFF2u,\n" \
    "  0xAAA73EAAu, 0x1111888Au, 0x00022211u, 0xAAA00000u, 0xA2200000u, 0xAA222BBAu,\n" \
    "  0x66778888u, 0x088BBAA6u, 0x01110000u, 0x20000000u, 0x00004AA2u, 0x2CC22220u,\n" \
    "  0xFFFFAA22u, 0xABB666AAu, 0x0000088Au, 0x00021111u, 0x4AA22000u, 0x22200000u,\n" \
    "  0xAA002CC2u, 0x00AAFFFFu, 0x088AABB6u, 0x11110000u, 0x20000007u, 0x00004422u,\n" \
    "  0xBBB22220u, 0xDDDD222Bu, 0x3DD60611u, 0x00000888u, 0x00071111u, 0x00224000u,\n" \
    "  0x77200000u, 0x225BBBB2u, 0x0644DDDDu, 0x08773DD4u, 0x11000000u, 0x40000007u,\n" \
    "  0x00000022u, 0xC2227720u, 0xDDFF22BCu, 0x334CC644u, 0x00000877u, 0x00071100u,\n" \
    "  0x00000000u, 0x40000000u, 0x20BCC222u, 0xC603DDFFu, 0x0006331Cu, 0x00000000u,\n" \
    "  0x00000000u, 0x00000000u, 0x22224000u, 0xDDFFFFF2u, 0x33116EEEu, 0x00000006u,\n" \
    "  0x00000000u, 0x00000000u, 0x00000000u, 0xAA222210u, 0x66AADDDDu, 0x00000644u,\n" \
    "  0x00000000u, 0x00000000u, 0x00000000u, 0x22100000u, 0xDDDDAA22u, 0x064466AAu,\n" \
    "  0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xBB552000u, 0x66EEDDDDu,\n" \
    "  0x00000006u, 0x00000000u, 0x00000000u, 0x00000000u, 0x20000000u, 0xDDDDBB55u,\n" \
    "  0x000666EEu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xDD200000u,\n" \
    "  0x0AEE33AAu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xA0000000u,\n" \
    "  0x3377BBCAu, 0x000BBA66u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,\n" \
    "  0xBBCAA000u, 0xBA664477u, 0x0000000Bu, 0x00000000u, 0x00000000u, 0x00000000u,\n" \
    "  0xAAAAA000u, 0x4477222Au, 0xAAA66666u, 0x000000AAu, 0x00000000u, 0x00000000u,\n" \
    "  0xA0000000u, 0x222AAAAAu, 0x66666611u, 0x00AAAAA6u, 0x00000000u, 0x00000000u,\n" \
    "  0x00000000u, 0x40000000u, 0x66112222u, 0x00066666u, 0x00000000u, 0x00000000u,\n" \
    "  0x00000000u, 0x77222000u, 0x00229911u, 0x66000000u, 0x11666666u, 0x00000007u,\n" \
    "  0x00000000u, 0x20F00FAFu, 0xA9A17AFAu, 0xF000F02Au, 0x66F66AFFu, 0xAF07FFF6u,\n" \
    "  0x0000000Fu, 0x2A0F0000u, 0x1F2AA0F2u, 0xF042A9F6u, 0x66F0000Au, 0x212664F2u,\n" \
    "  0x00A26F0Au, 0x00000000u, 0xA0F22A0Fu, 0x20F0AA00u, 0x000F220Au, 0x021A00F0u,\n" \
    "  0x6F0A20A0u, 0x000000F6u, 0x6FFA0000u, 0xAA44F4F4u, 0x4A4AFFF0u, 0x666F662Fu,\n" \
    "  0xFFF40F0Fu, 0x04F33A44u, 0x00000000u, 0xA4F4A42Fu, 0x20F0AA44u, 0x662FAF4Au,\n" \
    "  0x0FAF666Fu, 0x3F44F2F4u, 0x000004F3u, 0xA22A0000u, 0x3F4AA4F4u, 0x1F7AA4F7u,\n" \
    "  0x4666F6AAu, 0xF1A63A6Au, 0x04AA6A11u, 0x00000000u, 0x44F46FFFu, 0x24A73AFAu,\n" \
    "  0xF6F1127Au, 0xA266A2FFu, 0xFF12A1A6u, 0x0000046Fu, 0x33200000u, 0x11444477u,\n" \
    "  0x88444477u, 0x66663399u, 0x77661166u, 0x04666644u, 0x00000000u, 0x44773320u,\n" \
    "  0x44771144u, 0x33998844u, 0x11666666u, 0x66447766u, 0x00000466u, 0x22200000u,\n" \
    "  0x44222222u, 0x22999922u, 0x66334422u, 0x77117777u, 0x07333311u, 0x00000000u,\n" \
    "  0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u);\n" \
    "int factory_sign_index(ivec2 point){\n" \
    "  int texel_index = point.y * 52 + point.x;\n" \
    "  uint word = factory_sign_texels[texel_index >> 3];\n" \
    "  return int((word >> uint((texel_index & 7) * 4)) & 15u);\n" \
    "}\n" \
    "vec3 factory_sign_sample(int index, vec3 original){\n" \
    "  return index == 0 ? original : col5(vram_at(v_clut.x + factory_sign_colors[index], g_row));\n" \
    "}\n" \
    "bool factory_sign_color(vec2 uv, int mode, inout vec3 rgb){\n" \
    "  if (g_row < 216 || g_row >= 228) return false;\n" \
    "  vec2 local = uv - vec2(v_sampled.xy & ivec2(224));\n" \
    "  if (any(lessThan(local, vec2(0.0))) || any(greaterThanEqual(local, vec2(32.0)))) return false;\n" \
    "  vec2 point = (vec2(g_factory_sign & 1, g_factory_sign >> 1) + clamp(local / 31.0, 0.0, 1.0)) * 64.0;\n" \
    "  if (g_factory_sign_mirrored) point.x = 128.0 - point.x;\n" \
    "  point -= vec2(38.0, 16.0);\n" \
    "  if (any(lessThan(point, vec2(0.0))) || any(greaterThanEqual(point, vec2(52.0, 60.0)))) return false;\n" \
    "  if (mode == 0 || mode == 3) {\n" \
    "    int index = factory_sign_index(ivec2(point));\n" \
    "    if (index == 0) return false;\n" \
    "    rgb = factory_sign_sample(index, rgb);\n" \
    "    return true;\n" \
    "  }\n" \
    "  vec2 centered = max(point - 0.5, vec2(0.0));\n" \
    "  ivec2 lo = ivec2(centered), hi = min(lo + ivec2(1), ivec2(51, 59));\n" \
    "  ivec4 indices = ivec4(factory_sign_index(lo), factory_sign_index(ivec2(hi.x, lo.y)),\n" \
    "                        factory_sign_index(ivec2(lo.x, hi.y)), factory_sign_index(hi));\n" \
    "  if (all(equal(indices, ivec4(0)))) return false;\n" \
    "  vec2 weight = fract(centered);\n" \
    "  vec3 top = mix(factory_sign_sample(indices.x, rgb), factory_sign_sample(indices.y, rgb), weight.x);\n" \
    "  vec3 bottom = mix(factory_sign_sample(indices.z, rgb), factory_sign_sample(indices.w, rgb), weight.x);\n" \
    "  rgb = mix(top, bottom, weight.y);\n" \
    "  return true;\n" \
    "}\n"

/* Integer river offsets preserve the texture phase when the 16-bit tick wraps. */
#define FACTORY_WATER_FRAGMENT \
    "uniform float u_factory_water_tick;\n" \
    "vec3 factory_water_texel(ivec2 pos, int side, int phase){\n" \
    "  pos = ivec2(mod(vec2(pos), float(32 * side)));\n" \
    "  if (side == 5 && phase == 1 &&\n" \
    "      ((pos.x >= 62 && pos.x < 68 && pos.y >= 9 && pos.y < 12) ||\n" \
    "       (pos.x >= 154 && pos.x < 157 && pos.y >= 106 && pos.y < 109) ||\n" \
    "       (pos.x >= 126 && pos.x < 129 && pos.y >= 133 && pos.y < 136) ||\n" \
    "       (pos.x >= 117 && pos.x < 120 && pos.y >= 152 && pos.y < 155))) phase = 0;\n" \
    "  int tile = (pos.y / 32) * side + pos.x / 32;\n" \
    "  ivec2 cell;\n" \
    "  if (side == 2) {\n" \
    "    cell = phase == 0 ? ivec2(43 - 2 * tile, 14) : ivec2(42 - 2 * tile, 13);\n" \
    "  } else {\n" \
    "    bool odd = (tile & 1) != 0;\n" \
    "    cell.x = 35 - tile / 2 - (odd && phase == 2 ? 1 : 0);\n" \
    "    cell.y = odd ? (phase == 2 ? 15 : 11 - phase) : 14 - phase;\n" \
    "  }\n" \
    "  int word = vram_at(cell.x * 16 + (pos.x & 31) / 2, cell.y * 32 + (pos.y & 31));\n" \
    "  return col5(vram_at(v_clut.x + ((word >> ((pos.x & 1) * 8)) & 255), g_row));\n" \
    "}\n" \
    "vec3 factory_water_sample(vec2 point, int side, int phase){\n" \
    "  vec2 edge = mod(point + 16.0, 32.0) - 16.0;\n" \
    "  vec2 at = point - 0.65 * edge * (1.0 - smoothstep(vec2(0.0), vec2(3.0), abs(edge))) - 0.5;\n" \
    "  ivec2 first = ivec2(floor(at)); vec2 weight = fract(at);\n" \
    "  vec3 top = mix(factory_water_texel(first, side, phase), factory_water_texel(first + ivec2(1, 0), side, phase), weight.x);\n" \
    "  vec3 bottom = mix(factory_water_texel(first + ivec2(0, 1), side, phase),\n" \
    "                    factory_water_texel(first + ivec2(1, 1), side, phase), weight.x);\n" \
    "  return mix(top, bottom, weight.y);\n" \
    "}\n" \
    "bool factory_water_color(vec2 uv, int family, out vec3 color){\n" \
    "  ivec2 origin = v_sampled.xy & ivec2(224);\n" \
    "  if (any(notEqual(origin, v_sampled.zw & ivec2(224)))) return false;\n" \
    "  ivec2 pixel = (ivec2(v_tpage.x * 2, v_tpage.y) + origin) & ivec2(2047, 511);\n" \
    "  int x = pixel.x / 32, y = pixel.y / 32;\n" \
    "  int side, tile, phase = 0;\n" \
    "  if (family == 1) {\n" \
    "    side = 2;\n" \
    "    if (y >= 6 && y <= 14 && x >= 36 && x <= 43) tile = (42 + (x & 1) - x) / 2;\n" \
    "    else if (y == 15 && x >= 35 && x <= 42) tile = (42 - (x & 1) - x) / 2;\n" \
    "    else return false;\n" \
    "  } else {\n" \
    "    side = 5;\n" \
    "    if (x < 23 || x > 35) return false;\n" \
    "    if (y >= 12 && y <= 14) { tile = 2 * (35 - x); phase = 14 - y; }\n" \
    "    else if (y == 10 || y == 11) { tile = 2 * (35 - x) + 1; phase = 11 - y; }\n" \
    "    else if (y == 15 && x <= 34) { tile = 2 * (34 - x) + 1; phase = 2; }\n" \
    "    else return false;\n" \
    "    if (tile >= 25) return false;\n" \
    "  }\n" \
    "  vec2 local = clamp((uv - vec2(origin)) / 31.0, 0.0, 1.0);\n" \
    "  vec2 point = (vec2(tile % side, tile / side) + local) * 32.0;\n" \
    "  if (family == 2) { color = factory_water_sample(point, side, phase); return true; }\n" \
    "  float tick = u_factory_water_tick > 0.0 ? u_factory_water_tick - 1.0 : v_material_animation.y;\n" \
    "  float time = tick / 1024.0;\n" \
    "  vec3 broad = factory_water_sample(point + vec2(0.0, -790.0) * time, side, 0);\n" \
    "  float footprint = max(length(g_dx), length(g_dy));\n" \
    "  float detail = 0.19 * (1.0 - smoothstep(0.6, 2.0, footprint));\n" \
    "  color = broad;\n" \
    "  if (detail > 0.0) {\n" \
    "    vec2 ripple = mat2(2.0, 1.0, -1.0, 2.0) * point + vec2(679.0, -1922.0) * time;\n" \
    "    color = mix(broad, factory_water_sample(ripple, side, 1), detail);\n" \
    "  }\n" \
    "  float visibility = float(299 - g_row) * (1.0 / 11.0);\n" \
    "  float value = dot(color, vec3(0.299, 0.587, 0.114));\n" \
    "  color = mix(color, mix(vec3(value), color, 0.88) * 0.86, visibility);\n" \
    "  return true;\n" \
    "}\n"
