#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// C++
#include <vector>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
//#include <GLES3/gl3.h>
#define GLEW_EGL
#include <GL/glew.h>

#include <glm/glm.hpp>

#include <sys/time.h>
#include <linux/input.h>

#include <cairo.h>

#include "xdg-shell.h"

#define WINDOW_WIDTH 480
#define WINDOW_HEIGHT 360

struct wl_display *display = NULL;
struct wl_compositor *compositor = NULL;
struct wl_subcompositor *subcompositor = NULL;
struct wl_surface *wl_surface;
struct wl_egl_window *egl_window;
struct wl_region *region;
struct wl_seat *seat = NULL;
struct wl_keyboard *keyboard = NULL;
struct wl_pointer *pointer = NULL;

struct xdg_wm_base *xdg_wm_base = NULL;
struct xdg_surface *xdg_surface = NULL;
struct xdg_toplevel *xdg_toplevel = NULL;

EGLDisplay egl_display;
EGLConfig egl_conf;
EGLSurface egl_surface;
EGLContext egl_context;
GLuint program_object;

//struct wl_subsurface *subsurface;

uint32_t image_width;
uint32_t image_height;
uint32_t image_size;
uint32_t *image_data;

uint32_t cursor_width;
uint32_t cursor_height;
uint32_t cursor_size;
uint32_t *cursor_data;

GLuint indices[] = {
    0, 1, 3,    // first triangle
    1, 2, 3,    // second triangle
};

//================
// Surface
//================

class Surface
{
public:
    Surface(uint32_t width, uint32_t height);

    uint32_t width() const;
    uint32_t height() const;
    uint32_t scale() const;

    void set_size(uint32_t width, uint32_t height);

    uint32_t scaled_width() const;
    uint32_t scaled_height() const;

private:
    uint32_t _width;
    uint32_t _height;
    uint32_t _scale;
};

Surface::Surface(uint32_t width, uint32_t height)
{
    this->_width = width;
    this->_height = height;
    this->_scale = 2;
}

uint32_t Surface::width() const
{
    return this->_width;
}

uint32_t Surface::height() const
{
    return this->_height;
}

uint32_t Surface::scale() const
{
    return this->_scale;
}

void Surface::set_size(uint32_t width, uint32_t height)
{
    this->_width = width;
    this->_height = height;
}

uint32_t Surface::scaled_width() const
{
    return this->_width * this->_scale;
}

uint32_t Surface::scaled_height() const
{
    return this->_height * this->_scale;
}

Surface surface(WINDOW_WIDTH, WINDOW_HEIGHT);

//=================
// KeyboardState
//=================

class KeyboardState
{
public:
    uint32_t rate;
    uint32_t delay;

    bool pressed;       // Keyboard is pressed.
    uint32_t key;       // Pressed key.
    bool repeat;        // Keyboard is repeating.
    bool processed;     // Key processed first time.
    uint64_t time;
    uint64_t pressed_time;  // Key pressed time in milliseconds.
    uint64_t elapsed_time;  // Key elapsed time in milliseconds.

    bool repeating() const
    {
        if (pressed_time == 0) {
            return false;
        }
        return elapsed_time - pressed_time >= delay;
    }

    bool should_processed(uint32_t key)
    {
        if (this->key == key && (!this->processed || this->repeating())) {
            return true;
        }

        return false;
    }
};

KeyboardState keyboard_state;

namespace gl {

class Object
{
public:
    Object(int32_t x, int32_t y, uint32_t width, uint32_t height);

    void set_image(const uint8_t *image_data,
            uint64_t width, uint64_t height);

    void init_texture();

    int32_t x() const;
    int32_t y() const;
    int32_t viewport_x() const;
    int32_t viewport_y() const;
    uint32_t width() const;
    uint32_t height() const;

    uint32_t scaled_width() const;
    uint32_t scaled_height() const;

    void set_x(int32_t x);
    void set_y(int32_t y);

    GLuint vao() const;
    GLuint ebo() const;
    GLuint texture() const;

    std::vector<glm::vec3> vertices() const;

private:
    int32_t _x;
    int32_t _y;
    uint32_t _width;
    uint32_t _height;

