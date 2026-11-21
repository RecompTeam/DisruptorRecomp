#include "game_host.h"

#if __has_include(<webp/decode.h>)
#include <webp/decode.h>
#else
#include <src/webp/decode.h>
#endif

extern const unsigned char disruptor_sky_rooftops_clouds[];
extern const size_t disruptor_sky_rooftops_clouds_size;
extern const unsigned char disruptor_sky_chemical_factory[];
extern const size_t disruptor_sky_chemical_factory_size;
extern const unsigned char disruptor_sky_triton_night[];
extern const size_t disruptor_sky_triton_night_size;

typedef struct {
    GLuint program, texture, vao;
    GLuint geometry, buffer, mesh_vao;
    GLint transform, projection;
    int area, count, solid_count;
    float outdoor_exposure;
    float ceiling;
    GLint frame, heading, fog, lightning, surface, sky_eye, sky_projection;
    int loaded, ready;
} NightRenderer;

typedef enum { NIGHT_TERRAIN, SKY_CHEMICAL, SKY_ROOFTOPS } NightSky;

#define TRITON_FOG_COLOR "vec3(4.,5.,6.)/31."
#define TRITON_SKY_SHARE \
    "float sky_share(float elevation, float outdoors){" \
    "return smoothstep(mix(-0.02,0.12,outdoors),mix(0.18,0.42,outdoors),elevation);}"

static NightRenderer s_night_game, s_night_between, s_chemical_game, s_chemical_between, s_rooftops_game, s_rooftops_between;

static void night_reset(void) {
    memset(&s_rooftops_game, 0, sizeof(s_rooftops_game));
    memset(&s_rooftops_between, 0, sizeof(s_rooftops_between));
    memset(&s_chemical_game, 0, sizeof(s_chemical_game));
    memset(&s_chemical_between, 0, sizeof(s_chemical_between));
    memset(&s_night_game, 0, sizeof(s_night_game));
    memset(&s_night_between, 0, sizeof(s_night_between));
}

static int32_t chemical_lightning_strength(void) {
    enum { rise_ms = 40, end_ms = 310, interval_ms = 12000, first_ms = 4800 };
    const int phase = (int)((SDL_GetTicks() + interval_ms - first_ms) % interval_ms);
    if (phase < rise_ms) return 1000 * phase / rise_ms;
    return phase < end_ms ? 1000 * (end_ms - phase) / (end_ms - rise_ms) : 0;
}

static NightView night_view(int color, int height, int top) {
    NightView result = {-1, {0}};
    if (color == 0x18A4 && height == 240) result.view = psx_game_night_sky_view(top, result.camera, &result.balcony);
    if (color == 0x1A76 && height == 240) {
        result.view = psx_game_chemical_sky_view(top, result.camera);
        if (result.view >= 0 && result.camera[3] == -1) result.camera[0] = chemical_lightning_strength();
    }
    if (color == 0x49EF && height == 240) result.view = psx_game_rooftops_sky_view(top, result.camera);
    return result;
}

static void night_free_pixels(SDL_Surface *rgb) {
#if defined(PSX_SDL3)
    SDL_DestroySurface(rgb);
#else
    SDL_FreeSurface(rgb);
#endif
}

static SDL_Surface *night_webp(const unsigned char *data, size_t size) {
    int width, height;
    if (!WebPGetInfo(data, size, &width, &height)) return NULL;
#if defined(PSX_SDL3)
    SDL_Surface *rgb = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGB24);
#else
    SDL_Surface *rgb = SDL_CreateRGBSurfaceWithFormat(0, width, height, 24, SDL_PIXELFORMAT_RGB24);
#endif
    if (!rgb) return NULL;
    if (WebPDecodeRGBInto(data, size, rgb->pixels, (size_t)rgb->pitch * height, rgb->pitch)) return rgb;
    night_free_pixels(rgb);
    return NULL;
}

