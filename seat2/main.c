#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <unistd.h>

#include "xdg-shell.h"

#include "utils.h"

struct wl_display *display = NULL;
struct wl_compositor *compositor = NULL;
struct wl_surface *surface;
struct wl_egl_window *egl_window;
struct wl_region *region;
struct wl_callback *callback;
struct wl_callback *frame_callback;
struct xdg_wm_base *xdg_wm_base;
struct xdg_surface *xdg_surface;
struct xdg_toplevel *xdg_toplevel;
struct wl_buffer *buffer;

struct wl_surface *surface2;
struct wl_callback *frame_callback2;
struct wl_region *region2;

struct wl_subcompositor *subcompositor;
struct wl_subsurface *subsurface;

void *shm_data;

// Input devices
struct wl_seat *seat = NULL;
struct wl_pointer *pointer;
struct wl_keyboard *keyboard;

// Cursor
struct wl_shm *shm;
struct wl_cursor_theme *cursor_theme;
struct wl_cursor *default_cursor;
struct wl_surface *cursor_surface;

// EGL
EGLDisplay egl_display;
EGLConfig egl_conf;
EGLSurface egl_surface;
EGLContext egl_context;

//============
// Keyboard
//============
static void keyboard_keymap_handler(void *data, struct wl_keyboard *keyboard,
        uint32_t format, int fd, uint32_t size)
{
}

static void keyboard_enter_handler(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
    fprintf(stderr, "Keyboard gained focus\n");
}

static void keyboard_leave_handler(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface)
{
    fprintf(stderr, "Keyboard lost focus\n");
}

static void keyboard_key_handler(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    fprintf(stderr, "Key is %d, state is %d\n", key, state);
}

static void keyboard_modifiers_handler(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
        uint32_t mods_locked, uint32_t group)
{
    fprintf(stderr, "Modifiers depressed %d, latched %d, locked %d, group %d\n",
        mods_depressed, mods_latched, mods_locked, group);
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap_handler,
    .enter = keyboard_enter_handler,
    .leave = keyboard_leave_handler,
    .key = keyboard_key_handler,
    .modifiers = keyboard_modifiers_handler,
};

//==============
// Pointer
//==============
static void paint_pixels2();

static void pointer_enter_handler(void *data, struct wl_pointer *pointer,
        uint32_t serial, struct wl_surface *surface,
        wl_fixed_t sx, wl_fixed_t sy)
{
    fprintf(stderr, "Pointer entered surface\t%p at %d %d\n", surface, sx, sy);

    struct wl_buffer *buffer;
    struct wl_cursor_image *image;

    image = default_cursor->images[0];
    buffer = wl_cursor_image_get_buffer(image);
    wl_pointer_set_cursor(pointer, serial, cursor_surface,
        image->hotspot_x, image->hotspot_y);
    wl_surface_attach(cursor_surface, buffer, 0, 0);
    wl_surface_damage(cursor_surface, 0, 0, image->width, image->height);
    wl_surface_commit(cursor_surface);

    wl_surface_attach(surface2, buffer, 0, 0);
    wl_surface_commit(surface2);
}

static void pointer_leave_handler(void *data, struct wl_pointer *pointer,
        uint32_t serial, struct wl_surface *surface)
{
    fprintf(stderr, "Pointer left surface\t%p\n", surface);
}

static void pointer_motion_handler(void *data, struct wl_pointer *pointer,
        uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
//    fprintf(stderr, "Pointer moved at %d %d\n", sx, sy);
//    if (sx > 49152 && sy < 2816) {
//        zxdg_surface_v6_destroy(xdg_surface);
//    }
}

static void pointer_button_handler(void *data, struct wl_pointer *wl_pointer,
        uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    fprintf(stderr, "Pointer button\n");
    if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        fprintf(stderr, "Move! wl_pointer: %p, xdg_toplevel: %p\n",
            wl_pointer, xdg_toplevel);
        xdg_toplevel_move(xdg_toplevel, seat, serial);
    }
}