    const uint8_t *_image_data;
    uint64_t _image_width;
    uint64_t _image_height;
    GLuint _texture;
};

//====================
// Object Methods
//====================
Object::Object(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    this->_x = x;
    this->_y = y;
    this->_width = width;
    this->_height = height;

    this->_image_data = nullptr;
    this->_image_width = 0;
    this->_image_height = 0;
    this->_texture = 0;
}

void Object::set_image(const uint8_t *image_data,
        uint64_t width, uint64_t height)
{
    this->_image_data = image_data;
    this->_image_width = width;
    this->_image_height = height;
}

void Object::init_texture()
{
    if (this->_image_data == nullptr) {
        fprintf(stderr, "[WARN] Image is null!\n");
    }

    glGenTextures(1, &this->_texture);
    glBindTexture(GL_TEXTURE_2D, this->_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        this->_image_width,
        this->_image_height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        this->_image_data
    );
    glGenerateMipmap(GL_TEXTURE_2D);

    fprintf(stderr, "Object::init_texture() - texture: %d\n", this->_texture);
}

int32_t Object::x() const
{
    return this->_x;
}

int32_t Object::y() const
{
    return this->_y;
}

int32_t Object::viewport_x() const
{
    return this->_x;
}

int32_t Object::viewport_y() const
{
    return surface.scaled_height() - (this->_y) - (this->_height * surface.scale());
}

uint32_t Object::width() const
{
    return this->_width;
}

uint32_t Object::height() const
{
    return this->_height;
}

void Object::set_x(int32_t x)
{
    if (this->_x != x) {
        this->_x = x;
    }
}

void Object::set_y(int32_t y)
{
    if (this->_y != y) {
        this->_y = y;
    }
}

uint32_t Object::scaled_width() const
{
    return this->_width * surface.scale();
}

uint32_t Object::scaled_height() const
{
    return this->_height * surface.scale();
}

GLuint Object::texture() const
{
    return this->_texture;
}

std::vector<glm::vec3> Object::vertices() const
{
    std::vector<glm::vec3> v;

    // Top right.
    v.push_back(glm::vec3(
        this->_width / WINDOW_WIDTH,
        this->_height / WINDOW_HEIGHT,
        0.0f
    ));

    // Bottom right.
    v.push_back(glm::vec3(
        this->_width / WINDOW_WIDTH,
        -(this->_height / WINDOW_HEIGHT),
        0.0f
    ));

    // Bottom left.
    v.push_back(glm::vec3(
        -(this->_width / WINDOW_WIDTH),
        -(this->_height / WINDOW_HEIGHT),
        0.0f
    ));

    // Top left.
    v.push_back(glm::vec3(
        -(1.0f - ((float)this->_x / WINDOW_WIDTH)),
        (1.0f - ((float)this->_y / WINDOW_HEIGHT)),
        0.0f
    ));

    return v;
}

} // namespace gl

std::vector<gl::Object> objects;
std::vector<glm::ivec3> vectors;

gl::Object cursor_object(0, 0, 12, 12);
glm::ivec3 cursor_vector;

/*
GLbyte vertex_shader_str[] =
    "#version 330 core              \n"
//        "attribute vec4 vPosition;      \n"
    "layout (location = 0) in vec3 aPos;        \n"
    "layout (location = 1) in vec3 aColor;      \n"
    "layout (location = 2) in vec2 aTexCoord;   \n"
    "out vec3 ourColor;             \n"
    "out vec2 TexCoord;             \n"
    "void main()                            \n"
    "{                                      \n"
    "    gl_Position = vec4(aPos, 1.0);     \n"
    "    ourColor = aColor;                 \n"
    "    TexCoord = aTexCoord;              \n"
    "}                                      \n";
*/

const uint32_t x = 100;
const uint32_t y = 100;
const uint32_t width = 50;
const uint32_t height = 100;