#if defined(DISRUPTOR_DEV_TOOLS)
static SDL_Surface *night_preview_bmp(NightSky sky) {
    const char *base = SDL_GetBasePath();
    char *path = NULL;
    if (base) SDL_asprintf(&path, "%s%s", base, sky == SKY_CHEMICAL ? "chemical-factory-preview.bmp" : "upper-deck-night.bmp");
#if !defined(PSX_SDL3)
    SDL_free((void *)base);
#endif
    SDL_Surface *bmp = path ? SDL_LoadBMP(path) : NULL;
    SDL_free(path);
    if (!bmp) return NULL;
#if defined(PSX_SDL3)
    SDL_Surface *rgb = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGB24);
#else
    SDL_Surface *rgb = SDL_ConvertSurfaceFormat(bmp, SDL_PIXELFORMAT_RGB24, 0);
#endif
    night_free_pixels(bmp);
    return rgb;
}
#endif

static SDL_Surface *night_pixels(NightSky sky) {
    if (sky == SKY_ROOFTOPS) return night_webp(disruptor_sky_rooftops_clouds, disruptor_sky_rooftops_clouds_size);
#if defined(DISRUPTOR_DEV_TOOLS)
    SDL_Surface *preview = night_preview_bmp(sky);
    if (preview) return preview;
#endif
    if (sky == SKY_CHEMICAL) return night_webp(disruptor_sky_chemical_factory, disruptor_sky_chemical_factory_size);
    return night_webp(disruptor_sky_triton_night, disruptor_sky_triton_night_size);
}

