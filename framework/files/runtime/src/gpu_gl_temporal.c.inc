/* Geometry inter-frame presentation. Included by gpu_gl_renderer.c so the
 * capture sites use the exact same shader inputs as the live mirror. Guest
 * VRAM is never a replay destination. The presenter owns its own GL objects;
 * published scenes and texture snapshots are immutable until its fence retires.
 */
#include "gpu_temporal_mesh.h"

#define TEMP_BUILD_SLOTS (WIDE_MAX_SURF * 2)
#define TEMP_MAX_VERTS 24576
#define TEMP_MAX_COMMANDS 2048
#define TEMP_MAX_TEXTURES 16
typedef struct {
    int first, count, first_tri, textured, mode, semi, mask, check, filter;
    int twin[4], sy, sh, texture;
    int filler;  /* holds grown copies of the game's: they go under it, see s_crack_fill */
    int world;   /* holds only exact world polygons: drawn by depth when s_world_depth is on */
} TemporalCommand;
typedef struct {
    GLuint capture_fbo;
    float *vertices;
    GpuTemporalTriangle *triangles;
    GpuTemporalTriangle *from;
    GpuTemporalTriangle *start, *gone_to; /* the two ends of a departed face's path, see temporal_underlay */
    int *gone;
    GpuTemporalMeshStats mesh_stats;
    TemporalCommand *commands;
    int *matches;
    int nv, nt, nc, active, bad, clear_y, clear_h, width, height, scale;
    int origin_y, texture_count, matched, world, moving;
    float clear[4], tint;
    GLuint textures[TEMP_MAX_TEXTURES];
    uint64_t texture_generation[TEMP_MAX_TEXTURES];
} TemporalScene;
static TemporalScene s_temp_build[TEMP_BUILD_SLOTS], s_temp_prev, s_temp_curr;
static GpuTemporalCamera s_temp_camera; /* the last interval's camera, a hypothesis for the next */
static SDL_atomic_t s_temp_worker_ready;
static uint64_t s_temp_published, s_temp_replays, s_temp_fallbacks;
static int s_temp_verify_left;
static int s_temp_last_matched, s_temp_last_world, s_temp_last_ready;
static GLuint s_temp_geo_prog, s_temp_tex_prog, s_temp_present_prog;
static GLuint s_temp_vao[2], s_temp_vbo, s_temp_fbo, s_temp_tex, s_temp_rb;
static int s_temp_width, s_temp_scale;
static int s_temp_underlaying;  /* departed faces are being drawn: they keep the depth rule of their own pass */
static int s_temp_marked;       /* the textured program is the one that tells fillers apart */
static int s_temp_part;         /* what temporal_submit draws of a command: 0 all of it, 1 its fillers alone, 2 its polygons alone */
static GLint s_temp_uPull=-1;
static int s_temp_fill;         /* departed faces are leaving their depth where the scene left none, see temporal_submit */
static float s_temp_scratch[TEXBATCH_MAXV * TEXV], s_temp_under[TEXBATCH_MAXV * TEXV];
typedef struct {
    GLint shift, xhalf, visual, options, twin, mask, filter, pass, semi;
} TemporalUniforms;
static TemporalUniforms s_temp_uniforms[2];