std::vector<glm::vec3> vertices = {
    {  ((float)width / WINDOW_WIDTH),  1.0f - ((float)height / WINDOW_HEIGHT), 0.0f }, // Top right
    {  ((float)width / WINDOW_WIDTH), -(1.0f - ((float)height / WINDOW_HEIGHT)), 0.0f }, // Bottom right
    { -(1.0f - ((float)x / WINDOW_WIDTH)), -(1.0f - ((float)height / WINDOW_HEIGHT)), 0.0f }, // Bottom left
    { -(1.0f - ((float)x / WINDOW_WIDTH)),  1.0f - ((float)y / WINDOW_HEIGHT), 0.0f }, // Top left
};

std::vector<glm::vec3> full_vertices = {
    {  1.0f,  1.0f, 0.0f },
    {  1.0f, -1.0f, 0.0f },
    { -1.0f, -1.0f, 0.0f },
    { -1.0f,  1.0f, 0.0f },
};

glm::vec3 colors[] = {
    { 1.0f, 1.0f, 1.0f, },
    { 0.0f, 1.0f, 0.0f, },
    { 0.0f, 0.0f, 1.0f, },
};

GLfloat tex_coords[] = {
    1.0f, 0.0f,
    1.0f, -1.0f,
    0.0f, -1.0f,
    0.0f, 0.0f,
};

void load_image()
{
    cairo_surface_t *cairo_surface = cairo_image_surface_create_from_png(
        "miku@2x.png");
    cairo_t *cr = cairo_create(cairo_surface);
    image_width = cairo_image_surface_get_width(cairo_surface);
    image_height = cairo_image_surface_get_height(cairo_surface);
    image_size = sizeof(uint32_t) * (image_width * image_height);
    image_data = (uint32_t*)malloc(image_size);
    memcpy(image_data, cairo_image_surface_get_data(cairo_surface), image_size);

    // Color correct.
    for (uint32_t i = 0; i < (image_width * image_height); ++i) {
        uint32_t color = 0x00000000;
        color += ((image_data[i] & 0xff000000));  // Set alpha.
        color += ((image_data[i] & 0x00ff0000) >> 16);   // Set blue.
        color += ((image_data[i] & 0x0000ff00));   // Set green.
        color += ((image_data[i] & 0x000000ff) << 16);  // Set red.
        image_data[i] = color;
    }

    cairo_surface_destroy(cairo_surface);
    cairo_destroy(cr);
}

void load_image2()
{
    cairo_surface_t *cairo_surface = cairo_image_surface_create_from_png(
        "cursor.png");
    cairo_t *cr = cairo_create(cairo_surface);
    cursor_width = cairo_image_surface_get_width(cairo_surface);
    cursor_height = cairo_image_surface_get_height(cairo_surface);
    cursor_size = sizeof(uint32_t) * (cursor_width * cursor_height);
    cursor_data = (uint32_t*)malloc(cursor_size);
    memcpy(cursor_data, cairo_image_surface_get_data(cairo_surface), cursor_size);

    // Color correct.
    for (uint32_t i = 0; i < (cursor_width * cursor_height); ++i) {
        uint32_t color = 0x00000000;
        color += ((cursor_data[i] & 0xff000000));  // Set alpha.
        color += ((cursor_data[i] & 0x00ff0000) >> 16);   // Set blue.
        color += ((cursor_data[i] & 0x0000ff00));   // Set green.
        color += ((cursor_data[i] & 0x000000ff) << 16);  // Set red.
        cursor_data[i] = color;
    }

    cairo_surface_destroy(cairo_surface);
    cairo_destroy(cr);
}

GLuint load_shader(const char *path, GLenum type)
{
    GLuint shader;
    GLint compiled;

    // Read file.
    uint8_t *code = nullptr;
    int64_t size;
    FILE *f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    size = ftell(f) + 1;
    fseek(f, 0, SEEK_SET);

    code = new uint8_t[size];
    fread(code, size - 1, 1, f);
    code[size] = '\0';

    // Create the shader object.
    shader = glCreateShader(type);
    if (shader == 0) {
        return 0;
    }
    fprintf(stderr, "Shader: %d\n", shader);

    // Load the shader source.
    glShaderSource(shader, 1, (const GLchar* const*)&code, NULL);

    // Compile the shader.
    glCompileShader(shader);

    // Check the compile status.
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;

        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = (char*)malloc(sizeof(char));
            glGetShaderInfoLog(shader, info_len, NULL, info_log);
            fprintf(stderr, "Error compiling shader: %s\n", info_log);
            free(info_log);
        }

        glDeleteShader(shader);
        return 0;
    }

    delete[] code;

    return shader;
}

