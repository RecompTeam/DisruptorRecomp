#include "game/render/archive_terminal.h"

extern const unsigned char disruptor_archive_atlas[];
extern const size_t disruptor_archive_atlas_size;

static int s_archive_frame;
static int32_t s_archive_eye[3];
static GLuint s_archive_atlas;
static int s_archive_load_failed;

void gl_renderer_set_archive_terminal(int frame, int32_t eye_x, int32_t eye_y, int32_t eye_z) {
    const int32_t words[] = {frame, eye_x, eye_y, eye_z};
    if (rth_rec_call(RTH_ARCHIVE_TERMINAL, words, 4)) return;
    s_archive_frame = frame;
    s_archive_eye[0] = eye_x;
    s_archive_eye[1] = eye_y;
    s_archive_eye[2] = eye_z;
}

static int archive_terminal_frame(uint16_t page, uint16_t clut_x, uint16_t clut_y,
                                  const int us[3], const int vs[3], const uint64_t identity[3]) {
    if (!s_archive_frame || clut_x != 320u || clut_y != 336u) return 0;
    if ((page & ~0x10u) != 0x008bu && page != 0x009au) return 0;
    const int tile_left = us[0] & ~31, tile_top = vs[0] & ~31;
    int left = 0, right = 0;
    for (int i = 0; i < 3; ++i) {
        const uint64_t corner = identity[i];
        if (corner >> 48 != 0x8000u || (us[i] != tile_left && us[i] != tile_left + 31) ||
            (vs[i] != tile_top && vs[i] != tile_top + 31)) return 0;
        const int64_t x = (int16_t)corner + (int64_t)s_archive_eye[0];
        const int64_t y = (int16_t)(corner >> 16) + (int64_t)s_archive_eye[1];
        const int64_t z = (int16_t)(corner >> 32) + (int64_t)s_archive_eye[2];
        if (x != DISRUPTOR_ARCHIVE_SCREEN_X || (y != DISRUPTOR_ARCHIVE_SCREEN_TOP && y != DISRUPTOR_ARCHIVE_SCREEN_BOTTOM))
            return 0;
        if (z == DISRUPTOR_ARCHIVE_PANEL_LEFT_Z) left = 1;
        else if (z == DISRUPTOR_ARCHIVE_PANEL_RIGHT_Z) right = 1;
        else if (z != DISRUPTOR_ARCHIVE_PANEL_MID_Z) return 0;
    }
    if (left == right) return 0;
    return 2 * (s_archive_frame - 1) + right + 1;
}

static int archive_terminal_bind(void) {
    if (s_archive_load_failed) return 0;
    if (s_archive_atlas) {
        p_glActiveTexture(PSXGL_TEXTURE0 + ARCHIVE_TERMINAL_UNIT);
        glBindTexture(GL_TEXTURE_2D, s_archive_atlas);
        p_glActiveTexture(PSXGL_TEXTURE0);
        return 1;
    }

    int width = 0, height = 0;
    unsigned char *pixels = stbi_load_from_memory(disruptor_archive_atlas, (int)disruptor_archive_atlas_size,
                                                 &width, &height, NULL, 4);
    if (pixels && width == DISRUPTOR_ARCHIVE_FRAME_WIDTH &&
        height == DISRUPTOR_ARCHIVE_FRAME_HEIGHT * DISRUPTOR_ARCHIVE_FRAME_COUNT) {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        if (texture) {
            GLint row_length = 0, unpack_buffer = 0, uploaded_width = 0, uploaded_height = 0;
            glGetIntegerv(PSXGL_UNPACK_ROW_LENGTH, &row_length);
            glGetIntegerv(0x88EF, &unpack_buffer);
            p_glBindBuffer(PSXGL_PIXEL_UNPACK_BUFFER, 0);
            glPixelStorei(PSXGL_UNPACK_ROW_LENGTH, 0);
            p_glActiveTexture(PSXGL_TEXTURE0 + ARCHIVE_TERMINAL_UNIT);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &uploaded_width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &uploaded_height);
            if (uploaded_width == width && uploaded_height == height) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                s_archive_atlas = texture;
            } else glDeleteTextures(1, &texture);
            p_glActiveTexture(PSXGL_TEXTURE0);
            glPixelStorei(PSXGL_UNPACK_ROW_LENGTH, row_length);
            p_glBindBuffer(PSXGL_PIXEL_UNPACK_BUFFER, (GLuint)unpack_buffer);
        }
    }
    stbi_image_free(pixels);
    s_archive_load_failed = s_archive_atlas == 0;
    return !s_archive_load_failed;
}

static void archive_terminal_release(int context_current) {
    if (context_current && s_archive_atlas) glDeleteTextures(1, &s_archive_atlas);
    s_archive_atlas = 0;
    s_archive_frame = 0;
    s_archive_load_failed = 0;
}

#define ARCHIVE_STRING_VALUE(value) #value
#define ARCHIVE_STRING(value) ARCHIVE_STRING_VALUE(value)
#define ARCHIVE_TERMINAL_FRAGMENT \
    "uniform sampler2D u_archive_terminal;\n" \
    "const vec2 archive_lo = vec2(" ARCHIVE_STRING(DISRUPTOR_ARCHIVE_GLASS_LEFT) ", " \
    ARCHIVE_STRING(DISRUPTOR_ARCHIVE_GLASS_TOP) ");\n" \
    "const vec2 archive_hi = vec2(" ARCHIVE_STRING(DISRUPTOR_ARCHIVE_GLASS_RIGHT) ", " \
    ARCHIVE_STRING(DISRUPTOR_ARCHIVE_GLASS_BOTTOM) ");\n" \
    "const float archive_corner = float(" ARCHIVE_STRING(DISRUPTOR_ARCHIVE_GLASS_CORNER) ");\n"
#define ARCHIVE_TERMINAL_SAMPLE \
    "  if (v_material_animation.x < 0.0) {\n" \
    "    int tag = -int(v_material_animation.x) - 1;\n" \
    "    vec2 on_panel = mod(sample_uv, 32.0) + vec2(float(tag & 1) * 31.0, 0.0);\n" \
    "    float corner = (on_panel.y < archive_lo.y + archive_corner || on_panel.y >= archive_hi.y - archive_corner)\n" \
    "        ? archive_corner : 0.0;\n" \
    "    if (on_panel.x >= archive_lo.x + corner && on_panel.x < archive_hi.x - corner &&\n" \
    "        on_panel.y >= archive_lo.y && on_panel.y < archive_hi.y) {\n" \
    "      ivec2 size = textureSize(u_archive_terminal, 0);\n" \
    "      int height = size.y / 8;\n" \
    "      vec2 texel = (on_panel - archive_lo) / (archive_hi - archive_lo);\n" \
    "      ivec2 pixel = min(ivec2(texel * vec2(size.x, height)), ivec2(size.x, height) - 1);\n" \
    "      pixel.y += (tag >> 1) * height;\n" \
    "      rgb = texelFetch(u_archive_terminal, pixel, 0).rgb; stp = 0; return true;\n" \
    "    }\n" \
    "  }\n"