#ifndef PSX_NO_DEBUG_TOOLS
static int s_temp_edge_diag = -1;
/* Clear-colour pixels in one column of the bound read framebuffer. */
static int temporal_edge_gap(int column,int y,int rows,const float *clear) {
    static uint8_t pixels[VRAM_H*16*4];
    int gap=0;
    if (rows>VRAM_H*16) rows=VRAM_H*16;
    glReadPixels(column,y,1,rows,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for (int i=0;i<rows;++i) {
        int same=1;
        for (int k=0;k<3;++k)
            if (abs((int)pixels[i*4+k]-(int)(clear[k]*255.0f+0.5f))>2) same=0;
        gap+=same;
    }
    return gap;
}
/* Writes a run of real and presented in-between frames once the camera
 * turns fast. A negative alpha marks the real frame just captured. */
static int s_temp_dump_left=-1, s_temp_dump_index;
/* PSX_GL_TEMPORAL_FRAME_DUMP_AT pins the in-betweens of a dump to one alpha,
 * one per real frame, so that two builds dump the same pictures. Not with
 * PSX_GL_TEMPORAL_VERIFY: its alpha 1 picture would take a frame's place. */
static float temporal_dump_pin(void) {
    static float pin=-2.0f;
    if (pin<-1.5f) {
        const char *at=getenv("PSX_GL_TEMPORAL_FRAME_DUMP_AT");
        pin=at && at[0] ? (float)atof(at) : -1.0f;
    }
    return pin;
}
static float temporal_dump_alpha(float alpha) {
    return temporal_dump_pin()>=0.0f && s_temp_dump_left>0 && s_temp_dump_index ? temporal_dump_pin() : alpha;
}
static void temporal_dump_frame(int x,int y,int width,int height,float alpha,int departed) {
    static unsigned long long dumped=~0ull;
    const char *dir=getenv("PSX_GL_TEMPORAL_FRAME_DUMP");
    char path[512];
    uint8_t *pixels;
    FILE *file;
    static int threshold=-1, held=0;
    if (threshold<0) {
        const char *least=getenv("PSX_GL_TEMPORAL_FRAME_DUMP_DEPARTED"), *wait=getenv("PSX_GL_TEMPORAL_FRAME_DUMP_WAIT");
        threshold=least && least[0] ? atoi(least) : 300;
        held=wait && wait[0]=='1';
    }
    if (s_temp_dump_left<0) s_temp_dump_left=dir && dir[0] ? 48 : 0;
    /* PSX_GL_TEMPORAL_FRAME_DUMP_WAIT=1 holds the run back until a file named go appears in the directory. */
    if (held && s_temp_dump_left && !s_temp_dump_index && alpha<0.0f) {
        snprintf(path,sizeof(path),"%s/go",dir);
        file=fopen(path,"rb");
        if (file) { fclose(file); held=0; }
    }
    if (!s_temp_dump_left || (!s_temp_dump_index && (alpha>=0.0f || departed<threshold || held))) return;
    if (alpha>=0.0f && temporal_dump_pin()>=0.0f) {
        if (dumped==s_temp_published) return;
        dumped=s_temp_published;
    }
    pixels=(uint8_t *)malloc((size_t)width*height*3);
    if (!pixels) return;
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(x,y,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels);
    glPixelStorei(GL_PACK_ALIGNMENT,4);
    if (alpha<0.0f)
        snprintf(path,sizeof(path),"%s/real_%llu.ppm",dir,(unsigned long long)s_temp_published);
    else
    snprintf(path,sizeof(path),"%s/frame_%03d_%llu_%03d.ppm",dir,s_temp_dump_index,
             (unsigned long long)s_temp_published,(int)(alpha*100.0f));
    ++s_temp_dump_index;
    file=fopen(path,"wb");
    if (file) {
        fprintf(file,"P6\n%d %d\n255\n",width,height);
        fwrite(pixels,1,(size_t)width*height*3,file);
        fclose(file);
    }
    free(pixels);
    --s_temp_dump_left;
}
/* Keeps the first few real frames that reached the screen with an edge gap. */
static void temporal_dump_edge(int x,int y,int width,int height) {
    static int written;
    const char *dir=getenv("PSX_GL_TEMPORAL_EDGE_DUMP");
    char path[512];
    uint8_t *pixels;
    FILE *file;
    if (!dir || !dir[0] || written>=8) return;
    pixels=(uint8_t *)malloc((size_t)width*height*3);
    if (!pixels) return;
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(x,y,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels);
    glPixelStorei(GL_PACK_ALIGNMENT,4);
    snprintf(path,sizeof(path),"%s/edge_%llu.ppm",dir,(unsigned long long)s_temp_published);
    file=fopen(path,"wb");
    if (file) {
        fprintf(file,"P6\n%d %d\n255\n",width,height);
        fwrite(pixels,1,(size_t)width*height*3,file);
        fclose(file);
    }
    free(pixels);
    ++written;
}
static int temporal_edge_diag(void) {
    if (s_temp_edge_diag<0) {
        const char *e=getenv("PSX_GL_TEMPORAL_EDGE_DIAG");
        s_temp_edge_diag=e && e[0]=='1';
    }
    return s_temp_edge_diag;
}
#endif

static int temporal_recording(void) {
    return s_interp_enabled && s_interp_blend_mode == 2 &&
           !s_interp_suspended && s_geometry_correction && !s_ws_ablate;
}
static TemporalScene *temporal_builder(GLuint fbo, int y, int h) {
    for (int i=0;i<TEMP_BUILD_SLOTS;++i) {
        TemporalScene *s=&s_temp_build[i];
        if (fbo && s->capture_fbo==fbo && s->active &&
            s->clear_y<=y && s->clear_y+s->clear_h>=y+h) return s;
    }
    return NULL;
}
static void temporal_reset(void) {
    for (int i = 0; i < TEMP_BUILD_SLOTS; ++i) { s_temp_build[i].active = 0; s_temp_build[i].capture_fbo=0; }
    s_temp_prev.active = s_temp_curr.active = 0;
}
static int temporal_allocate(TemporalScene *s) {
    if (!s->vertices) s->vertices = (float *)malloc(TEMP_MAX_VERTS * TEXV * sizeof(float));
    if (!s->triangles) s->triangles = (GpuTemporalTriangle *)malloc((TEMP_MAX_VERTS/3) * sizeof(GpuTemporalTriangle));
    if (!s->from) s->from = (GpuTemporalTriangle *)malloc((TEMP_MAX_VERTS/3) * sizeof(GpuTemporalTriangle));
    if (!s->start) s->start = (GpuTemporalTriangle *)malloc((TEMP_MAX_VERTS/3) * sizeof(GpuTemporalTriangle));
    if (!s->gone_to) s->gone_to = (GpuTemporalTriangle *)malloc((TEMP_MAX_VERTS/3) * sizeof(GpuTemporalTriangle));
    if (!s->gone) s->gone = (int *)malloc((TEMP_MAX_VERTS/3) * sizeof(int));
    if (!s->commands) s->commands = (TemporalCommand *)malloc(TEMP_MAX_COMMANDS * sizeof(TemporalCommand));
    if (!s->matches) s->matches = (int *)malloc((TEMP_MAX_VERTS/3) * sizeof(int));
    return s->vertices && s->triangles && s->from && s->start && s->gone_to && s->gone &&
           s->commands && s->matches;
}
static void temporal_begin(GLuint fbo, int y, int h, uint16_t color) {
    TemporalScene *s=NULL;
    for (int i=0;i<TEMP_BUILD_SLOTS;++i)
        if (s_temp_build[i].capture_fbo==fbo && s_temp_build[i].clear_y==y) { s=&s_temp_build[i]; break; }
    if (!s) for (int i=0;i<TEMP_BUILD_SLOTS;++i)
        if (!s_temp_build[i].active) { s=&s_temp_build[i]; break; }
    if (!s) return;
    for (int i=0;i<TEMP_BUILD_SLOTS;++i) {
        TemporalScene *other=&s_temp_build[i];
        if (other!=s && other->capture_fbo==fbo && other->active &&
            other->clear_y<y+h && y<other->clear_y+other->clear_h)
            other->active=0;
    }
    s->capture_fbo=fbo;
    s->active = 0;
    if (!temporal_recording() || !temporal_allocate(s)) return;
    s->active = 1; s->bad = 0; s->nv = s->nt = s->nc = 0;
    s->texture_count = 0; s->clear_y = y; s->clear_h = h;
    s->width = g_wide_w; s->scale = s_scale;
    s->clear[0] = (color & 31) / 31.0f;
    s->clear[1] = ((color >> 5) & 31) / 31.0f;
    s->clear[2] = ((color >> 10) & 31) / 31.0f;
    s->clear[3] = (color & 0x8000) ? 1.0f : 0.0f;
    s->tint = s_geometry_coverage_tint ? 0.55f : 0.0f;
}
static void temporal_invalidate_surface(GLuint fbo) {
    for (int i=0;i<TEMP_BUILD_SLOTS;++i)
        if (s_temp_build[i].capture_fbo==fbo) s_temp_build[i].bad=4;
}

/* Snapshot every distinct texture generation used by this scene. This also
 * handles texture uploads between draws, without sampling mutable VRAM from
 * the other context. A bounded overflow holds the real frame. */
static int temporal_texture(TemporalScene *s) {
    for (int i = 0; i < s->texture_count; ++i)
        if (s->texture_generation[i] == s_temporal_raw_generation) return i;
    if (s->texture_count == TEMP_MAX_TEXTURES) { s->bad = 3; return -1; }
    int i = s->texture_count++;
    GLint read_fbo = 0, texture = 0;
    glGetIntegerv(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &read_fbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    if (!s->textures[i])
        s->textures[i] = make_tex(PSXGL_R16UI, VRAM_W, VRAM_H,
                                  PSXGL_RED_INTEGER, GL_UNSIGNED_SHORT);
    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER, s_raw_fbo);
    glBindTexture(GL_TEXTURE_2D, s->textures[i]);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, VRAM_W, VRAM_H);
    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER, (GLuint)read_fbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    s->texture_generation[i] = s_temporal_raw_generation;
    return i;
}
static uint64_t temporal_hash(uint64_t h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= UINT64_C(1099511628211); }
    return h;
}

/* Bake the mirror's projection into local screen coordinates. Both the
 * canonical and precise slots get the baked value; texture perspective and
 * coverage flags remain exactly those selected for the original draw. */
