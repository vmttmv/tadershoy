#include "common.h"
#include "font.h"
#include "memory.h"
#include <assert.h>
#include <float.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>

// Update the program every N frame
#define FILE_UPDATE_RATE    10

#define NSEC_PER_SEC        1000000000
#define DEFAULT_WIDTH       1280
#define DEFAULT_HEIGHT      720

// Explicit uniform locations
#define ULOC_RESOLUTION     0
#define ULOC_TIME           1
#define ULOC_TIME_DELTA     2
#define ULOC_FRAME          3
#define ULOC_MOUSE          4

#define make_vertex(pos, uv, color) (vertex_t){(pos), (uv), (color)}
#define make_vec2(x, y) (vec2){{(x), (y)}}
#define make_rect(x, y, w, h) (rect_t){(x), (y), (w), (h)}

#pragma pack(push, 1)
typedef struct
{
    vec2 pos;
    vec2 uv;
    uint32_t color;
} vertex_t;
#pragma pack(pop)

static const char *file_template =
    "// Inputs:\n"
    "// uniform vec2 iResolution; - Viewport resolution in pixels\n"
    "// uniform float iTime; - Playback time (in seconds)\n"
    "// uniform float iTimeDelta; - Render time (in seconds)\n"
    "// uniform int iFrame; - Current frame number\n"
    "// uniform vec2 iMouse; - Cursor coordinates\n\n"
    "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
    "   fragColor = vec4(1.0);\n"
    "}\n";

static const char *quad_vs_src =
    "#version 450 core\n"
    "layout(location = 0) in vec2 pos;\n"
    "layout(location = 1) in vec2 uv;\n"
    "layout(location = 2) in uint color;\n"
    "layout(location = 0) out vec2 fsUV;\n"
    "layout(location = 1) out vec4 fsColor;\n"
    "layout(location = 0) uniform vec2 iResolution;\n"
    "vec4 unpack_rgba(uint v) {\n"
    "    float r = ((v >> 24) & 0xFF) / 255.0;\n"
    "    float g = ((v >> 16) & 0xFF) / 255.0;\n"
    "    float b = ((v >> 8) & 0xFF) / 255.0;\n"
    "    float a = (v & 0xFF) / 255.0;\n"
    "    return vec4(r, g, b, a);\n"
    "}\n"
    "void main(void) {\n"
    "   gl_Position = vec4(2.0*pos.x/iResolution.x-1.0, 1.0-2.0*pos.y/iResolution.y, 0.0, 1.0);\n"
    "   fsUV = uv;\n"
    "   fsColor = unpack_rgba(color);\n"
    "}\n";

static const char *quad_fs_src =
    "#version 450 core\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "layout(location = 0) in vec2 fsUV;\n"
    "layout(location = 1) in vec4 fsColor;\n"
    "layout(location = 1) uniform sampler2D iSampler;\n"
    "void main(void) {\n"
    "   if (fsUV.x < 0.0) {\n"
    "       fragColor = fsColor;\n"
    "   } else {\n"
    "       fragColor = fsColor * texture(iSampler, fsUV).r;\n"
    "   }\n"
    "}\n";

static const char *vs_src =
    "#version 450 core\n"
    "const vec2[3] verts = vec2[3](\n"
    "   vec2(-4.0, -1.0),\n"
    "   vec2(1.0, -1.0),\n"
    "   vec2(1.0, 4.0));\n"
    "void main(void) {\n"
    "   gl_Position = vec4(verts[gl_VertexID], 0.0, 1.0);\n"
    "}\n";

static const char *fs_header_src =
    "#version 450 core\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "layout(location = 0) uniform vec2 iResolution;\n"
    "layout(location = 1) uniform float iTime;\n"
    "layout(location = 2) uniform float iTimeDelta;\n"
    "layout(location = 3) uniform int iFrame;\n"
    "layout(location = 4) uniform vec2 iMouse;\n";

static const char *fs_footer_src =
    "void main(void) {\n"
    "   mainImage(fragColor, gl_FragCoord.xy);\n"
    "}\n";