static int night_prepare(NightRenderer *night, NightSky sky) {
    if (night->loaded) return night->ready;
    night->loaded = 1;
    SDL_Surface *rgb = night_pixels(sky);
    if (!rgb) return 0;
    const int panorama = sky != NIGHT_TERRAIN;
    const int expected = sky == SKY_ROOFTOPS ||
        (sky == SKY_CHEMICAL ? rgb->w == 2048 && rgb->h == 1536 :
                             (rgb->w == 4608 && rgb->h == 640) || (rgb->w == 1280 && rgb->h == 384));
    if (expected && rgb->pitch == rgb->w * 3) {
        night->program = build_program(
            "#version 330 core\nuniform int surface; uniform vec4 eye, projection;"
            "const vec2 rim[6]=vec2[6](vec2(11776,12032),vec2(11776,11808),vec2(12064,11648),"
            "vec2(12448,11648),vec2(12736,11808),vec2(12736,12032));"
            "const int corner[6]=int[6](0,1,2,2,1,3);"
            "void main(){if(surface==0){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
            "gl_Position=vec4(p*2.-1.,0.,1.);return;} vec3 point;"
            "int side=gl_VertexID/6, k=corner[gl_VertexID%6];vec2 a=rim[side], b=rim[side+1];"
            "vec2 edge=b-a; if(dot(eye.xz-a,vec2(-edge.y,edge.x))<0.){gl_Position=vec4(2.,2.,2.,1.);return;}"
            "point.xz=(k&1)==0?a:b;point.y=k<2?1408.:1664.;vec2 offset=point.xz-eye.xz;float height=point.y-eye.y;"
            "vec2 forward=vec2(cos(eye.w),sin(eye.w)); float z=dot(offset,forward);"
            "float x=dot(offset,vec2(forward.y,-forward.x));"
            "gl_Position=vec4(x*projection.x,z*projection.y-height*projection.z,z-1.,z);}",
            panorama ?
            "#version 330 core\nuniform vec4 frame, camera, fog; uniform sampler2D sky; uniform float lightning; out vec4 frag;"
            "float bolt_segment(vec2 p,vec2 a,vec2 b){vec2 t=b-a;"
            "return length(p-a-t*clamp(dot(p-a,t)/dot(t,t),0.,1.));}"
            "vec3 chemical_lightning(vec3 clouds,float u,float elev){"
            "vec2 d=vec2((fract(u-.904+.5)-.5)*6.2831853,elev-.626);"
            "if(abs(d.x)>=.24 || abs(d.y)>=.19) return clouds;"
            "float glow=1.-smoothstep(.0,1.,length(d/vec2(.24,.19)));"
            "vec2 a=vec2(.030,.046), b=vec2(.009,.013), c=vec2(.015,-.001), e=vec2(-.017,-.028);"
            "float arc=min(bolt_segment(d,a,b),min(bolt_segment(d,b,c),bolt_segment(d,c,e)));"
            "arc=min(arc,bolt_segment(d,e,vec2(-.023,-.051)));"
            "float fork=bolt_segment(d,b,vec2(-.019,-.015));"
            "float core=max(1.-smoothstep(.0006,.0021,arc),.48*(1.-smoothstep(.0004,.0016,fork)));"
            "return clouds+vec3(1.,.86,.60)*lightning*(.19*glow+.55*core+.12*(1.-smoothstep(.0,.014,arc)));}"
            "void main(){vec2 p=gl_FragCoord.xy/frame.w-frame.xy;"
            "float u=camera.x+atan(p.x/camera.y)/6.2831853;"
            "float rise=frame.z-p.y;"
            "float elev=atan(rise/(160.*length(vec2(1.,p.x/camera.y))));"
            "bool storm_sky=fog.w>1. && camera.w==0.;"
            "float cloud_angle=fog.w>1. && !storm_sky ? elev : atan(rise/160.);"
            "float inset=1.5/float(textureSize(sky,0).y);float v=clamp(1.-cloud_angle/1.5707963,inset,1.-inset);"
            "vec3 clouds=texture(sky,vec2(u,(camera.w+v)/fog.w)).rgb;"
            "if(storm_sky && lightning>0.) clouds=chemical_lightning(clouds,u,cloud_angle);"
            "frag=vec4(mix(fog.xyz,clouds,smoothstep(camera.z-0.015,camera.z+0.27,elev)),0.);}" :
            "#version 330 core\nuniform vec4 frame, camera, fog; uniform sampler2D sky; out vec4 frag;"
            TRITON_SKY_SHARE
            "void main(){ vec2 p=gl_FragCoord.xy/frame.w-vec2(frame.x,frame.y);"
            "float up=frame.z-p.y;"
            "float u=camera.x+atan(p.x/camera.y)/6.2831853;"
            "float elev=atan(up/(160.*length(vec2(1.,p.x/camera.y))));"
            "vec3 rgb=texture(sky,vec2(u,clamp(1.-sqrt(max(0.,sin(elev))),0.,1.))).rgb*0.87;"
            "vec3 haze=" TRITON_FOG_COLOR ";"
            "float outdoors=camera.w==0.?fog.w:0.;"
            "float above_fog=sky_share(elev,outdoors);"
            "float exitDepth=max(fog.z,1.)*160./max(up,0.0001);"
            "float visible=above_fog*(1.-smoothstep(fog.x,fog.y,exitDepth));"
            "if(camera.w<0.) visible*=1.+camera.w;"
            "if(camera.w>0.) {float reach=1.-smoothstep(fog.x,fog.y,1./gl_FragCoord.w);"
            "frag=vec4(mix(haze,rgb,above_fog),fog.w*reach);return;}"
            "frag=vec4(mix(haze,rgb,visible),0.); }");
        if (!panorama) night->geometry = build_program(
            "#version 330 core\nlayout(location=0) in vec3 world; layout(location=1) in vec2 uv; layout(location=2) in vec2 material;"
            "uniform vec4 camera, project; out vec2 texel; out vec3 ray; flat out ivec2 tile;"
            "void main(){vec3 p=world-camera.xyz;if(material.y==65535. && dot(-p.xz,uv)>0.){gl_Position=vec4(2.,2.,2.,1.);return;}"
            "vec2 forward=vec2(cos(camera.w),sin(camera.w));"
            "float z=dot(p.xz,forward);float x=dot(p.xz,vec2(forward.y,-forward.x));tile=ivec2(material);ray=p;"
            "texel=tile.y==65535?vec2(tile.x&1,tile.x>>1):uv;"
            "gl_Position=vec4(x*project.x,z*project.y-p.y*project.z,z-1.,z);}",
            "#version 330 core\nuniform vec4 project; uniform usampler2D vram; in vec2 texel; in vec3 ray; flat in ivec2 tile; out vec4 frag;"
            TRITON_SKY_SHARE
            "int word(int x,int y){return int(texelFetch(vram,ivec2(x&1023,y&511),0).r);}"
            "void main(){vec3 haze=" TRITON_FOG_COLOR ";"
            "float open=project.w>0.?project.w*sky_share(atan(ray.y/max(length(ray.xz),0.01)),project.w):0.;"
            "if(tile.y==65535){float clear=max(smoothstep(0.5,1.,texel.y),open);"
            "frag=vec4(haze,(1.-clear)*smoothstep(0.,0.08,min(texel.x,1.-texel.x)));return;}"
            "ivec2 uv=ivec2(floor(texel))&255;int page=tile.y, depth=(page>>7)&3;"
            "int shift=depth==0?2:depth==1?1:0;"
            "int pixel=word((page&15)*64+(uv.x>>shift),((page>>4)&1)*256+uv.y);"
            "if(shift!=0){int bits=16>>shift;int index=(pixel>>((uv.x&((1<<shift)-1))*bits))&((1<<bits)-1);"
            "pixel=word((tile.x&63)*16+index,tile.x>>6);} if(pixel==0)discard;frag=vec4(haze,0.);}");
        if (night->program && (panorama || night->geometry)) {
            if (!panorama) {
                night->surface = p_glGetUniformLocation(night->program, "surface");
                night->sky_eye = p_glGetUniformLocation(night->program, "eye");
                night->sky_projection = p_glGetUniformLocation(night->program, "projection");
                night->transform = p_glGetUniformLocation(night->geometry, "camera");
                night->projection = p_glGetUniformLocation(night->geometry, "project");
                p_glGenVertexArrays(1, &night->mesh_vao);
                p_glGenBuffers(1, &night->buffer);
            }
            night->frame = p_glGetUniformLocation(night->program, "frame");
            night->heading = p_glGetUniformLocation(night->program, "camera");
            night->fog = p_glGetUniformLocation(night->program, "fog");
            if (sky == SKY_CHEMICAL) night->lightning = p_glGetUniformLocation(night->program, "lightning");
            p_glGenVertexArrays(1, &night->vao);
            glGenTextures(1, &night->texture);
            glBindTexture(GL_TEXTURE_2D, night->texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            GLint limit = 0, uploaded = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
            int width = rgb->w, height = rgb->h;
            unsigned char *pixels = (unsigned char *)rgb->pixels;
            while (limit > 0 && (width > limit || height > limit) && width >= 2 && height >= 2) {
                const int half_width = width / 2, half_height = height / 2;
                for (int y = 0; y < half_height; ++y) {
                    for (int x = 0; x < half_width; ++x) {
                        const int src = 3 * (2 * y * width + 2 * x), dst = 3 * (y * half_width + x);
                        for (int c = 0; c < 3; ++c)
                            pixels[dst+c] = (unsigned char)((pixels[src+c] + pixels[src+3+c] +
                                pixels[src+3*width+c] + pixels[src+3*width+3+c] + 2) / 4);
                    }
                }
                width = half_width; height = half_height;
            }
            if (night->texture && width <= limit && height <= limit) {
                GLint alignment, row_length, unpack;
                glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
                glGetIntegerv(PSXGL_UNPACK_ROW_LENGTH, &row_length);
                glGetIntegerv(0x88EF, &unpack);
                p_glBindBuffer(PSXGL_PIXEL_UNPACK_BUFFER, 0);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glPixelStorei(PSXGL_UNPACK_ROW_LENGTH, 0);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &uploaded);
                glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
                glPixelStorei(PSXGL_UNPACK_ROW_LENGTH, row_length);
                p_glBindBuffer(PSXGL_PIXEL_UNPACK_BUFFER, (GLuint)unpack);
                night->ready = uploaded == width;
            }
        }
    }
    night_free_pixels(rgb);
    return night->ready;
}