static void temporal_position(float *v, int textured, int gate, int direct) {
    int flags_at = textured ? 19 : 6, pos_at = textured ? 20 : 7;
    int corrected = !direct && s_geometry_correction && v[flags_at] > 0.5f;
    float x = corrected ? v[pos_at] : v[0], y = corrected ? v[pos_at+1] : v[1];
    if (corrected && fabsf(s_yaw_sin) > 0.0000001f && s_yaw_focal_x > 0) {
        float cx = g_wide_cur_base + s_yaw_center_x;
        float cy = s_off_y + s_yaw_center_y_relative;
        float ux = (x - cx) / s_yaw_focal_x;
        float den = s_yaw_cos - ux * s_yaw_sin;
        if (fabsf(den) > 0.01f) {
            x = cx + s_yaw_focal_x * (ux*s_yaw_cos+s_yaw_sin) / den;
            if (s_geometry_full_yaw) y = cy + (y-cy)/den;
        }
    }
    if (textured && (!corrected || v[flags_at] <= 1.5f))
        v[22] = 0;
    if (!direct) {
        if (gate && g_ws_bd_stretch_on) {
            extern int g_ws_tex_edge_pct;
            int nw = g_wide_w - 2*g_wide_off;
            if (nw > 0) {
                float factor = g_ws_bd_stretch_pct > 0 ? g_ws_bd_stretch_pct/100.0f : (float)g_wide_w/nw;
                float cx = g_wide_cur_base + nw/2.0f;
                if (gate == 2) {
                    factor = g_ws_tex_edge_pct > 0 ? g_ws_tex_edge_pct/100.0f : factor;
                    float half = (g_wide_w/2.0f)/factor;
                    if (x < cx-half) x = cx-half+(x-(cx-half))*factor;
                    else if (x > cx+half) x = cx+half+(x-(cx+half))*factor;
                } else x = (x-cx)*factor+cx;
            }
        }
        x += wide_dx();
    }
    v[0] = v[pos_at] = x; v[1] = v[pos_at+1] = y;
}
static void temporal_draw(const float *vertices, const uint64_t *identities, int n,
                          int textured, GLenum mode, int semi, int mask, int filter,
                          const int *twin, int gate, int direct) {
    if (!temporal_recording()) return;
    TemporalScene *s = temporal_builder(g_wide_cur, s_area_y1, s_area_y2-s_area_y1+1);
    if (!s) {
        /* An unretained draw touching a captured band makes that band unsafe.
         * Do not publish an incomplete command list as a valid scene. */
        for (int i=0;i<TEMP_BUILD_SLOTS;++i) {
            TemporalScene *other=&s_temp_build[i];
            if (other->active && other->capture_fbo==g_wide_cur &&
                other->clear_y<=s_area_y2 && s_area_y1<other->clear_y+other->clear_h)
                other->bad=5;
        }
        return;
    }
    if (!s->active || s->bad) return;
    /* Deferred destination-mask reconstruction is not replayed yet. Reject
     * the entire scene rather than inventing stencil state between batches. */
    if (s_mask_check) { s->bad=1; return; }
    if (s->nc == TEMP_MAX_COMMANDS || s->nv+n > TEMP_MAX_VERTS ||
        n > TEXBATCH_MAXV || (mode != GL_TRIANGLES && mode != GL_LINES)) { s->bad=2; return; }
    TemporalCommand *c = &s->commands[s->nc++];
    memset(c, 0, sizeof(*c));
    c->first=s->nv; c->count=n; c->first_tri=s->nt;
    c->textured=textured; c->mode=(int)mode; c->semi=semi;
    c->mask=mask; c->check=s_mask_check; c->filter=filter;
    c->filler=textured && s_draw_filler;
    c->world=textured && s_draw_world;
    c->sy=direct ? 0 : s_area_y1; c->sh=direct ? VRAM_H : s_area_y2-s_area_y1+1;
    if (twin) memcpy(c->twin, twin, sizeof(c->twin));
    c->texture=textured ? temporal_texture(s) : -1;
    if (s->bad) return;
    const int stride = textured ? TEXV : GEOV;
    for (int i=0; i<n; ++i) {
        float *v = &s->vertices[(s->nv+i)*TEXV];
        memset(v, 0, TEXV*sizeof(float));
        memcpy(v, vertices+i*stride, stride*sizeof(float));
        temporal_position(v, textured, gate, direct);
    }
    if (mode == GL_TRIANGLES) for (int i=0; i<n; i+=3) {
        GpuTemporalTriangle *t=&s->triangles[s->nt++];
        memset(t,0,sizeof(*t));
        uint64_t hash=UINT64_C(14695981039346656037);
        hash=temporal_hash(hash,&textured,sizeof(textured));
        hash=temporal_hash(hash,&c->semi,sizeof(c->semi));
        hash=temporal_hash(hash,&c->mask,sizeof(c->mask));
        hash=temporal_hash(hash,&c->filter,sizeof(c->filter));
        hash=temporal_hash(hash,c->twin,sizeof(c->twin));
        const int sprite = identities && identities[i]==TEMPORAL_SPRITE_IDENTITY &&
            identities[i+1]==TEMPORAL_SPRITE_IDENTITY && identities[i+2]==TEMPORAL_SPRITE_IDENTITY;
        const int partial = identities && identities[i]==TEMPORAL_PARTIAL_IDENTITY &&
            identities[i+1]==TEMPORAL_PARTIAL_IDENTITY && identities[i+2]==TEMPORAL_PARTIAL_IDENTITY;
        t->world = 1;
        t->sprite = sprite;
        t->modelled = !sprite && !partial && identities && identities[i] && identities[i+1] && identities[i+2];
        for (int j=0; t->modelled && j<3; ++j)
            for (int axis=0; axis<3; ++axis)
                t->model[j][axis]=(int16_t)(identities[i+j]>>(16*axis));
        for (int j=0; j<3; ++j) {
            float *v=&s->vertices[(s->nv+i+j)*TEXV];
            t->x[j]=v[0]; t->y[j]=v[1]; t->q[j]=textured ? v[22] : 0;
            t->depth[j]=v[textured ? 23 : 9]>0.0f ? v[textured ? 23 : 9] : 0.0f;  /* negative: the display mirror's own, see take_visual_triangle */
            if (v[textured ? 19 : 6] <= 0.5f && !sprite && !partial) t->world=0;
            if (textured) {
                hash=temporal_hash(hash,v+2,2*sizeof(float));
                hash=temporal_hash(hash,v+8,11*sizeof(float));
            } else hash=temporal_hash(hash,v+2,4*sizeof(float));
        }
        t->material=hash;
    }
    s->nv+=n;
}

/* Optional single moving-scene capture for checking actual game topology.
 * Absent from normal builds/play; no readback or file I/O unless requested. */
#ifndef PSX_NO_DEBUG_TOOLS
static void temporal_dump_mesh(void) {
    static int written, seen, limit=-1, stride=1, held;
    static char numbered[600];
    const char *path;
    FILE *file;
    int moved=0;
    if (limit<0) {
        const char *count=getenv("PSX_GL_TEMPORAL_MESH_DUMP_COUNT");
        const char *every=getenv("PSX_GL_TEMPORAL_MESH_DUMP_EVERY");
        limit=count && atoi(count)>0 ? atoi(count) : 1;
        stride=every && atoi(every)>0 ? atoi(every) : 1;
        every=getenv("PSX_GL_TEMPORAL_MESH_DUMP_WAIT");
        held=every && every[0]=='1';
    }
    if (written>=limit || s_temp_curr.world<50 || !s_temp_curr.moving) return;
    path=getenv("PSX_GL_TEMPORAL_MESH_DUMP");
    if (!path || !path[0]) return;
    /* PSX_GL_TEMPORAL_MESH_DUMP_WAIT=1 holds the dumps back until the file <path>.go appears. */
    if (held) {
        snprintf(numbered,sizeof(numbered),"%s.go",path);
        file=fopen(numbered,"rb");
        if (!file) return;
        fclose(file);
        held=0;
    }
    for (int i=0;i<s_temp_curr.nt && !moved;++i)
        for (int j=0;j<3;++j)
            if (fabsf(s_temp_curr.from[i].x[j]-s_temp_curr.triangles[i].x[j])>1 ||
                fabsf(s_temp_curr.from[i].y[j]-s_temp_curr.triangles[i].y[j])>1) moved=1;
    if (!moved || (seen++ % stride)) return;
    if (limit>1) {
        snprintf(numbered,sizeof(numbered),"%s.%02d.json",path,written);
        path=numbered;
    }
    ++written;
    file=fopen(path,"wb");
    if (!file) return;
    fprintf(file,"{\"matches\":[");
    for (int i=0;i<s_temp_curr.nt;++i) fprintf(file,"%s%d",i?",":"",s_temp_curr.matches[i]);
    fprintf(file,"]");
    for (int set=0;set<3;++set) {
        const GpuTemporalTriangle *tri=set==0?s_temp_prev.triangles:set==1?s_temp_curr.triangles:s_temp_curr.from;
        int n=set==0?s_temp_prev.nt:s_temp_curr.nt;
        fprintf(file,",\"%s\":[",set==0?"previous":set==1?"current":"from");
        for (int i=0;i<n;++i) {
            fprintf(file,"%s{\"world\":%d,\"material\":\"%016llx\",\"vertices\":[",i?",":"",
                    tri[i].world,(unsigned long long)tri[i].material);
            for (int j=0;j<3;++j)
                fprintf(file,"%s[%.9g,%.9g,%.9g,%.9g,%d,%d,%d,%d,%.9g]",j?",":"",
                        tri[i].x[j],tri[i].y[j],tri[i].depth[j],tri[i].q[j],tri[i].modelled,
                        tri[i].model[j][0],tri[i].model[j][1],tri[i].model[j][2],tri[i].w[j]);
            fprintf(file,"]}");
        }
        fprintf(file,"]");
    }
    fprintf(file,"}\n");
    fclose(file);
    fprintf(stdout,"psxrecomp: GL geometry moving mesh captured\n");
}
#endif