static PFNGLXSWAPINTERVALEXTPROC glXSwapIntervalEXT;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
static PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
static PFNGLVERTEXATTRIBIPOINTERPROC glVertexAttribIPointer;
static PFNGLGENBUFFERSPROC glGenBuffers;
static PFNGLDELETEBUFFERSPROC glDeleteBuffers;
static PFNGLBINDBUFFERPROC glBindBuffer;
static PFNGLBUFFERDATAPROC glBufferData;
static PFNGLTEXSTORAGE2DPROC glTexStorage2D;
static PFNGLCREATESHADERPROC glCreateShader;
static PFNGLSHADERSOURCEPROC glShaderSource;
static PFNGLCOMPILESHADERPROC glCompileShader;
static PFNGLGETSHADERIVPROC glGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
static PFNGLATTACHSHADERPROC glAttachShader;
static PFNGLDETACHSHADERPROC glDetachShader;
static PFNGLDELETESHADERPROC glDeleteShader;
static PFNGLCREATEPROGRAMPROC glCreateProgram;
static PFNGLLINKPROGRAMPROC glLinkProgram;
static PFNGLDELETEPROGRAMPROC glDeleteProgram;
static PFNGLGETPROGRAMIVPROC glGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
static PFNGLUSEPROGRAMPROC glUseProgram;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
static PFNGLUNIFORM1FPROC glUniform1f;
static PFNGLUNIFORM1IPROC glUniform1i;
static PFNGLUNIFORM2FPROC glUniform2f;
static PFNGLUNIFORM3FPROC glUniform3f;
static PFNGLUNIFORM4FPROC glUniform4f;

static Display *display;
static Window window;
static int window_width;
static int window_height;

static struct timespec file_mtime;

static char *log_buffer;
static char *file_buffer;
static vertex_t *vertex_buffer;

static GLuint vao;
static GLuint vbo;

static inline void timespec_sub(struct timespec *r, const struct timespec *a, const struct timespec *b)
{
    r->tv_sec = a->tv_sec - b->tv_sec;
    r->tv_nsec = a->tv_nsec - b->tv_nsec;
    if (r->tv_nsec < 0) {
        r->tv_sec--;
        r->tv_nsec += NSEC_PER_SEC;
    }
}

static inline double timespec_to_msec(const struct timespec *a)
{
    return (double)a->tv_sec*1000.0 + (double)a->tv_nsec / 1000000000.0;
}

static inline void *get_proc(const char *name)
{
    return (void *)glXGetProcAddress((const GLubyte *)name);
}

static GLXContext create_context(void)
{
    static const int visual_attribs[] = {
        GLX_X_RENDERABLE,   True,
        GLX_DRAWABLE_TYPE,  GLX_WINDOW_BIT,
        GLX_RENDER_TYPE,    GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE,  GLX_TRUE_COLOR,
        GLX_RED_SIZE,       8,
        GLX_GREEN_SIZE,     8,
        GLX_BLUE_SIZE,      8,
        GLX_ALPHA_SIZE,     8,
        GLX_DEPTH_SIZE,     24,
        GLX_DOUBLEBUFFER,   True,
        None
    };

    static const int context_attribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB,  4,
        GLX_CONTEXT_MINOR_VERSION_ARB,  5,
        GLX_CONTEXT_FLAGS_ARB,          GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        GLX_CONTEXT_PROFILE_MASK_ARB,   GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None
    };
    
    int num_configs;
    GLXFBConfig *configs = glXChooseFBConfig(display, DefaultScreen(display), visual_attribs, &num_configs);
    if (!configs || !num_configs) return NULL;

    GLXFBConfig config = configs[0];
    XVisualInfo *vi = glXGetVisualFromFBConfig(display, config);
    XFree(configs);

    GLXContext ctx = glXCreateContext(display, vi, 0, GL_TRUE);
    XFree(vi);
    if (!ctx) return NULL;

    PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribsARB = (PFNGLXCREATECONTEXTATTRIBSARBPROC)get_proc("glXCreateContextAttribsARB");
    glXSwapIntervalEXT = (PFNGLXSWAPINTERVALEXTPROC)get_proc("glXSwapIntervalEXT");
    glXDestroyContext(display, ctx);

    ctx = glXCreateContextAttribsARB(display, config, NULL, GL_TRUE, context_attribs);
    if (!ctx) return NULL;

    return ctx;
}