static void pointer_axis_handler(void *data, struct wl_pointer *wl_pointer,
        uint32_t time, uint32_t axis, wl_fixed_t value)
{
    fprintf(stderr, "Pointer handle axis\n");
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter_handler,
    .leave = pointer_leave_handler,
    .motion = pointer_motion_handler,
    .button = pointer_button_handler,
    .axis = pointer_axis_handler,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
        enum wl_seat_capability caps)
{
    if (caps & WL_SEAT_CAPABILITY_POINTER) {
        printf("Display has a pointer.\n");
    }
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        printf("Display has a keyboard.\n");
        keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
        fprintf(stderr, "Destroy keyboard.\n");
        wl_keyboard_destroy(keyboard);
        keyboard = NULL;
    }
    if (caps & WL_SEAT_CAPABILITY_TOUCH) {
        printf("Display has a touch screen.\n");
    }

    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
        pointer  =wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer, &pointer_listener, NULL);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && pointer) {
        wl_pointer_destroy(pointer);
        pointer = NULL;
    }
}

static const struct wl_seat_listener seat_listener = {
    seat_handle_capabilities,
};

//============
// EGL
//============
static void paint_pixels();
static void paint_pixels2();
static void redraw();
static void redraw2();

static const struct wl_callback_listener frame_listener = {
    redraw,
};

static const struct wl_callback_listener frame_listener2 = {
    redraw2,
};

static void redraw(void *data, struct wl_callback *callback, uint32_t time)
{
    fprintf(stderr, "Redraw!\n");
    wl_callback_destroy(frame_callback);
    wl_surface_damage(surface, 0, 0, 480, 360);
//    paint_pixels();
    frame_callback = wl_surface_frame(surface);

//    wl_surface_attach(surface, buffer, 0, 0);
    wl_callback_add_listener(frame_callback, &frame_listener, NULL);
    wl_surface_commit(surface);
}

static void redraw2(void *data, struct wl_callback *callback, uint32_t time)
{
    fprintf(stderr, "Redraw2!\n");
    wl_callback_destroy(frame_callback2);
    wl_surface_damage(surface2, 0, 0, 480, 360);
    paint_pixels2();
    frame_callback2 = wl_surface_frame(surface2);

    wl_surface_attach(surface2, buffer, 0, 0);
    wl_callback_add_listener(frame_callback2, &frame_listener2, NULL);
    wl_surface_commit(surface2);
}

static void configure_callback(void *data, struct wl_callback *callback,
        uint32_t time)
{
//    fprintf(stderr, "configure_callback()\n");
//    if (callback == NULL) {
//        redraw(data, NULL, time);
//    }
}

static struct wl_callback_listener configure_callback_listener = {
    configure_callback,
};

static struct wl_buffer* create_buffer(int width, int height);

static void create_window()
{
    buffer = create_buffer(480, 360);

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
}

static void create_surface2()
{
    buffer = create_buffer(200, 200);

    wl_surface_attach(surface2, buffer, 0, 0);
    wl_surface_commit(surface2);
}

//==============
// Xdg
//==============

// Xdg wm base
static void ping_handler(void *data,
        struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
    fprintf(stderr, "Ping pong.\n");
}

const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = ping_handler,
};

// Xdg surface
static void xdg_surface_configure_handler(void *data,
        struct xdg_surface *xdg_surface, uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure_handler,
};

// Toplevel
static void xdg_toplevel_configure_handler(void *data,
        struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height,
        struct wl_array *states)
{
    printf("TOPLEVEL Configure: %dx%d\n", width, height);
}

static void xdg_toplevel_close_handler(void *data,
        struct xdg_toplevel *xdg_toplevel)
{
    printf("TOPLEVEL Close %p\n", xdg_toplevel);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure_handler,
    .close = xdg_toplevel_close_handler,
};

//==============
// Paint
//==============
uint32_t pixel_value= 0x000000;

static void paint_pixels()
{
    uint32_t *pixel = shm_data;

    for (int n = 0; n < 480 * 360; ++n) {
        *pixel++ = pixel_value;
    }

    pixel_value += 0x010101;

    if (pixel_value > 0xffffff) {
        pixel_value = 0x0;
    }
}

static void paint_pixels2()
{
    uint32_t *pixel = shm_data;

    for (int n = 0; n < 200 * 200; ++n) {
        *pixel++ = 0xff0000;
    }
}

static struct wl_buffer* create_buffer(int width, int height)
{
    struct wl_shm_pool *pool;
    int stride = width * 4;
    int size = stride * height;
    int fd;
    struct wl_buffer *buff;

    fd = os_create_anonymous_file(size);
    if (fd < 0) {
        fprintf(stderr, "Creating a buffer file for %d B failed: %m\n",
            size);
        exit(1);
    }

    shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_data == MAP_FAILED) {
        close(fd);
        exit(1);
    }

    pool = wl_shm_create_pool(shm, fd, size);
    buff = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
        WL_SHM_FORMAT_XRGB8888);

    wl_shm_pool_destroy(pool);
    return buff;
}

//==============
// Global
//==============
static void global_registry_handler(void *data, struct wl_registry *registry,
        uint32_t id, const char *interface, uint32_t version)
{
    (void)data;
    (void)version;

    if (strcmp(interface, "wl_seat") == 0) {
        seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
        wl_seat_add_listener(seat, &seat_listener, NULL);
    } else if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, id, &wl_compositor_interface,
            1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        xdg_wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
    } else if (strcmp(interface, "wl_shm") == 0) {
        shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
        cursor_theme = wl_cursor_theme_load("breeze_cursors", 24, shm);
        if (cursor_theme == NULL) {
            fprintf(stderr, "Can't get cursor theme.\n");
        } else {
            fprintf(stderr, "Got a cursor theme %p.\n", cursor_theme);
        }
        default_cursor = wl_cursor_theme_get_cursor(cursor_theme, "wait");
        if (default_cursor == NULL) {
            fprintf(stderr, "Can't get default cursor.\n");
            default_cursor = wl_cursor_theme_get_cursor(cursor_theme,
                "left_ptr");
            if (default_cursor == NULL) {
                exit(1);
            }
        }
    } else if (strcmp(interface, "wl_subcompositor") == 0) {
        subcompositor = wl_registry_bind(registry, id,
            &wl_subcompositor_interface, 1);
    } else {
        fprintf(stderr, "Interface <%s>\n", interface);
    }
}

static void global_registry_remover(void *data, struct wl_registry *registry,
        uint32_t id)
{
    printf("Got a registry losing event for <%d>\n", id);
}

static const struct wl_registry_listener registry_listener = {
    global_registry_handler,
    global_registry_remover
};

//===========
// Main
//===========
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

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
    }
    if (subcompositor == NULL) {
        fprintf(stderr, "Can't find subcompositor.\n");
        exit(1);
    }

    // Check surface.
    surface = wl_compositor_create_surface(compositor);
    if (surface == NULL) {
        exit(1);
    } else {
        fprintf(stderr, " = surface:     %p\n", surface);
    }

    xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, NULL);

    // Check xdg surface.
    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
    if (xdg_surface == NULL) {
        exit(1);
    } else {
        fprintf(stderr, " = xdg_surface: %p\n", xdg_surface);
    }

    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

    xdg_toplevel =
        xdg_surface_get_toplevel(xdg_surface);
    if (xdg_toplevel == NULL) {
        exit(1);
    }
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

    wl_surface_commit(surface);

    // Wait for the surface to be configured.
    wl_display_roundtrip(display);


    surface2 = wl_compositor_create_surface(compositor);
    if (surface2 == NULL) {
        exit(1);
    } else {
        fprintf(stderr, "surface2 created: %p\n", surface2);
    }
    create_surface2();
    paint_pixels2();
    wl_surface_attach(surface2, buffer, 0, 0);
    wl_surface_commit(surface2);

    // Subsurface
    subsurface = wl_subcompositor_get_subsurface(subcompositor,
        surface2, surface);

//    callback = wl_display_sync(display);
//    wl_callback_add_listener(callback, &configure_callback_listener, NULL);
//    frame_callback = wl_surface_frame(surface);
//    wl_callback_add_listener(frame_callback, &frame_listener, NULL);

    frame_callback2 = wl_surface_frame(surface2);
    wl_callback_add_listener(frame_callback2, &frame_listener2, NULL);

    wl_surface_attach(surface2, buffer, 0, 0);
    wl_surface_commit(surface2);

    region2 = wl_compositor_create_region(compositor);
    wl_region_add(region2, 0, 0, 100, 50);
    wl_surface_set_input_region(surface2, region2);


    create_window();
//    create_surface2();
    paint_pixels();
//    redraw(NULL, NULL, 0);
//    redraw2(NULL, NULL, 0);

    cursor_surface = wl_compositor_create_surface(compositor);



    wl_surface_commit(surface);

    while (wl_display_dispatch(display) != -1) {
        ;
    }

    wl_display_disconnect(display);
    printf("Disconnected from display.\n");

    return 0;
}