/* Everything about the scene being captured that needs no lock, 1.7 ms of
 * matching above all: the presenter reads neither the builder's arrays nor
 * the current scene's departure arrays. Game thread only. */
static TemporalScene *s_temp_ready;
static void temporal_prepare(GLuint fbo, int y, int w, int h) {
    TemporalScene *s=temporal_builder(fbo,y,h);
    s_temp_ready=NULL;
    if (!temporal_recording() || !s || !s->active || s->bad ||
        s->clear_y > y || s->clear_y+s->clear_h < y+h ||
        s->width != w || s->scale != s_scale || !s->nc) return;
    s->origin_y=y; s->height=h; s->matched=s->world=s->moving=0;
    memset(&s->mesh_stats,0,sizeof(s->mesh_stats));
    for (int i=0; i<s->nc; ++i) {
        TemporalCommand *c=&s->commands[i];
        c->sy-=y;
        for (int j=0; j<c->count; ++j) {
            float *v=&s->vertices[(c->first+j)*TEXV];
            v[1]-=y; v[c->textured ? 21 : 8]-=y;
        }
    }
    for (int i=0; i<s->nt; ++i) {
        for (int j=0; j<3; ++j) s->triangles[i].y[j]-=y;
        s->matches[i]=-1;
        if (s->triangles[i].world) ++s->world;
    }
    memcpy(s->from,s->triangles,s->nt*sizeof(*s->from));
    if (s_temp_curr.active && s_temp_curr.width==w && s_temp_curr.height==h &&
        s_temp_curr.scale==s_scale && s_temp_curr.tint==s->tint) {
        gpu_temporal_match(s_temp_curr.triangles,s_temp_curr.nt,s->triangles,s->nt,s->matches);
        for (int i=0; i<s->nt; ++i)
            if (s->matches[i]>=0) ++s->matched;
        s->moving=gpu_temporal_mesh_prepare_scene(
            s_temp_curr.triangles,s_temp_curr.nt,s->triangles,s->nt,s->matches,s->from,
            &s->mesh_stats,s_temp_curr.start,s_temp_curr.gone_to,s_temp_curr.gone,
            &s_temp_camera);
    } else {
        s_temp_camera.inliers=0;
    }
    s_temp_ready=s;
}

/* Called with the present mutex locked, after the worker's last draw fence.
 * Rotate allocations rather than copying scene-sized buffers. */
static void temporal_publish(GLuint fbo, int y, int h) {
    TemporalScene *s=s_temp_ready;
    s_temp_ready=NULL;
    if (!s || !s->active) {
        s=temporal_builder(fbo,y,h);
        if (s_interp_diag && s_temp_fallbacks<12)
            fprintf(stdout,"psxrecomp: GL geometry fallback: builder=%d active=%d reason=%d commands=%d clear=%d+%d display=%d+%d\n",
                    s!=NULL,s?s->active:0,s?s->bad:0,s?s->nc:0,s?s->clear_y:0,s?s->clear_h:0,y,h);
        s_temp_prev.active=s_temp_curr.active=0;
        if (s) s->active=0;
        s_temp_last_matched=s_temp_last_world=s_temp_last_ready=0;
        ++s_temp_fallbacks;
        return;
    }
    TemporalScene old=s_temp_prev;
    s_temp_prev=s_temp_curr; s_temp_curr=*s; *s=old; s->active=0;
    s->capture_fbo=fbo; s->clear_y=s_temp_curr.clear_y;
    s_temp_last_matched=s_temp_curr.matched;
    s_temp_last_world=s_temp_curr.world;
    s_temp_last_ready=s_temp_curr.moving>0;
#ifndef PSX_NO_DEBUG_TOOLS
    temporal_dump_mesh();
#endif
    ++s_temp_published;
}

static void temporal_uniforms_init(GLuint p, TemporalUniforms *u) {
#define TEMP_U(field,name) u->field=p_glGetUniformLocation(p,name)
    TEMP_U(shift,"u_shift"); TEMP_U(xhalf,"u_xhalf"); TEMP_U(visual,"u_visual");
    TEMP_U(options,"u_visual_options"); TEMP_U(twin,"u_twin"); TEMP_U(mask,"u_maskset");
    TEMP_U(filter,"u_filter"); TEMP_U(pass,"u_semipass"); TEMP_U(semi,"u_semimode");
#undef TEMP_U
    p_glUseProgram(p);
    p_glUniform1f(p_glGetUniformLocation(p,"u_xscale"),1);
    p_glUniform4f(p_glGetUniformLocation(p,"u_camera"),0,1,0,0);
    p_glUniform1i(p_glGetUniformLocation(p,"u_vram"),0);
}
/* The presenter's own copy of a vertex shader: one more attribute carries
 * the depth order. The canonical shaders stay exactly as they are. */