int init(GLuint *program_object)
{
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLint linked;

    vertex_shader = load_shader("shaders/shader.vert", GL_VERTEX_SHADER);
    fragment_shader = load_shader("shaders/shader.frag", GL_FRAGMENT_SHADER);

    fprintf(stderr, "Vertex shader: %d\n", vertex_shader);
    fprintf(stderr, "Fragment shader: %d\n", fragment_shader);

    *program_object = glCreateProgram();
    if (*program_object == 0) {
        fprintf(stderr, "glCreateProgram() - program_object is 0\n");
        return 0;
    }
    fprintf(stderr, "Program object created.\n");

    glAttachShader(*program_object, vertex_shader);
    glAttachShader(*program_object, fragment_shader);

    // Bind vPosition to attribute 0.
//    glBindAttribLocation(*program_object, 0, "vPosition");

    // Link the program.
    glLinkProgram(*program_object);

    // Check the link status.
    glGetProgramiv(*program_object, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint info_len = 0;
        glGetProgramiv(*program_object, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = (char*)malloc(sizeof(char) * info_len);

            glGetProgramInfoLog(*program_object, info_len, NULL, info_log);
            fprintf(stderr, "Error linking program: %s\n", info_log);
            free(info_log);
        }

        glDeleteProgram(*program_object);
        return 0;
    }

    load_image();
    load_image2();

    return 1;
}

static void recreate_window();

//=============
// Pointer
//=============

static void pointer_enter_handler(void *data,
          struct wl_pointer *wl_pointer,
          uint32_t serial,
          struct wl_surface *surface,
          wl_fixed_t surface_x,
          wl_fixed_t surface_y)
{
    (void)data;
    (void)wl_pointer;
    (void)serial;
    (void)surface;
    (void)surface_x;
    (void)surface_y;
}

static void pointer_leave_handler(void *data,
          struct wl_pointer *wl_pointer,
          uint32_t serial,
          struct wl_surface *surface)
{
    (void)data;
    (void)wl_pointer;
    (void)serial;
    (void)surface;
}

static void pointer_motion_handler(void *data,
           struct wl_pointer *wl_pointer,
           uint32_t time,
           wl_fixed_t surface_x,
           wl_fixed_t surface_y)
{
    (void)data;
    (void)wl_pointer;
    (void)time;
    (void)surface_x;
    (void)surface_y;
}

static void pointer_button_handler(void *data,
           struct wl_pointer *wl_pointer,
           uint32_t serial,
           uint32_t time,
           uint32_t button,
           uint32_t state)
{
    (void)data;
    (void)wl_pointer;
    (void)serial;
    (void)time;
    if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        xdg_toplevel_move(xdg_toplevel, seat, serial);
    } else if (button == BTN_RIGHT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        xdg_toplevel_resize(xdg_toplevel, seat, serial,
            XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT);
    }
}

static void pointer_axis_handler(void *data,
         struct wl_pointer *wl_pointer,
         uint32_t time,
         uint32_t axis,
         wl_fixed_t value)
{
    (void)data;
    (void)wl_pointer;
    (void)time;
    (void)axis;
    (void)value;
}

static void pointer_frame_handler(void *data,
          struct wl_pointer *wl_pointer)
{
    (void)data;
    (void)wl_pointer;
}

static void pointer_axis_source_handler(void *data,
            struct wl_pointer *wl_pointer,
            uint32_t axis_source)
{
    (void)data;
    (void)wl_pointer;
    (void)axis_source;
}

static void pointer_axis_stop_handler(void *data,
          struct wl_pointer *wl_pointer,
          uint32_t time,
          uint32_t axis)
{
    (void)data;
    (void)wl_pointer;
    (void)time;
    (void)axis;
}

static void pointer_axis_discrete_handler(void *data,
              struct wl_pointer *wl_pointer,
              uint32_t axis,
              int32_t discrete)
{
    (void)data;
    (void)wl_pointer;
    (void)axis;
    (void)discrete;
}