static void night_draw_blended(int first, int count) {
    enum { equation_rgb = 0x8009, equation_alpha = 0x883D, src_rgb = 0x80C9,
           dst_rgb = 0x80C8, src_alpha = 0x80CB, dst_alpha = 0x80CA };
    GLint eq_rgb, eq_alpha, src, dst, src_a, dst_a;
    glGetIntegerv(equation_rgb, &eq_rgb); glGetIntegerv(equation_alpha, &eq_alpha);
    glGetIntegerv(src_rgb, &src); glGetIntegerv(dst_rgb, &dst);
    glGetIntegerv(src_alpha, &src_a); glGetIntegerv(dst_alpha, &dst_a);
    glEnable(GL_BLEND);
    p_glBlendEquationSeparate(PSXGL_FUNC_ADD, PSXGL_FUNC_ADD);
    p_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glDrawArrays(GL_TRIANGLES, first, count);
    p_glBlendFuncSeparate(src, dst, src_a, dst_a);
    p_glBlendEquationSeparate(eq_rgb, eq_alpha);
    glDisable(GL_BLEND);
}

static void night_draw(NightRenderer *night, NightView current, NightView prior, float alpha, int width, int y, int scale, float focal_x, GLuint vram) {
    night->outdoor_exposure = 0;
    const int view = current.view;
    const NightSky sky = current.camera[3] == PSX_ROOFTOPS_SKY ? SKY_ROOFTOPS :
                         current.camera[3] < 0 ? SKY_CHEMICAL : NIGHT_TERRAIN;
    if (sky == SKY_CHEMICAL) night = night == &s_night_game ? &s_chemical_game : &s_chemical_between;
    if (sky == SKY_ROOFTOPS) night = night == &s_night_game ? &s_rooftops_game : &s_rooftops_between;
    if (view < 0 || (night->loaded && !night->ready)) return;
    GLint texture, active_unit;
    glGetIntegerv(0x84E0, &active_unit);
    p_glActiveTexture(PSXGL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    if (night_prepare(night, sky)) {
        GLint program, vao;
        glGetIntegerv(0x8B8D, &program);
        glGetIntegerv(0x85B5, &vao);
        GLboolean depth = glIsEnabled(GL_DEPTH_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);
        GLboolean blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        p_glUseProgram(night->program);
        p_glBindVertexArray(night->vao);
        glBindTexture(GL_TEXTURE_2D, night->texture);
        if (sky == NIGHT_TERRAIN && (!current.camera[3] || night->area != current.camera[3])) {
            int count = 0;
            const PsxNightVertex *vertices = psx_game_night_geometry(current.camera[3], &count);
            night->solid_count = count;
            while (night->solid_count && vertices[night->solid_count - 1].page == 0xFFFFu) --night->solid_count;
            if (vertices && count > 0) {
                // Above the highest loaded surface a ray can leave the fog before its far plane.
                night->ceiling = vertices[0].position[1];
                for (int i = 1; i < count; ++i)
                    if (vertices[i].position[1] > night->ceiling) night->ceiling = vertices[i].position[1];
                GLint buffer;
                glGetIntegerv(0x8894, &buffer);
                p_glBindVertexArray(night->mesh_vao);
                p_glBindBuffer(PSXGL_ARRAY_BUFFER, night->buffer);
                p_glBufferData(PSXGL_ARRAY_BUFFER, (ptrdiff_t)count * sizeof(*vertices), vertices, 0x88E4);
                p_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(*vertices), (void *)offsetof(PsxNightVertex, position));
                p_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(*vertices), (void *)offsetof(PsxNightVertex, uv));
                p_glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(*vertices), (void *)offsetof(PsxNightVertex, clut));
                p_glEnableVertexAttribArray(0); p_glEnableVertexAttribArray(1); p_glEnableVertexAttribArray(2);
                p_glBindBuffer(PSXGL_ARRAY_BUFFER, (GLuint)buffer);
                p_glBindVertexArray(night->vao);
            }
            night->area = current.camera[3]; night->count = count;
        }
        if (prior.view < 0 || prior.camera[3] != current.camera[3]) prior = current;
        const int previous = prior.view;
        float turn = (float)((view & 255) - (previous & 255)) + current.yaw_fraction - prior.yaw_fraction;
        if (turn >= 128.0f) turn -= 256.0f;
        if (turn < -128.0f) turn += 256.0f;
        const float horizon = prior.horizon + alpha * (current.horizon - prior.horizon);
        const float prev_open = (previous >> 24) / 127.0f, now_open = (view >> 24) / 127.0f;
        const float outside = (1.0f-alpha) * (prior.balcony ? 0.0f : prev_open) +
                              alpha * (current.balcony ? 0.0f : now_open);
        const float windows = (1.0f-alpha) * (prior.balcony ? prev_open : 0.0f) +
                              alpha * (current.balcony ? now_open : 0.0f);
        const int old_basin = (previous >> 16) & 255, new_basin = (view >> 16) & 255;
        const float basin = (old_basin + alpha * (new_basin - old_basin)) / 255.0f;
        night->outdoor_exposure = night->count ? fmaxf(outside, basin) : 0.0f;
        p_glUniform4f(night->frame, width * 0.5f, (float)y, horizon, (float)scale);
        const float heading = (float)(previous & 255) + prior.yaw_fraction + alpha * turn;
        const float layer = sky == SKY_CHEMICAL ? (float)(-current.camera[3] - 1) : sky == SKY_ROOFTOPS ? 0.0f : -windows;
        const float fog_lift = sky == SKY_ROOFTOPS ? 0.25f : 0.0f;
        p_glUniform4f(night->heading, -heading / 256.0f, focal_x, fog_lift, layer);
        if (sky != NIGHT_TERRAIN) {
            if (sky == SKY_CHEMICAL) {
                p_glUniform4f(night->fog, 22.0f / 31.0f, 19.0f / 31.0f, 6.0f / 31.0f, 3.0f);
                const float flash = (prior.camera[0] + alpha * (current.camera[0] - prior.camera[0])) * 0.001f;
                p_glUniform1f(night->lightning, flash);
            } else p_glUniform4f(night->fog, 15.0f / 31.0f, 15.0f / 31.0f, 18.0f / 31.0f, 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        if (night->count > 0) {
            float eye[3];
            for (int i=0; i<3; ++i) eye[i]=(float)prior.camera[i]+alpha*((float)current.camera[i]-(float)prior.camera[i]);
            const float start = prior.fog_start + alpha * (current.fog_start - prior.fog_start);
            const float end = prior.fog_end + alpha * (current.fog_end - prior.fog_end);
            p_glUniform4f(night->fog, start, end, night->ceiling - eye[1], outside);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            const float yaw = heading * (6.28318530718f / 256.0f);
            if (windows > 0.0f) {
                p_glUniform4f(night->sky_eye, eye[0], eye[1], eye[2], yaw);
                p_glUniform4f(night->sky_projection, 2.0f * focal_x / width,
                              2.0f * (y + horizon) / VRAM_H - 1.0f, 320.0f / VRAM_H, 0.0f);
                p_glUniform1i(night->surface, 1);
                p_glUniform4f(night->heading, -heading / 256.0f, focal_x, 0.0f, 1.0f);
                p_glUniform4f(night->fog, start, end, night->ceiling - eye[1], windows);
                night_draw_blended(0, 30);
                p_glUniform1i(night->surface, 0);
            }
            if (vram && (windows > 0.0f || outside > 0.0f || horizon * end > fmaxf(night->ceiling - eye[1], 1.0f) * 160.0f)) {
                p_glUseProgram(night->geometry); p_glBindVertexArray(night->mesh_vao);
                glBindTexture(GL_TEXTURE_2D, vram);
                p_glUniform4f(night->transform, eye[0], eye[1], eye[2], yaw);
                p_glUniform4f(night->projection, 2.0f * focal_x / width,
                              2.0f * (y + horizon) / VRAM_H - 1.0f, 320.0f / VRAM_H, night->outdoor_exposure);
                glDrawArrays(GL_TRIANGLES, 0, night->solid_count);
                if (night->solid_count < night->count)
                    night_draw_blended(night->solid_count, night->count - night->solid_count);
            }
        }
        p_glBindVertexArray((GLuint)vao); p_glUseProgram((GLuint)program);
        if (depth) glEnable(GL_DEPTH_TEST);
        if (stencil) glEnable(GL_STENCIL_TEST);
        if (blend) glEnable(GL_BLEND);
        if (cull) glEnable(GL_CULL_FACE);
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    p_glActiveTexture((GLenum)active_unit);
}

#undef TRITON_FOG_COLOR
#undef TRITON_SKY_SHARE