static void get_procs(void)
{
    glCreateShader = (PFNGLCREATESHADERPROC)get_proc("glCreateShader");
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)get_proc("glGenVertexArrays");
    glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)get_proc("glDeleteVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)get_proc("glBindVertexArray");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)get_proc("glEnableVertexAttribArray");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)get_proc("glVertexAttribPointer");
    glVertexAttribIPointer = (PFNGLVERTEXATTRIBIPOINTERPROC)get_proc("glVertexAttribIPointer");
    glGenBuffers = (PFNGLGENBUFFERSPROC)get_proc("glGenBuffers");
    glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)get_proc("glDeleteBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC)get_proc("glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC)get_proc("glBufferData");
    glTexStorage2D = (PFNGLTEXSTORAGE2DPROC)get_proc("glTexStorage2D");
    glShaderSource = (PFNGLSHADERSOURCEPROC)get_proc("glShaderSource");
    glCompileShader = (PFNGLCOMPILESHADERPROC)get_proc("glCompileShader");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)get_proc("glGetShaderiv");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)get_proc("glGetShaderInfoLog");
    glAttachShader = (PFNGLATTACHSHADERPROC)get_proc("glAttachShader");
    glDetachShader = (PFNGLDETACHSHADERPROC)get_proc("glDetachShader");
    glDeleteShader = (PFNGLDELETESHADERPROC)get_proc("glDeleteShader");
    glCreateProgram = (PFNGLCREATEPROGRAMPROC)get_proc("glCreateProgram");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)get_proc("glLinkProgram");
    glDeleteProgram = (PFNGLDELETEPROGRAMPROC)get_proc("glDeleteProgram");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)get_proc("glGetProgramiv");
    glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)get_proc("glGetProgramInfoLog");
    glUseProgram = (PFNGLUSEPROGRAMPROC)get_proc("glUseProgram");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)get_proc("glGetUniformLocation");
    glUniform1f = (PFNGLUNIFORM1FPROC)get_proc("glUniform1f");
    glUniform1i = (PFNGLUNIFORM1IPROC)get_proc("glUniform1i");
    glUniform2f = (PFNGLUNIFORM2FPROC)get_proc("glUniform2f");
    glUniform3f = (PFNGLUNIFORM3FPROC)get_proc("glUniform3f");
    glUniform4f = (PFNGLUNIFORM4FPROC)get_proc("glUniform4f");
}

static GLuint create_shader(const char **src, int num_src,  GLenum type)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, num_src, src, NULL);
    glCompileShader(shader);
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        int len;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        if (len > 0) {
            array_clear(log_buffer);
            array_ensure(log_buffer, (size_t)len);
            glGetShaderInfoLog(shader, len, &len, log_buffer);
            glDeleteShader(shader);
            assert(log_buffer);
            array_header(log_buffer)->size = (size_t)len;
        }
        return 0;
    }

    return shader;
}

static GLuint link_program(GLuint vs, GLuint fs)
{
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        int len;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        if(len > 0) {
            array_clear(log_buffer);
            array_ensure(log_buffer, (size_t)len);
            glGetProgramInfoLog(program, len, &len, log_buffer);
            glDeleteProgram(program);
            assert(log_buffer);
            array_header(log_buffer)->size = (size_t)len;
        }
        return 0;
    }

    return program;
}

static inline void push_quad(rect_t r, rect_t uv, uint32_t color)
{
    array_push_back(vertex_buffer, make_vertex(make_vec2(r.x, r.y), make_vec2(uv.x, uv.y), color));
    array_push_back(vertex_buffer, make_vertex(make_vec2(r.x+r.w, r.y), make_vec2(uv.x+uv.w, uv.y), color));
    array_push_back(vertex_buffer, make_vertex(make_vec2(r.x+r.w, r.y+r.h), make_vec2(uv.x+uv.w, uv.y+uv.h), color));

    array_push_back(vertex_buffer, make_vertex(make_vec2(r.x+r.w, r.y+r.h), make_vec2(uv.x+uv.w, uv.y+uv.h), color));
    array_push_back(vertex_buffer, make_vertex(make_vec2(r.x, r.y+r.h), make_vec2(uv.x, uv.y+uv.h), color));
    array_push_back(vertex_buffer, make_vertex(make_vec2(r.x, r.y), make_vec2(uv.x, uv.y), color));
}

static void push_text(const char *str, size_t len, float x, float y)
{
    uint32_t color = 0xFFFFFFFF;
    float orig_x = x;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\n') {
            y += 20.0f;
            x = orig_x;
            continue;
        }

        const glyph_t *glyph = get_glyph(str[i]);
        rect_t r = make_rect(x+(float)glyph->offset_x, y-(float)glyph->offset_y, (float)glyph->width, (float)glyph->height);
        push_quad(r, glyph->uv, color);
        x += glyph->advance_x;
    }
}