static const wl_pointer_listener pointer_listener = {
    .enter = pointer_enter_handler,
    .leave = pointer_leave_handler,
    .motion = pointer_motion_handler,
    .button = pointer_button_handler,
    .axis = pointer_axis_handler,
    .frame = pointer_frame_handler,
    .axis_source = pointer_axis_source_handler,
    .axis_stop = pointer_axis_stop_handler,
    .axis_discrete = pointer_axis_discrete_handler,
};

//=============
// Keyboard
//=============

static void keyboard_keymap_handler(void *data, struct wl_keyboard *keyboard,
        uint32_t format, int fd, uint32_t size)
{
    (void)data;
    (void)keyboard;
    (void)format;
    (void)fd;
    (void)size;
}

static void keyboard_enter_handler(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
}

static void keyboard_leave_handler(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
}

static void keyboard_key_handler(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)time;
    (void)key;
    (void)state;
    fprintf(stderr, "Key! %d\n", key);

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        keyboard_state.pressed = true;
        keyboard_state.key = key;
    } else if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        keyboard_state.pressed = false;
        keyboard_state.key = key;
    }
}

static void keyboard_modifiers_handler(void *data, struct wl_keyboard *keyboard,
        uint32_t serial,
        uint32_t mods_depressed,
        uint32_t mods_latched,
        uint32_t mods_locked,
        uint32_t group)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)mods_depressed;
    (void)mods_latched;
    (void)mods_locked;
    (void)group;
}

static void keyboard_repeat_info_handler(void *data,
        struct wl_keyboard *keyboard,
        int rate, int delay)
{
    (void)data;
    (void)keyboard;
    fprintf(stderr, "keyboard_repeat_info_handler()\n");
    fprintf(stderr, " - rate: %d\n", rate);
    fprintf(stderr, " - delay: %d\n", delay);

    keyboard_state.rate = rate;
    keyboard_state.delay = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap_handler,
    .enter = keyboard_enter_handler,
    .leave = keyboard_leave_handler,
    .key = keyboard_key_handler,
    .modifiers = keyboard_modifiers_handler,
    .repeat_info = keyboard_repeat_info_handler,
};

//=============
// Seat
//=============

static void seat_capabilities_handler(void *data, struct wl_seat *wl_seat,
        uint32_t caps_uint)
{
    (void)data;
    (void)wl_seat;
    enum wl_seat_capability caps = (enum wl_seat_capability)caps_uint;

    fprintf(stderr, "seat_capabilities_handler()\n");

    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        fprintf(stderr, " - Has keyboard!\n");
        keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
    }
    if (caps & WL_SEAT_CAPABILITY_POINTER) {
        pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer, &pointer_listener, NULL);
    }
}

static void seat_name_handler(void *data, struct wl_seat *seat,
        const char *name)
{
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities_handler,
    .name = seat_name_handler,
};

//===========
// XDG
//===========
static void xdg_wm_base_ping_handler(void *data,
        struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping_handler,
};

static void xdg_surface_configure_handler(void *data,
        struct xdg_surface *xdg_surface, uint32_t serial)
{
    (void)data;
    fprintf(stderr, "xdg_surface_configure_handler()\n");
    xdg_surface_ack_configure(xdg_surface, serial);
}

const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure_handler,
};

static void xdg_toplevel_configure_handler(void *data,
        struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height,
        struct wl_array *states)
{
    (void)data;
    (void)xdg_toplevel;
    (void)width;
    (void)height;
    (void)states;
    void *p_state;
    enum xdg_toplevel_state state;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpointer-arith"
    wl_array_for_each(p_state, states) {
        state = *((enum xdg_toplevel_state*)(p_state));
        if (state == XDG_TOPLEVEL_STATE_RESIZING) {
            fprintf(stderr, "Resizing! %dx%d\n", width, height);
            if (width != 0 && height != 0) {
                surface.set_size(width, height);
                recreate_window();
            }
        }
    }
#pragma GCC diagnostic pop
}

static void xdg_toplevel_close_handler(void *data,
        struct xdg_toplevel *xdg_toplevel)
{
    (void)data;
    (void)xdg_toplevel;
}

