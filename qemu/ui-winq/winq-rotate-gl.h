/* SPDX-License-Identifier: GPL-2.0-only */
/* Presentation-context resources, created once. Rotation samples the shared
 * scanout directly; there is no intermediate image or CPU readback. */
static GLuint winq_rotate_program, winq_rotate_vao, winq_rotate_vbo;
static GLuint winq_rotate_sampler;
static GLint winq_rotate_matrix, winq_rotate_offset, winq_rotate_flip;
static bool winq_rotate_failed;

static GLuint winq_rotate_shader(GLenum type, const char *body)
{
    const char *version = winq_wgl_attivo() ? "#version 130\n" :
                                            "#version 300 es\nprecision mediump float;\n";
    const char *parts[] = { version, body };
    GLuint shader = glCreateShader(type);
    GLint ok;
    char log[1024];

    glShaderSource(shader, 2, parts, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        error_report("winq: rotation shader failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool winq_rotate_init(void)
{
    static const GLfloat vertices[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    GLuint vs, fs;
    GLint ok;
    char log[1024];

    if (winq_rotate_program) {
        return true;
    }
    if (winq_rotate_failed) {
        return false;
    }
    winq_rotate_failed = true;
    vs = winq_rotate_shader(GL_VERTEX_SHADER,
        "in vec2 position; out vec2 uv;\n"
        "uniform mat2 source_matrix; uniform vec2 source_offset;\n"
        "uniform bool flip_y;\n"
        "void main() {\n"
        " vec2 p = vec2((position.x + 1.0)*0.5, (1.0-position.y)*0.5);\n"
        " vec2 s = source_matrix*p + source_offset;\n"
        " uv = vec2(s.x, flip_y ? s.y : 1.0-s.y);\n"
        " gl_Position = vec4(position, 0.0, 1.0); }\n");
    fs = winq_rotate_shader(GL_FRAGMENT_SHADER,
        "in vec2 uv; out vec4 colour; uniform sampler2D image;\n"
        "void main() { colour = texture(image, uv); }\n");
    if (!vs || !fs) {
        if (vs) { glDeleteShader(vs); }
        if (fs) { glDeleteShader(fs); }
        return false;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "position");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        error_report("winq: rotation program failed: %s", log);
        glDeleteProgram(program);
        return false;
    }
    winq_rotate_program = program;
    winq_rotate_matrix = glGetUniformLocation(program, "source_matrix");
    winq_rotate_offset = glGetUniformLocation(program, "source_offset");
    winq_rotate_flip = glGetUniformLocation(program, "flip_y");
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    glGenVertexArrays(1, &winq_rotate_vao);
    glBindVertexArray(winq_rotate_vao);
    glGenBuffers(1, &winq_rotate_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, winq_rotate_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glGenSamplers(1, &winq_rotate_sampler);
    glSamplerParameteri(winq_rotate_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(winq_rotate_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(winq_rotate_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(winq_rotate_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    info_report("winq: shared-texture rotation ready");
    return true;
}

static bool winq_rotate_draw(GLuint texture, bool flip_y,
                             int x, int y, int w, int h)
{
    double x0, y0, x1, y1, x2, y2;
    GLfloat matrix[4];

    if (!winq_rotate_init()) {
        return false;
    }
    /* The same inverse transform maps mouse coordinates and shader UVs. */
    winq_rotation_source_uv(winq.geom.rotation, 0, 0, &x0, &y0);
    winq_rotation_source_uv(winq.geom.rotation, 1, 0, &x1, &y1);
    winq_rotation_source_uv(winq.geom.rotation, 0, 1, &x2, &y2);
    matrix[0] = x1-x0; matrix[1] = y1-y0;
    matrix[2] = x2-x0; matrix[3] = y2-y0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(x, y, w, h);
    glUseProgram(winq_rotate_program);
    glUniformMatrix2fv(winq_rotate_matrix, 1, GL_FALSE, matrix);
    glUniform2f(winq_rotate_offset, x0, y0);
    glUniform1i(winq_rotate_flip, flip_y);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindSampler(0, winq_rotate_sampler);
    glBindVertexArray(winq_rotate_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindSampler(0, 0);
    return true;
}