static char *temporal_ordered_shader(const char *source,int location) {
    static const char flat[]="0.0, 1.0); }\n", ordered[]="a_order, 1.0); }\n";
    const size_t length=strlen(source), tail=sizeof(flat)-1;
    const char *body=strchr(source,'\n');
    char *out;
    int written;
    if (!body || length<tail || strcmp(source+length-tail,flat)) return NULL;
    out=(char *)malloc(length+96);
    if (!out) return NULL;
    written=snprintf(out,length+96,"%.*slayout(location=%d) in float a_order;\n",
                     (int)(body+1-source),source,location);
    memcpy(out+written,body+1,(size_t)(source+length-tail-(body+1)));
    strcpy(out+written+(source+length-tail-(body+1)),ordered);
    return out;
}
static int temporal_worker_init(void) {
    const char *verify=getenv("PSX_GL_TEMPORAL_VERIFY");
    char *geo_vs=temporal_ordered_shader(GEO_VS,4), *tex_vs=temporal_ordered_shader(TEX_VS,12);
    s_temp_verify_left=verify && verify[0]=='1' ? 3 : 0;
    s_temp_geo_prog=geo_vs ? build_program(geo_vs,GEO_FS) : 0;
    {   /* As the frame loop's programs: what a fragment is goes into the alpha for the shading to tell sprites and blends by. */
        char *marked_vs=tex_vs ? shader_variant(tex_vs,"flat out float v_bill; noperspective out float v_deep; uniform float u_pull;\n","a_order, 1.0); }\n",
            "u_visual_options.w >= 1.0 ? 0.99999 : ((u_visual_options.w > 0.0 && a_order > -0.9999 && a_order < 0.9999) ? "
            "1.0 - (1.0 - a_order)/(1.0 + u_visual_options.w) : (u_pull > 0.0 && a_order > -0.99999 && a_order < 0.99999) ? "
            "max(1.0 - (1.0 - a_order)*(1.0 + u_pull), -1.0) : a_order), 1.0);\n"
            "  bool grown = a_visual_flags > 5.5;\n"
            "  if (u_visual_options.w == 0.0 ? grown : u_visual_options.w >= 1.0 ? (grown || !(a_visual_flags > 1.5 && a_order > -0.9999 && a_order < 0.9999))\n"
            "      : (u_visual_options.w > 0.0 && !grown)) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
            "  v_deep = (a_order > -0.9999 && a_order < 0.9999) ? 1.0 : -1000.0;\n"
            "  v_bill = (a_visual_flags < 1.5 && a_order > -0.9999 && a_order < 0.9999) ? 2.0 : 1.0; }\n") : NULL;
        char *marked_fs=marked_vs ? sprite_marking_shader(TEX_FS) : NULL;
        s_temp_tex_prog=tex_vs ? build_program_ex(marked_fs ? marked_vs : tex_vs,marked_fs ? marked_fs : TEX_FS,1) : 0;
        s_temp_marked=marked_fs!=NULL;
        free(marked_vs); free(marked_fs);
    }
    free(geo_vs); free(tex_vs);
    s_temp_present_prog=build_program(PRESENT_VS,PRESENT_FS);
    if (!s_temp_geo_prog || !s_temp_tex_prog || !s_temp_present_prog) return 0;
    temporal_uniforms_init(s_temp_geo_prog,&s_temp_uniforms[0]);
    temporal_uniforms_init(s_temp_tex_prog,&s_temp_uniforms[1]);
    s_temp_uPull=p_glGetUniformLocation(s_temp_tex_prog,"u_pull");
    p_glGenBuffers(1,&s_temp_vbo);
    p_glGenVertexArrays(2,s_temp_vao);
    const int sizes[13]={2,2,4,2,2,1,1,4,1,1,2,1,1};
    const int offsets[13]={0,2,4,8,10,12,13,14,18,19,20,22,23};
    for (int t=0;t<2;++t) {
        p_glBindVertexArray(s_temp_vao[t]);
        p_glBindBuffer(PSXGL_ARRAY_BUFFER,s_temp_vbo);
        for (int a=0;a<(t?13:5);++a) {
            const int gs[5]={2,4,1,2,1}, go[5]={0,2,6,7,9};
            p_glVertexAttribPointer(a,t?sizes[a]:gs[a],GL_FLOAT,GL_FALSE,
                TEXV*sizeof(float),(void *)(size_t)((t?offsets[a]:go[a])*sizeof(float)));
            p_glEnableVertexAttribArray(a);
        }
    }
    p_glBindVertexArray(0); p_glUseProgram(0);
    return 1;
}
static int temporal_target(int width,int scale) {
    if (s_temp_fbo && s_temp_width==width && s_temp_scale==scale) return 1;
    if (s_temp_fbo) p_glDeleteFramebuffers(1,&s_temp_fbo);
    if (s_temp_tex) glDeleteTextures(1,&s_temp_tex);
    if (s_temp_rb) p_glDeleteRenderbuffers(1,&s_temp_rb);
    s_temp_fbo=s_temp_tex=s_temp_rb=0;
    s_temp_tex=make_tex(GL_RGBA8,width*scale,VRAM_H*scale,GL_RGBA,GL_UNSIGNED_BYTE);
    p_glGenRenderbuffers(1,&s_temp_rb);
    p_glBindRenderbuffer(PSXGL_RENDERBUFFER,s_temp_rb);
    p_glRenderbufferStorage(PSXGL_RENDERBUFFER,PSXGL_DEPTH24_STENCIL8,width*scale,VRAM_H*scale);
    if (!make_fbo(&s_temp_fbo,s_temp_tex,s_temp_rb)) return 0;
    s_temp_width=width; s_temp_scale=scale;
    return 1;
}
static int s_temp_filling;  /* a filler is being drawn: it obeys the mask and leaves it as it is */
static void temporal_mask(int check,int value) {
    if (s_temp_filling) {
        if (!check) { glDisable(GL_STENCIL_TEST); return; }
        glEnable(GL_STENCIL_TEST); glStencilMask(0); glStencilFunc(GL_EQUAL,0,1); glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
        return;
    }
    glEnable(GL_STENCIL_TEST); glStencilMask(1);
    glStencilFunc(check ? GL_EQUAL : GL_ALWAYS,check ? 0 : value,1);
    glStencilOp(GL_KEEP,GL_KEEP,check ? (value ? GL_INVERT : GL_KEEP) : GL_REPLACE);
}
/* Draw part of one retained command with the scene's own state. */
static void temporal_submit_faces(const TemporalScene *s,const TemporalCommand *c,int height,
                                  const float *vertices,int count);
/* How the frame being shown was placed, for a screen recording's list. */
static GpuTemporalMeshStats s_temp_shown_stats;
static int s_temp_shown_faces;

/* The diagnostic mesh view: after a textured command's faces the edges of its triangles, white whatever the texture, inside
 * black where the context draws a line wider than the white one. */
static void temporal_submit(const TemporalScene *s,const TemporalCommand *c,int height,
                            const float *vertices,int count) {
    temporal_submit_faces(s,c,height,vertices,count);
    if (!s_mesh_view || !c->textured || c->mode!=GL_TRIANGLES || s_temp_underlaying || s_temp_fill || count<=0) return;
    TemporalUniforms *u=&s_temp_uniforms[1];
    int sy=c->sy, sh=c->sh;
    float wide=0.75f*s->scale, narrow=0.375f*s->scale;
    if (sy<0) { sh+=sy;sy=0; }
    if (sy+sh>height) sh=height-sy;
    if (sh<=0) return;
    if (wide>s_line_width_max) wide=s_line_width_max;
    if (narrow>s_line_width_max) narrow=s_line_width_max;
    if (narrow<1.0f) narrow=1.0f;
    if (wide<narrow) wide=narrow;
    glEnable(GL_SCISSOR_TEST); glScissor(0,sy*s->scale,s->width*s->scale,sh*s->scale);
    p_glUseProgram(s_temp_tex_prog);
    p_glUniform1f(u->shift,0.5f/s->scale-1.0f/64.0f);
    p_glUniform1f(u->xhalf,s->width/2.0f); p_glUniform1f(u->visual,1);
    p_glUniform4f(u->options,0,0,s->tint,-1.0f);
    p_glBindVertexArray(s_temp_vao[1]); p_glBindBuffer(PSXGL_ARRAY_BUFFER,s_temp_vbo);
    p_glBufferData(PSXGL_ARRAY_BUFFER,count*TEXV*sizeof(float),vertices,PSXGL_STREAM_DRAW);
    glBindTexture(GL_TEXTURE_2D,s->textures[c->texture]);
    p_glUniform4i(u->twin,c->twin[0],c->twin[1],c->twin[2],c->twin[3]);
    p_glUniform1i(u->mask,c->mask); p_glUniform1i(u->filter,c->filter);
    p_glUniform1i(u->semi,0); p_glUniform1i(u->pass,0);
    glDisable(GL_BLEND); glDisable(GL_STENCIL_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    glPolygonMode(GL_FRONT_AND_BACK,GL_LINE); glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_CLEAR); glLineWidth(wide);
    glDrawArrays(GL_TRIANGLES,0,count);
    glLogicOp(GL_SET); glLineWidth(narrow);
    glDrawArrays(GL_TRIANGLES,0,count);
    glLogicOp(GL_COPY); glDisable(GL_COLOR_LOGIC_OP); glPolygonMode(GL_FRONT_AND_BACK,GL_FILL); glLineWidth(1.0f);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_TRUE);
    p_glUniform4f(u->options,0,0,s->tint,0.0f);
}

