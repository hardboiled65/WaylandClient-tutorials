#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "xdg-shell.h"

struct wl_display *display = NULL;
struct wl_compositor *compositor = NULL;
struct wl_surface *surface;
struct xdg_wm_base *xdg_wm_base;
struct xdg_surface *xdg_surface;
struct wl_egl_window *egl_window;
struct wl_region *region;

EGLDisplay egl_display;
EGLConfig egl_conf;
EGLSurface egl_surface;
EGLContext egl_context;

struct wl_surface *surface2;
struct xdg_surface *xdg_surface2;
struct wl_egl_window *egl_window2;

EGLSurface egl_surface2;

//==============
// Xdg
//==============
void xdg_toplevel_configure_handler(void *data,
        struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height,
        struct wl_array *states)
{
    printf("Configure: %dx%d\n", width, height);
}

void xdg_toplevel_close_handler(void *data,
        struct xdg_toplevel *xdg_toplevel)
{
    printf("Close.\n");
}

const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure_handler,
    .close = xdg_toplevel_close_handler,
};

void xdg_surface_configure_handler(void *data,
        struct xdg_surface *xdg_surface, uint32_t serial)
{
    fprintf(stderr, " = xdg_surface_configure_handler(). serial: %d\n",
        serial);
    xdg_surface_ack_configure(xdg_surface, serial);
}

const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure_handler,
};

void xdg_wm_base_ping_handler(void *data, struct xdg_wm_base *xdg_wm_base,
        uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
    printf("Pong!\n");
}

const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping_handler,
};

//==============
// Global
//==============
static void global_registry_handler(void *data, struct wl_registry *registry,
        uint32_t id, const char *interface, uint32_t version)
{
    if (strcmp(interface, "wl_compositor") == 0) {
        fprintf(stderr, "Interface is <wl_compositor>.\n");
        compositor = wl_registry_bind(
            registry,
            id,
            &wl_compositor_interface,
            version
        );
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        fprintf(stderr, "Interface is <xdg_wm_base>.\n");
        xdg_wm_base = wl_registry_bind(
            registry, id, &xdg_wm_base_interface, 1);
    }
}

static void global_registry_remover(void *data, struct wl_registry *registry,
        uint32_t id)
{
    printf("Got a registry losing event for <%d>\n", id);
}

static const struct wl_registry_listener registry_listener = {
    .global = global_registry_handler,
    .global_remove = global_registry_remover,
};

//==============
// EGL
//==============

static void create_opaque_region()
{
    fprintf(stderr, " = Begin create_opaque_region()\n");

    region = wl_compositor_create_region(compositor);
    wl_region_add(
        region,
        0,
        0,
        480,
        360
    );
    wl_surface_set_opaque_region(surface, region);

    fprintf(stderr, " = End create_opaque_region()\n");
}

static void create_window()
{
    fprintf(stderr, " = Begin create_window()\n");
    egl_window = wl_egl_window_create(surface, 480, 360);

    if (egl_window == EGL_NO_SURFACE) {
        exit(1);
    }

    egl_surface =
        eglCreateWindowSurface(egl_display, egl_conf, egl_window, NULL);
    if (egl_surface == NULL) {
        fprintf(stderr, "Can't create EGL window surface.\n");
    }

    if (eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "Made current.\n");
    } else {
        fprintf(stderr, "Made current failed!\n");
    }

    if (eglSwapBuffers(egl_display, egl_surface)) {
        fprintf(stderr, "Swapped buffers.\n");
    } else {
        fprintf(stderr, "Swapped buffers failed!\n");
    }
    fprintf(stderr, " = End create_window()\n");

    // 2nd.
    egl_window2 = wl_egl_window_create(surface2, 200, 300);

    egl_surface2 =
        eglCreateWindowSurface(egl_display, egl_conf, egl_window2, NULL);

    eglMakeCurrent(egl_display, egl_surface2, egl_surface2, egl_context);
    eglSwapBuffers(egl_display, egl_surface2);
}

static void init_egl()
{
    fprintf(stderr, " = Begin init_egl()\n");
    EGLint major, minor, count ,n, size;
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
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES2_BIT,
        EGL_NONE,
    };

    static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION,
        2,
        EGL_NONE,
    };

    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY) {
        exit(1);
    }

    if (eglInitialize(egl_display, &major, &minor) != EGL_TRUE) {
        exit(1);
    }
    printf("EGL major: %d, minor: %d.\n", major, minor);

    eglGetConfigs(egl_display, NULL, 0, &count);
    printf("EGL has %d configs.\n", count);

    configs = calloc(count, sizeof *configs);

    eglChooseConfig(egl_display, config_attribs, configs, count, &n);

    for (int i = 0; i < n; ++i) {
        eglGetConfigAttrib(
            egl_display,
            configs[i],
            EGL_BUFFER_SIZE,
            &size
        );
        printf("Buffer size for config %d is %d.\n", i, size);

        // Just choose the first one
        egl_conf = configs[i];
        break;
    }

    egl_context = eglCreateContext(
        egl_display,
        egl_conf,
        EGL_NO_CONTEXT,
        context_attribs
    );
    if (egl_context == NULL) {
        fprintf(stderr, "Failed create EGL context.\n");
        exit(1);
    }

    fprintf(stderr, " = End init_egl()\n");
}

static void get_server_references()
{
    display = wl_display_connect(NULL);
    if (display == NULL) {
        exit(1);
    }
    printf("Connected to display.\n");

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    if (compositor == NULL || xdg_wm_base == NULL) {
        fprintf(stderr, "Can't find compositor or xdg_wm_base.\n");
    }
}

//==============
// Main
//==============
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    get_server_references();

    // Check surface.
    fprintf(stderr, " - Checking surface...\n");
    surface = wl_compositor_create_surface(compositor);
    if (surface == NULL) {
        fprintf(stderr, "Can't create surface.\n");
        exit(1);
    } else {
        fprintf(stderr, "Created surface!\n");
    }
    // 2nd.
    surface2 = wl_compositor_create_surface(compositor);

    xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, NULL);

    // Check XDG surface.
    fprintf(stderr, " - Checking xdg surface...\n");
    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
    if (xdg_surface == NULL) {
        fprintf(stderr, "Can't create xdg surface.\n");
        exit(1);
    } else {
        fprintf(stderr, "Created xdg surface!\n");
    }
    xdg_surface_add_listener(
        xdg_surface, &xdg_surface_listener, NULL);
    // 2nd.
    xdg_surface2 = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface2);
    xdg_surface_add_listener(
        xdg_surface2, &xdg_surface_listener, NULL);

    // Set XDG toplevel.
    struct xdg_toplevel *xdg_toplevel =
        xdg_surface_get_toplevel(xdg_surface);
    if (xdg_toplevel == NULL) {
        fprintf(stderr, "Can't get xdg_surface toplevel.\n");
        exit(1);
    } else {
        fprintf(stderr, "Got xdg_toplevel.\n");
    }
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
    // 2nd.
    struct xdg_toplevel *xdg_toplevel2 =
        xdg_surface_get_toplevel(xdg_surface2);
    xdg_toplevel_add_listener(xdg_toplevel2, &xdg_toplevel_listener, NULL);

    wl_surface_commit(surface);
    wl_surface_commit(surface2);

    // wait for the surface to be configured
    wl_display_roundtrip(display);

    create_opaque_region();
    init_egl();
    create_window();

    wl_surface_commit(surface);
    wl_surface_commit(surface2);

    while (wl_display_dispatch(display) != -1) {
        ;
    }

    wl_display_disconnect(display);
    printf("Disconnected from display.\n");

    return 0;
}
