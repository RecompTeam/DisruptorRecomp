#define FACTORY_SMOKE_BASE_HEIGHT 1788
#define FACTORY_SMOKE_RISE 1460
#define FACTORY_SMOKE_RISE_VARIATION 72
#define FACTORY_SMOKE_SECTIONS 16
#define FACTORY_SMOKE_GLSL_TEXT(value) FACTORY_SMOKE_GLSL_LITERAL(value)
#define FACTORY_SMOKE_GLSL_LITERAL(value) #value

static GLuint factory_smoke_program(void) {
    return build_program(
        "#version 330 core\n"
        "uniform vec4 camera, project, smoke;\n"
        "out vec2 plume; out float depth; flat out float seed;\n"
        "const int corner[6] = int[6](0,1,2,2,1,3);\n"
        "const int sections = " FACTORY_SMOKE_GLSL_TEXT(FACTORY_SMOKE_SECTIONS) ";\n"
        "void main(){\n"
        "  int pipe = gl_VertexID / (sections * 6), strip = gl_VertexID % (sections * 6) / 6;\n"
        "  int vertex = corner[gl_VertexID % 6];\n"
        "  float section = float(strip + vertex / 2) / float(sections);\n"
        "  float height = section * section;\n"
        "  float side = (vertex & 1) == 0 ? -1.0 : 1.0;\n"
        "  seed = float(pipe);\n"
        "  plume = vec2(side, height);\n"
        "  vec2 forward = vec2(cos(camera.w), sin(camera.w));\n"
        "  vec2 right = vec2(forward.y, -forward.x);\n"
        "  float taper = (1.0 - height) * (1.0 - height);\n"
        "  float spread = 48.0 + 234.0 * (1.0 - taper * taper * (1.0 - height)) + 36.0 * height;\n"
        "  float meander = 54.0 * sin(8.5 * height + seed * 1.8 - smoke.x * 12.5663706) * height;\n"
        "  vec2 mouth = vec2(12448.0 + 256.0 * seed, 3136.0);\n"
        "  vec2 offset = mouth - camera.xz + vec2(340.0, 128.0) * height * height;\n"
        "  offset += right * (side * spread + meander);\n"
        "  float rise = " FACTORY_SMOKE_GLSL_TEXT(FACTORY_SMOKE_BASE_HEIGHT)
        ".0 + (" FACTORY_SMOKE_GLSL_TEXT(FACTORY_SMOKE_RISE)
        ".0 + " FACTORY_SMOKE_GLSL_TEXT(FACTORY_SMOKE_RISE_VARIATION)
        ".0 * sin(seed * 1.5)) * height - camera.y;\n"
        "  depth = dot(offset, forward);\n"
        "  gl_Position = vec4(dot(offset, right) * project.x, depth * project.y - rise * project.z, depth - 2.0, depth);\n"
        "}\n",
        "#version 330 core\n"
        "uniform vec4 smoke;\n"
        "in vec2 plume; in float depth; flat in float seed; out vec4 frag;\n"
        "const float grain_period = 32.0;\n"
        "float grain(vec2 position){\n"
        "  position.y = mod(position.y, grain_period);\n"
        "  vec3 hash = fract(position.xyx * 0.1031);\n"
        "  hash += dot(hash, hash.yzx + 33.33);\n"
        "  return fract((hash.x + hash.y) * hash.z);\n"
        "}\n"
        "float turbulence(vec2 position){\n"
        "  vec2 point = floor(position), part = fract(position);\n"
        "  part = part * part * (3.0 - 2.0 * part);\n"
        "  float low = mix(grain(point), grain(point + vec2(1.0, 0.0)), part.x);\n"
        "  float high = mix(grain(point + vec2(0.0, 1.0)), grain(point + vec2(1.0)), part.x);\n"
        "  return mix(low, high, part.y);\n"
        "}\n"
        "void main(){\n"
        "  float flow = plume.y * 13.0 - smoke.x * (2.0 * grain_period);\n"
        "  float billow = turbulence(vec2(0.35 + seed * 9.4, flow));\n"
        "  float across = plume.x - (billow - 0.5) * 0.22;\n"
        "  float detail = turbulence(vec2(across * 3.1 + seed * 13.7, flow * 2.0 + 4.3));\n"
        "  float radius = mix(0.61, 0.79, billow);\n"
        "  float edge = max(0.0, abs(across) + (0.53 - detail) * 0.14) / radius;\n"
        "  float thickness = sqrt(max(0.0, 1.0 - edge * edge));\n"
        "  float cover = thickness * (1.0 - smoothstep(0.79, 1.0, edge));\n"
        "  float falloff = smoothstep(0.0, 0.012, plume.y) * (1.0 - smoothstep(0.48, 1.0, plume.y));\n"
        "  float haze = smoke.z > smoke.y ? smoothstep(smoke.y, smoke.z, depth) : 0.0;\n"
        "  float pressure = smoothstep(0.14, 0.88, 0.72 * billow + 0.28 * detail);\n"
        "  float density = cover * falloff * (0.68 + 0.30 * pressure) * (1.0 - haze);\n"
        "  frag = vec4(0.105, 0.096, 0.083, density);\n"
        "}\n");
}

#undef FACTORY_SMOKE_GLSL_TEXT
#undef FACTORY_SMOKE_GLSL_LITERAL