static void temporal_submit_faces(const TemporalScene *s,const TemporalCommand *c,int height,
                                  const float *vertices,int count) {
    const int t=c->textured;
    TemporalUniforms *u=&s_temp_uniforms[t];
    int sy=c->sy, sh=c->sh;
    if (sy<0) { sh+=sy;sy=0; }
    if (sy+sh>height) sh=height-sy;
    if (sh<=0 || count<=0) return;
    glEnable(GL_SCISSOR_TEST); glScissor(0,sy*s->scale,s->width*s->scale,sh*s->scale);
    p_glUseProgram(t?s_temp_tex_prog:s_temp_geo_prog);
    p_glUniform1f(u->shift,0.5f/s->scale-1.0f/64.0f);
    p_glUniform1f(u->xhalf,s->width/2.0f); p_glUniform1f(u->visual,1);
    p_glUniform4f(u->options,0,0,s->tint,s_temp_underlaying ? -1.0f : 0.0f);
    p_glBindVertexArray(s_temp_vao[t]); p_glBindBuffer(PSXGL_ARRAY_BUFFER,s_temp_vbo);
    p_glBufferData(PSXGL_ARRAY_BUFFER,count*TEXV*sizeof(float),vertices,PSXGL_STREAM_DRAW);
    /* With the shading on, the scene's depth is that of its opaque fragments, as on the real frames. */
    const int fill=s_temp_marked && s_temp_scene_depth && !s_temp_underlaying && t && c->mode==GL_TRIANGLES && s_temp_part!=2;
    const int fillers=fill && c->filler;
    const int shaded_scene=s_ssao && s_temp_scene_depth && !(s_world_depth && s_temp_underlaying);
    const int by_depth=s_world_depth && c->world && s_temp_marked && s_temp_scene_depth && !s_temp_underlaying &&
                       t && c->mode==GL_TRIANGLES && s_temp_part!=1;
    if (s_temp_fill) {
        /* Under the depth test the departed faces drawn under the world wrote no depth. Where the scene left none either
         * they leave theirs now: a pass at the far sentinel depth tags those pixels in stencil bit 1, a second writes. The
         * tag stays to the frame's end: of two faces over one pixel the later drawn, whose colour shows, leaves its depth,
         * or none when it has none. */
        if (!t || !s_temp_marked) return;
        glBindTexture(GL_TEXTURE_2D,s->textures[c->texture]);
        p_glUniform4i(u->twin,c->twin[0],c->twin[1],c->twin[2],c->twin[3]);
        p_glUniform1i(u->mask,c->mask); p_glUniform1i(u->filter,c->filter);
        p_glUniform1i(u->semi,c->semi<0?0:c->semi); p_glUniform1i(u->pass,c->semi<0?3:4);
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE); glDisable(GL_BLEND);
        glEnable(GL_STENCIL_TEST); glStencilMask(2);
        p_glUniform4f(u->options,0,0,s->tint,2.0f);
        glDepthFunc(GL_LESS); glDepthMask(GL_FALSE);
        glStencilFunc(GL_ALWAYS,2,2); glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
        glDrawArrays(GL_TRIANGLES,0,count);
        p_glUniform4f(u->options,0,0,s->tint,-1.0f);
        glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
        glStencilFunc(GL_EQUAL,2,2); glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
        glDrawArrays(GL_TRIANGLES,0,count);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glStencilMask(1);
        p_glUniform4f(u->options,0,0,s->tint,0);
        return;
    }
    if (!t) {
        if (s_temp_part==1) return;
        if (c->semi>=0) apply_psx_blend(c->semi); else glDisable(GL_BLEND);
        temporal_mask(c->check,c->mask);
        glLineWidth((float)s->scale);
        if (shaded_scene && c->semi>=0) glDepthMask(GL_FALSE);
        glDrawArrays((GLenum)c->mode,0,count);
        if (shaded_scene) glDepthMask(GL_TRUE);
        return;
    }
    glBindTexture(GL_TEXTURE_2D,s->textures[c->texture]);
    p_glUniform4i(u->twin,c->twin[0],c->twin[1],c->twin[2],c->twin[3]);
    p_glUniform1i(u->mask,c->mask); p_glUniform1i(u->filter,c->filter);
    p_glUniform1i(u->semi,c->semi<0?0:c->semi);
    if (fill && s_crack_fill>0.0f) {  /* the edges of the exact world polygons, see s_crack_fill */
        p_glUniform4f(u->options,0,0,s->tint,2.0f);
        glPolygonMode(GL_FRONT_AND_BACK,GL_LINE); glLineWidth(filler_line_width(s->scale));
        glDepthFunc(GL_LESS); glDepthMask(GL_FALSE); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE); glDisable(GL_BLEND);
        s_temp_filling=1; temporal_mask(c->check,c->mask); s_temp_filling=0;
        p_glUniform1i(u->pass,c->semi<0?0:4); glDrawArrays(GL_TRIANGLES,0,count);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glStencilMask(1);
        p_glUniform4f(u->options,0,0,s->tint,0);
    }
    for (int round=fillers;round>=(s_temp_part==1);--round) {  /* its grown copies first, then the batch over them */
    const int shaded=shaded_scene && !round;
    if (round) {
        p_glUniform4f(u->options,0,0,s->tint,GROWN_COPY_MARGIN); glDepthFunc(GL_LESS); glDepthMask(GL_FALSE);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE); s_temp_filling=1;
    } else if (fillers) {
        glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE); p_glUniform4f(u->options,0,0,s->tint,0);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glStencilMask(1); s_temp_filling=0;
    }
    if (!round && by_depth) {  /* as tex_batch_by_depth: the nearest opaque depth, then the colours within the margin of it */
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE); glDisable(GL_BLEND); glDisable(GL_STENCIL_TEST);
        glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
        p_glUniform1i(u->pass,c->semi<0?3:4); glDrawArrays(GL_TRIANGLES,0,count);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_FALSE);
        p_glUniform1f(s_temp_uPull,s_world_margin);
        if (!c->check && c->semi==4) {
            glEnable(GL_BLEND);
            p_glBlendEquationSeparate(PSXGL_FUNC_ADD,PSXGL_FUNC_ADD);
            p_glBlendFuncSeparate(GL_ONE,PSXGL_SRC1_ALPHA,GL_ONE,GL_ZERO);
            temporal_mask(0,c->mask);
            p_glUniform1i(u->pass,0); glDrawArrays(GL_TRIANGLES,0,count);
        } else if (c->semi<0) {
            temporal_mask(c->check,c->mask);
            p_glUniform1i(u->pass,0); glDrawArrays(GL_TRIANGLES,0,count);
        } else {
            temporal_mask(c->check,c->mask);
            p_glUniform1i(u->pass,1); glDrawArrays(GL_TRIANGLES,0,count);
            apply_psx_blend(c->semi); temporal_mask(c->check,1);
            p_glUniform1i(u->pass,2); glDrawArrays(GL_TRIANGLES,0,count);
        }
        p_glUniform1f(s_temp_uPull,0.0f); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
        continue;
    }
    if (!c->check && c->semi==4) {
        glEnable(GL_BLEND);
        p_glBlendEquationSeparate(PSXGL_FUNC_ADD,PSXGL_FUNC_ADD);
        p_glBlendFuncSeparate(GL_ONE,PSXGL_SRC1_ALPHA,GL_ONE,GL_ZERO);
        temporal_mask(0,c->mask);
        if (shaded) glDepthMask(GL_FALSE);
        p_glUniform1i(u->pass,0); glDrawArrays(GL_TRIANGLES,0,count);
        if (shaded) {
            glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE); glDisable(GL_BLEND); glDepthMask(GL_TRUE);
            p_glUniform1i(u->pass,4); glDrawArrays(GL_TRIANGLES,0,count);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        }
    } else if (c->semi<0) {
        glDisable(GL_BLEND); temporal_mask(c->check,c->mask);
        p_glUniform1i(u->pass,0); glDrawArrays(GL_TRIANGLES,0,count);
    } else {
        glDisable(GL_BLEND); temporal_mask(c->check,c->mask);
        p_glUniform1i(u->pass,1); glDrawArrays(GL_TRIANGLES,0,count);
        apply_psx_blend(c->semi); temporal_mask(c->check,1);
        if (shaded) glDepthMask(GL_FALSE);
        p_glUniform1i(u->pass,2); glDrawArrays(GL_TRIANGLES,0,count);
        if (shaded) glDepthMask(GL_TRUE);
    }
    }
    if (s_temp_part==1 && fillers) {
        glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE); p_glUniform4f(u->options,0,0,s->tint,0);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glStencilMask(1); s_temp_filling=0;
    }
}
/* The game draws back to front and keeps no depth, so nothing says where a
 * face it stopped submitting belongs among the current ones. Camera depth
 * does: every vertex carries it to the depth buffer, nearer is smaller.
 * It is the current frame's depth at every alpha. Along a view ray that still
 * orders static faces while the ray is within 90 degrees of the current axis,
 * and inside a face the value is interpolated, exact only at alpha 1. */