const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure_handler,
    .close = xdg_toplevel_close_handler,
};

//==============
// Global
//==============
static void global_registry_handler(void *data, struct wl_registry *registry,
        uint32_t id, const char *interface, uint32_t version)
{
    (void)data;

    if (strcmp(interface, "wl_compositor") == 0) {
        fprintf(stderr, "Interface is <wl_compositor>.\n");
        compositor = (struct wl_compositor*)wl_registry_bind(
            registry,
            id,
            &wl_compositor_interface,
            version
        );
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        xdg_wm_base = (struct xdg_wm_base*)wl_registry_bind(registry,
            id, &xdg_wm_base_interface, 1);
    } else if (strcmp(interface, "wl_subcompositor") == 0) {
        subcompositor = (struct wl_subcompositor*)wl_registry_bind(registry,
            id, &wl_subcompositor_interface, 1);
    } else if (strcmp(interface, "wl_seat") == 0) {
        seat = (struct wl_seat*)wl_registry_bind(registry,
            id, &wl_seat_interface, 5);
        wl_seat_add_listener(seat, &seat_listener, NULL);
    } else {
        printf("(%d) Got a registry event for <%s> id <%d>\n",
            version, interface, id);
    }
}

static void global_registry_remover(void *data, struct wl_registry *registry,
        uint32_t id)
{
    (void)data;
    (void)registry;
    (void)id;
    printf("Got a registry losing event for <%d>\n", id);
}

static const struct wl_registry_listener registry_listener = {
    global_registry_handler,
    global_registry_remover
};

static void gl_info()
{
    const GLubyte *version = glGetString(GL_VERSION);
    fprintf(stderr, "OpenGL version: %s\n", version);
}

static void init_egl()
{
    EGLint major, minor, count, n, size;
    EGLConfig *configs;
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT,
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_ALPHA_SIZE,
        8,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_BIT,
        EGL_NONE,
    };

    static const EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION,
        4,
        EGL_CONTEXT_MINOR_VERSION,
        6,
        EGL_NONE,
    };

    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Can't create egl display\n");
        exit(1);
    } else {
        fprintf(stderr, "Created egl display.\n");
    }

    eglBindAPI(EGL_OPENGL_API);

    if (eglInitialize(egl_display, &major, &minor) != EGL_TRUE) {
        fprintf(stderr, "Can't initialise egl display.\n");
        exit(1);
    }
    printf("EGL major: %d, minor %d\n", major, minor);

    eglGetConfigs(egl_display, NULL, 0, &count);
    printf("EGL has %d configs.\n", count);

    configs = (void**)calloc(count, sizeof *configs);

    eglChooseConfig(egl_display, config_attribs, configs, count, &n);

    for (int i = 0; i < n; ++i) {
        eglGetConfigAttrib(egl_display, configs[i], EGL_BUFFER_SIZE, &size);
        printf("Buffersize for config %d is %d\n", i, size);
        eglGetConfigAttrib(egl_display, configs[i], EGL_RED_SIZE, &size);
        printf("Red size for config %d is %d.\n", i, size);
    }
    // Just choose the first one.
    egl_conf = configs[0];

    egl_context = eglCreateContext(egl_display, egl_conf, EGL_NO_CONTEXT,
        context_attribs);

    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context);
}

static void create_window()
{
    egl_window = wl_egl_window_create(wl_surface,
        surface.scaled_width(), surface.scaled_height());
    if (egl_window == EGL_NO_SURFACE) {
        fprintf(stderr, "Can't create egl window.\n");
        exit(1);
    } else {
        fprintf(stderr, "Created egl window.\n");
    }

    egl_surface = eglCreateWindowSurface(egl_display, egl_conf, egl_window,
        NULL);
    if (eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "Made current.\n");
    } else {
        fprintf(stderr, "Made current failed.\n");
    }

    glClearColor(0.5, 0.5, 0.5, 0.8);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();

    if (eglSwapBuffers(egl_display, egl_surface)) {
        fprintf(stderr, "Swapped buffers.\n");
    } else {
        fprintf(stderr, "Swapped buffers failed.\n");
    }
}