static bool update_file_buffer(const char *path, size_t size)
{
    array_ensure(file_buffer, size+1);

    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    size_t n = fread(file_buffer, 1, size, fp);
    file_buffer[n] = 0;
    fclose(fp);

    return true;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Please specify a path.\n");
        return EXIT_SUCCESS;
    }

    const char *path = argv[1];

    // Check if the given file exists, create one if it does not.
    struct stat st;
    if (stat(path, &st)) {
        FILE *fp = fopen(path, "w");
        if (fp) {
            fwrite(file_template, strlen(file_template), 1, fp);
            fclose(fp);
        } else {
            // TODO: Error handling
        }
    }

    display = XOpenDisplay(NULL);
    Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);

    XSetWindowAttributes attr = {0};
    attr.event_mask = ExposureMask|StructureNotifyMask|PointerMotionMask;
    window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, DEFAULT_WIDTH, DEFAULT_HEIGHT,
                           0, CopyFromParent, InputOutput, CopyFromParent, CWEventMask, &attr);
    XSetWMProtocols(display, window, &wm_delete_window, 1);
    XMapWindow(display, window);

    window_width = DEFAULT_WIDTH;
    window_height = DEFAULT_HEIGHT;
    
    GLXContext ctx = create_context();
    if (!ctx) {
        XDestroyWindow(display, window);
        return EXIT_FAILURE;
    }
    glXMakeCurrent(display, window, ctx);
    glXSwapIntervalEXT(display, window, 1);
    get_procs();

    GLuint vs = create_shader(&quad_vs_src, 1, GL_VERTEX_SHADER);
    if (!vs) {
        glXDestroyContext(display, ctx);
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }
    GLuint fs = create_shader(&quad_fs_src, 1, GL_FRAGMENT_SHADER);
    if (!fs) {
        glDeleteShader(vs);
        glXDestroyContext(display, ctx);
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }
    GLuint quad_program = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!quad_program) {
        glXDestroyContext(display, ctx);
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }

    GLuint program = 0;
    vs = create_shader(&vs_src, 1, GL_VERTEX_SHADER);
    if (!vs) {
        glXDestroyContext(display, ctx);
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8, FONT_TEXTURE_WIDTH, FONT_TEXTURE_HEIGHT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FONT_TEXTURE_WIDTH, FONT_TEXTURE_HEIGHT, GL_RED,
                    GL_UNSIGNED_BYTE, font_data + NUM_GLYPHS*sizeof(glyph_t));

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, pos));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)offsetof(vertex_t, uv));
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, sizeof(vertex_t), (void *)offsetof(vertex_t, color));

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    // TODO: Possibly use XQueryPointer() instead of this to initialize the state.
    int mouse_x = -1;
    int mouse_y = -1;

    char fps_buffer[16];
    double t_total = 0.0;
    int frame = 0;
    int last_read = FILE_UPDATE_RATE;
    int running = 1;
    while (running) {
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            switch (event.type) {
                case MotionNotify: {
                    mouse_x = event.xmotion.x;
                    mouse_y = event.xmotion.y;
                } break;

                case ClientMessage: {
                    if ((Atom)event.xclient.data.l[0] == wm_delete_window)
                        running = 0;
                } break;

                case ConfigureNotify: {
                    window_width = event.xconfigure.width;
                    window_height = event.xconfigure.height;
                } break;
            }
        }

        if (last_read >= FILE_UPDATE_RATE) {
            if (stat(path, &st) == 0) {
                if (st.st_mtim.tv_sec != file_mtime.tv_sec || st.st_mtim.tv_nsec != file_mtime.tv_nsec) {
                    if (update_file_buffer(path, (size_t)st.st_size)) {
                        const char *src[3] = { fs_header_src, file_buffer, fs_footer_src };
                        if (program) glDeleteProgram(program);
                        program = 0;
                        fs = create_shader(src, 3, GL_FRAGMENT_SHADER);
                        if (fs) {
                            program = link_program(vs, fs);
                            if (program) {
                                file_mtime = st.st_mtim;
                                array_clear(log_buffer);
                            }
                        }
                    }
                }
            }
            last_read = 0;
        }

        struct timespec t1, delta;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        timespec_sub(&delta, &t1, &t0);
        t0 = t1;

        double dt = timespec_to_msec(&delta);
        t_total += dt;
        if (t_total > (double)FLT_MAX) t_total -= (double)FLT_MAX;

        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(0, 0, window_width, window_height);
        if (program) {
            glUseProgram(program);
            glUniform2f(ULOC_RESOLUTION, (float)window_width, (float)window_height);
            glUniform1f(ULOC_TIME, (float)t_total);
            glUniform1f(ULOC_TIME_DELTA, (float)dt);
            glUniform1i(ULOC_FRAME, frame);
            glUniform2f(ULOC_MOUSE, (float)mouse_x, (float)mouse_y);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            int len = snprintf(fps_buffer, 16, "FPS: %.3f", 1.0/(double)dt);
            push_quad(make_rect(0, 0, 90, 18), make_rect(-1, -1, -1, -1), 0x7F);
            push_text(fps_buffer, (size_t)len, 0, 14.0f);
        } else {
            push_text(log_buffer, array_size(log_buffer), 0, 14.0f);
        }

        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(array_size(vertex_buffer)*sizeof(vertex_t)),
                     vertex_buffer, GL_STREAM_DRAW);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(quad_program);
        glUniform2f(ULOC_RESOLUTION, (float)window_width, (float)window_height);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)array_size(vertex_buffer));
        glDisable(GL_BLEND);

        array_clear(vertex_buffer);

        glXSwapBuffers(display, window);

        frame++;
        last_read++;
    }

    array_free(log_buffer);
    array_free(file_buffer);
    array_free(vertex_buffer);

    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(quad_program);
    glDeleteProgram(program);
    glXDestroyContext(display, ctx);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return EXIT_SUCCESS;
}