#define TEMP_ORDER_NEAR (-0.999999f)
#define TEMP_ORDER_FAR 0.999999f
/* Over the scene a departed face must be this much nearer than what it covers: the game paints a coarse wall quad
 * and then its finer pieces over it, and the coarse one, never seen, lies in the same plane (0 of 1306 points on top). */
#define TEMP_ORDER_CLEARANCE 1.03f
static float temporal_order(float depth) {
    return depth>16.0f ? 1.0f-32.0f/depth : TEMP_ORDER_NEAR;
}
/* World faces of the previous frame that the game no longer submits, carried
 * to where the camera puts them now. Without them the edge the view turns away
 * from shows the clear colour between real frames. Three kinds, two passes:
 * an opaque face with model coordinates goes under the current world and
 * again over the scene wherever it is nearer by TEMP_ORDER_CLEARANCE than the
 * current face that covered it. An opaque face without them (a grown copy) or with a held corner goes under the world
 * only. A sprite goes over the scene only, and only when no current sprite
 * stands in its place. A translucent current face writes its depth too, so
 * behind one the second pass is lost and the far face shows. */
static void temporal_bounds(const GpuTemporalTriangle *face,float box[4]) {
    for (int k=0;k<3;++k) {
        const float w=face->w[k]!=0.0f ? face->w[k] : 1.0f, x=face->x[k]/w, y=face->y[k]/w;
        if (!k || x<box[0]) box[0]=x;
        if (!k || x>box[1]) box[1]=x;
        if (!k || y<box[2]) box[2]=y;
        if (!k || y>box[3]) box[3]=y;
    }
}
/* A current sprite stands where the departed one ends: its next animation frame, as a rule. */
static int temporal_sprite_replaced(const TemporalScene *s,const GpuTemporalTriangle *to) {
    float gone[4]={0}, now[4]={0};
    temporal_bounds(to,gone);
    for (int i=0;i<s->nt;++i) {
        if (!s->triangles[i].sprite) continue;
        temporal_bounds(&s->triangles[i],now);
        if (now[0]<=gone[1] && gone[0]<=now[1] && now[2]<=gone[3] && gone[2]<=now[3]) return 1;
    }
    return 0;
}
static void temporal_underlay(float alpha,int covered) {
    const TemporalScene *p=&s_temp_prev, *s=&s_temp_curr;
    if (!s->mesh_stats.departed_faces || p->width!=s->width || p->scale!=s->scale) return;
    glDepthFunc(covered ? GL_LESS : GL_ALWAYS); glDepthMask(covered ? GL_FALSE : GL_TRUE);
    s_temp_scene_depth=!covered; s_temp_underlaying=1;
    if (s_world_depth && !covered) glDepthMask(GL_FALSE);  /* last frame's faces must not stand in the way of this frame's */
    for (int i=0;i<p->nc;++i) {
        const TemporalCommand *c=&p->commands[i];
        const int t=c->textured;
        /* A textured batch is keyed 4 when opaque and mode-0 faces share it. */
        const int opaque=c->semi<0 || (t && c->semi==4);
        int any=0;
        if (c->mode!=GL_TRIANGLES || c->check || (!opaque && !covered)) continue;
        memcpy(s_temp_under,p->vertices+c->first*TEXV,c->count*TEXV*sizeof(float));
        for (int j=0;j<c->count;j+=3) {
            const int ti=c->first_tri+j/3;
            GpuTemporalTriangle out;
            const GpuTemporalTriangle *was=&p->triangles[ti];
            int show=p->gone[ti];
            /* Over the scene the old picture of a replaced sprite would ring the new one. A sprite the
             * game dropped has no successor and is carried on. */
            if (show && was->sprite) show=covered && !temporal_sprite_replaced(s,&p->gone_to[ti]);
            else if (show) show=opaque && !(t && s_temp_under[j*TEXV+18]!=0.0f) && (!covered || (was->modelled && show==1));
            if (show) {
                gpu_temporal_lerp(&p->start[ti],&p->gone_to[ti],alpha,&out);
                show=gpu_temporal_mesh_facing(&p->triangles[ti],&out);
            }
            for (int k=0;k<3;++k) {
                float *v=&s_temp_under[(j+k)*TEXV];
                v[0]=v[t?20:7]=show?out.x[k]:0; v[1]=v[t?21:8]=show?out.y[k]:0;
                v[t?23:9]=show?temporal_order(gpu_temporal_lerp_depth(&p->gone_to[ti],&out,k)*(covered?TEMP_ORDER_CLEARANCE:1.0f)):TEMP_ORDER_FAR;
            }
            any|=show;
        }
        if (any) temporal_submit(p,c,s->height,s_temp_under,c->count);
    }
    glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
    s_temp_scene_depth=1; s_temp_underlaying=0;
}
static int temporal_present(float alpha,int lx,int ly,int lw,int lh) {
    TemporalScene *s=&s_temp_curr;
    int first_world, last_world, underlaid=0;
    if (!s->active || !s->moving ||
        !temporal_target(s->width,s->scale)) return 0;
    for (first_world=0;first_world<s->nt && !s->triangles[first_world].world;++first_world) {}
    for (last_world=s->nt-1;last_world>=0 && !s->triangles[last_world].world;--last_world) {}
    if (s_interp_fence[s_interp_cur]) p_glWaitSync(s_interp_fence[s_interp_cur],0,PSXGL_TIMEOUT_IGNORED);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_temp_fbo);
    glViewport(0,0,s->width*s->scale,VRAM_H*s->scale);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glStencilMask(0xFF); glClearStencil(s->clear[3]>0.5f ? 1:0);
    glClearColor(s->clear[0],s->clear[1],s->clear[2],s->clear[3]);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE); glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    s_temp_scene_depth=1;
    s_temp_shown_stats=s->mesh_stats; s_temp_shown_faces=s->nt;
    p_glActiveTexture(PSXGL_TEXTURE0);
    for (int i=0;i<s->nc;++i) {
        TemporalCommand *c=&s->commands[i];
        const int t=c->textured;
        int split=0;
        memcpy(s_temp_scratch,s->vertices+c->first*TEXV,c->count*TEXV*sizeof(float));
        if (c->mode==GL_TRIANGLES) for (int j=0;j<c->count;j+=3) {
            int ti=c->first_tri+j/3;
            const GpuTemporalTriangle *now=&s->triangles[ti];
            if (ti==first_world) split=j;
            for (int k=0;k<3;++k)
                s_temp_scratch[(j+k)*TEXV+(t?23:9)]=
                    now->world && now->depth[k]>0.0f ? temporal_order(now->depth[k])
                    : ti<first_world ? TEMP_ORDER_FAR : TEMP_ORDER_NEAR;
            /* Under the depth test what has no depth is far, as on the real frames, unless nothing of the world follows it. */
            if (s_world_depth && (now->world || ti<last_world))
                for (int k=0;k<3;++k)
                    if (!(now->world && now->depth[k]>0.0f)) s_temp_scratch[(j+k)*TEXV+(t?23:9)]=TEMP_ORDER_FAR;
            if (!now->world) continue;
            GpuTemporalTriangle out;
            gpu_temporal_lerp(&s->from[ti],now,alpha,&out);
            const int facing=gpu_temporal_mesh_facing(now,&out);
            for (int k=0;k<3;++k) {
                float *v=&s_temp_scratch[(j+k)*TEXV];
                v[0]=v[t?20:7]=out.x[facing?k:0]; v[1]=v[t?21:8]=out.y[facing?k:0];
                if (t) v[22]=out.q[k];
                if (now->depth[k]>0.0f) v[t?23:9]=temporal_order(gpu_temporal_lerp_depth(now,&out,k));
            }
        } else for (int j=0;j<c->count;++j)
            s_temp_scratch[j*TEXV+9]=c->first_tri<=first_world || (s_world_depth && c->first_tri<=last_world)
                ? TEMP_ORDER_FAR : TEMP_ORDER_NEAR;
        /* Whatever precedes the first world face is the backdrop. */
        if (!underlaid && c->mode==GL_TRIANGLES && c->first_tri<=first_world &&
            first_world<c->first_tri+c->count/3) {
            /* The fillers of the whole command go under its backdrop part too, as under the frame loop's whole batch. */
            s_temp_part=1; temporal_submit(s,c,s->height,s_temp_scratch,c->count); s_temp_part=2;
            temporal_submit(s,c,s->height,s_temp_scratch,split);
            temporal_underlay(alpha,0);
            underlaid=1;
            temporal_submit(s,c,s->height,s_temp_scratch+split*TEXV,c->count-split);
            s_temp_part=0;
            continue;
        }
        temporal_submit(s,c,s->height,s_temp_scratch,c->count);
    }
    if (underlaid && s_world_depth) { s_temp_fill=1; temporal_underlay(alpha,0); s_temp_fill=0; }
    if (underlaid) temporal_underlay(alpha,1);
    s_temp_scene_depth=0;
    glDisable(GL_DEPTH_TEST);