static void recreate_window()
{
//    eglDestroySurface(egl_display, egl_surface);
    wl_egl_window_resize(egl_window, surface.scaled_width(), surface.scaled_height(), 0, 0);
}

static void create_objects()
{
    // Object 1.
    gl::Object obj(0, 0, 200, 100);
    obj.set_image((const uint8_t*)image_data, image_width, image_height);
    obj.init_texture();
    objects.push_back(obj);
    vectors.push_back(glm::ivec3(2, 4, 0));

    // Object 2.
    gl::Object obj2(50, 150, 100, 100);
    obj2.set_image((const uint8_t*)image_data, image_width, image_height);
    obj2.init_texture();
    objects.push_back(obj2);
    vectors.push_back(glm::ivec3(4, 2, 0));

    // Object 3.
    gl::Object obj3(0, 0, 64, 64);
    obj3.set_image((const uint8_t*)image_data, image_width, image_height);
    obj3.init_texture();
    objects.push_back(obj3);
    vectors.push_back(glm::ivec3(6, 4, 0));

    cursor_object.set_image((const uint8_t*)cursor_data, cursor_width, cursor_height);
    cursor_object.init_texture();
}

static void move_objects()
{
    for (uint64_t i = 0; i < objects.size(); ++i) {
        objects[i].set_x(objects[i].x() + vectors[i].x);
        objects[i].set_y(objects[i].y() + vectors[i].y);
        // Bottom bound.
        if (objects[i].y() + objects[i].scaled_height() >= surface.scaled_height()) {
            vectors[i].y = -(vectors[i].y);
        }
        // Right bound.
        if (objects[i].x() + objects[i].scaled_width() >= surface.scaled_width()) {
            vectors[i].x = -(vectors[i].x);
        }
        // Top bound.
        if (objects[i].y() <= 0) {
            vectors[i].y = -(vectors[i].y);
        }
        // Left bound.
        if (objects[i].x() <= 0) {
            vectors[i].x = -(vectors[i].x);
        }
    }

    // Cursor.
    cursor_object.set_x(cursor_object.x() + cursor_vector.x);
    cursor_object.set_y(cursor_object.y() + cursor_vector.y);
}

static void process_keyboard()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t ms = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);

    if (keyboard_state.pressed == true) {
        if (!keyboard_state.processed) {
            keyboard_state.pressed_time = ms;
        } else {
            keyboard_state.elapsed_time = ms;
        }
        if (keyboard_state.repeating()) {
            fprintf(stderr, "Key repeat!\n");
        }
        if (keyboard_state.should_processed(KEY_ENTER)) {
            fprintf(stderr, "Enter.\n");
            gl::Object obj(0, 0, 128, 128);
            obj.set_image((const uint8_t*)image_data, image_width, image_height);
            obj.init_texture();
            objects.push_back(obj);
            vectors.push_back(glm::ivec3(2, 2, 0));
        } else if (keyboard_state.key == KEY_RIGHT && !keyboard_state.processed) {
            cursor_vector.x = 2;
            cursor_vector.y = 0;
        } else if (keyboard_state.key == KEY_LEFT && !keyboard_state.processed) {
            cursor_vector.x = -2;
            cursor_vector.y = 0;
        } else if (keyboard_state.key == KEY_DOWN && !keyboard_state.processed) {
            cursor_vector.y = 2;
            cursor_vector.x = 0;
        } else if (keyboard_state.key == KEY_UP && !keyboard_state.processed) {
            cursor_vector.y = -2;
            cursor_vector.x = 0;
        } else if (keyboard_state.key == KEY_MINUS && !keyboard_state.processed) {
            surface.set_size(surface.width() - 10, surface.height() - 10);
            recreate_window();
        } else if (keyboard_state.key == KEY_EQUAL && !keyboard_state.processed) {
            surface.set_size(surface.width() + 10, surface.height() + 10);
            recreate_window();
        }
        keyboard_state.processed = true;
    } else {
        keyboard_state.processed = false;
        keyboard_state.pressed_time = 0;
    }
}