#ifndef PSX_NO_DEBUG_TOOLS
    temporal_dump_frame(0,0,s->width*s->scale,s->height*s->scale,alpha,s->mesh_stats.departed_faces);
    if (temporal_edge_diag())
        fprintf(stdout,"psxrecomp: edge interp %llu %.3f %d %d %d %d %d\n",
                (unsigned long long)s_temp_published,alpha,
                temporal_edge_gap(s->scale,0,s->height*s->scale,s->clear),
                temporal_edge_gap((s->width-2)*s->scale,0,s->height*s->scale,s->clear),
                s->mesh_stats.departed_faces,s->mesh_stats.camera_inliers,s->height*s->scale);
#endif
    glDisable(GL_STENCIL_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
    if (ssao_draw(&s_ssao_thread,s_interp_thread_vao,s_temp_fbo,0,s_temp_tex,s->width*s->scale,VRAM_H*s->scale,
                  0.5f/s->width,0.5f/VRAM_H,1-0.5f/s->width,(s->height-0.5f)/VRAM_H,
                  s->scale,s_interp_linear,lx,ly,lw,lh)) {
        ++s_temp_replays;
        return 1;
    }
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,0); glViewport(lx,ly,lw,lh);
    glBindTexture(GL_TEXTURE_2D,s_temp_tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,s_interp_linear?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,s_interp_linear?GL_LINEAR:GL_NEAREST);
    p_glUseProgram(s_temp_present_prog);
    p_glUniform1i(p_glGetUniformLocation(s_temp_present_prog,"u_tex"),0);
    p_glUniform4f(p_glGetUniformLocation(s_temp_present_prog,"u_uv_rect"),
                  0.5f/s->width,0.5f/VRAM_H,
                  1-0.5f/s->width,(s->height-0.5f)/VRAM_H);
    p_glBindVertexArray(s_interp_thread_vao); glDrawArrays(GL_TRIANGLES,0,3);
    p_glBindVertexArray(0); p_glUseProgram(0);
    ++s_temp_replays;
    return 1;
}
/* Optional bounded GPU parity check. Compare the rerasterized alpha=1
 * endpoint against the real source, before display filtering. Readbacks are
 * deliberately absent from normal play. */
static void temporal_verify_endpoint(int lx,int ly,int lw,int lh) {
    if (!s_temp_verify_left || !s_temp_curr.active || !s_temp_curr.moving) return;
    --s_temp_verify_left;
    if (!temporal_present(1,lx,ly,lw,lh)) return;
    size_t pixels=(size_t)s_interp_w*s_interp_h;
    unsigned char *actual=(unsigned char *)malloc(pixels*8);
    if (!actual) return;
    unsigned char *expected=actual+pixels*4;
    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER,s_temp_fbo);
    glReadPixels(0,0,s_interp_w,s_interp_h,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    glBindTexture(GL_TEXTURE_2D,s_interp_tex[s_interp_cur]);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,expected);
    size_t different=0; int max_delta=0;
    for (size_t i=0;i<pixels;++i) {
        int changed=0;
        for (int c=0;c<3;++c) {
            int d=abs((int)actual[4*i+c]-(int)expected[4*i+c]);
            if (d>max_delta) max_delta=d;
            if (d>1) changed=1;
        }
        different+=changed;
    }
    fprintf(stdout,"psxrecomp: GL geometry endpoint check: %llu/%llu RGB pixels differ >1, max delta %d, GL error 0x%x\n",
            (unsigned long long)different,(unsigned long long)pixels,max_delta,(unsigned)glGetError());
    fflush(stdout);
    free(actual);
    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER,0);
}
static void temporal_release(void) {
    /* Main context, only after the worker has stopped. Shared programs/buffers
     * disappear with the share group; free CPU allocations and texture pools. */
    for (int i=0;i<TEMP_BUILD_SLOTS+2;++i) {
        TemporalScene *s=i<TEMP_BUILD_SLOTS ? &s_temp_build[i] : i==TEMP_BUILD_SLOTS ? &s_temp_prev : &s_temp_curr;
        free(s->vertices); free(s->triangles); free(s->from); free(s->commands); free(s->matches);
        free(s->start); free(s->gone_to); free(s->gone);
        glDeleteTextures(TEMP_MAX_TEXTURES,s->textures);
        memset(s,0,sizeof(*s));
    }
    s_temp_ready=NULL;
    s_temp_fbo=s_temp_tex=s_temp_rb=0; s_temp_width=s_temp_scale=0;
}