static void draw_frame()
{
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    // Clear the color buffer.
    glClearColor(0.5, 0.5, 0.5, 0.8);
    glClear(GL_COLOR_BUFFER_BIT);

    // Use the program object.
    glUseProgram(program_object);

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    GLuint ebo;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    GLuint vbo[2];
    glGenBuffers(2, vbo);

    for (auto& object: objects) {
        glBindVertexArray(vao);

        // Position attribute.
        glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
        glBufferData(GL_ARRAY_BUFFER,
            sizeof(glm::vec3) * full_vertices.size(),
            full_vertices.data(),
            GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(0);

        // Texture coord attribute
        glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coords), tex_coords, GL_STATIC_DRAW);

        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(1);

//        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, object.texture());

        glBindVertexArray(vao);

        glViewport(object.viewport_x(), object.viewport_y(),
            object.scaled_width(), object.scaled_height());
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)0);
    }

    {
        glBindVertexArray(vao);

        // Position attribute.
        glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
        glBufferData(GL_ARRAY_BUFFER,
            sizeof(glm::vec3) * full_vertices.size(),
            full_vertices.data(),
            GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(0);

        // Texture coord attribute
        glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coords), tex_coords, GL_STATIC_DRAW);

        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(1);

//        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, cursor_object.texture());

        glBindVertexArray(vao);

        glViewport(cursor_object.viewport_x(), cursor_object.viewport_y(),
            cursor_object.scaled_width(), cursor_object.scaled_height());
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)0);
    }

    eglSwapBuffers(egl_display, egl_surface);

    // Free.
    glDeleteBuffers(2, vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &vao);
}

static void get_server_references()
{
    display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "Can't connect to display.\n");
        exit(1);
    }
    printf("Connected to display.\n");

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    if (compositor == NULL || xdg_wm_base == NULL) {
        fprintf(stderr, "Can't find compositor or xdg_wm_base.\n");
        exit(1);
    } else {
        fprintf(stderr, "Found compositor and xdg_wm_base.\n");
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    get_server_references();

    // Check surface.
    fprintf(stderr, " - Checking surface...\n");
    wl_surface = wl_compositor_create_surface(compositor);
    if (wl_surface == NULL) {
        fprintf(stderr, "Can't create surface.\n");
        exit(1);
    } else {
        fprintf(stderr, "Created surface!\n");
    }
    // subsurface = wl_subcompositor_get_subsurface(subcompositor,
    //     surface2, surface);
    // wl_subsurface_set_position(subsurface, -10, -10);
    wl_surface_set_buffer_scale(wl_surface, 2);

    xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, NULL);

    // Check xdg surface.
    fprintf(stderr, " - Checking xdg surface...\n");
    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, wl_surface);
    if (xdg_surface == NULL) {
        fprintf(stderr, "Can't create xdg surface.\n");
        exit(1);
    } else {
        fprintf(stderr, "Created xdg surface!\n");
    }
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    if (xdg_toplevel == NULL) {
        exit(1);
    }
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);


    // MUST COMMIT! or not working on weston.
    wl_surface_commit(wl_surface);

    wl_display_roundtrip(display);

    // create_opaque_region();
    if (!GL_VERSION_4_6) {
        fprintf(stderr, "No GL_VERSION_4_6!\n");
    }
    fprintf(stderr, "Has GL_VERSION_4_6\n");

    init_egl();
    gl_info();

    fprintf(stderr, "GLEW experimental: %d\n", glewExperimental);
    GLenum err = glewInit();
    if (err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY) {
        fprintf(stderr, "Failed to init GLEW! err: %d %s\n", err,
            glewGetErrorString(err));
        exit(1);
    }

    create_window();

    if (init(&program_object) == 0) {
        fprintf(stderr, "Error init!\n");
    }

    wl_surface_commit(wl_surface);

    create_objects();

    draw_frame();

    int res = wl_display_dispatch(display);
    while (res != -1) {
        // Process keyboard state.
        process_keyboard();
        // Move.
        move_objects();

        draw_frame();
        res = wl_display_dispatch(display);
    }
    fprintf(stderr, "wl_display_dispatch() - res: %d\n", res);

    wl_display_disconnect(display);
    printf("Disconnected from display.\n");

    return 0;
}
