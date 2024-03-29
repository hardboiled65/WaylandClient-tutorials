WAYLAND_PROTOCOLS_UNSTABLE_DIR=/usr/share/wayland-protocols/unstable
WAYLAND_PROTOCOLS_UNSTABLE_TARGET_DIR=wayland-protocols/unstable

WAYLAND_PROTOCOLS_STABLE_DIR=/usr/share/wayland-protocols/stable
WAYLAND_PROTOCOLS_STABLE_TARGET_DIR=wayland-protocols/stable
WAYLAND_PROTOCOLS_TARGET_DIR=wayland-protocols

XDG_SHELL_HEADER=xdg-shell.h
XDG_SHELL_SOURCE=xdg-shell.c
XDG_SHELL_HEADER_PATH=$(WAYLAND_PROTOCOLS_STABLE_TARGET_DIR)/$(XDG_SHELL_HEADER)
XDG_SHELL_SOURCE_PATH=$(WAYLAND_PROTOCOLS_STABLE_TARGET_DIR)/$(XDG_SHELL_SOURCE)

BLUSHER_COLLECTIONS_LIB=blusher-collections/target/release/libblusher_collections.so

CFLAGS += -Lblusher-collections/target/release -lblusher_collections

C_INCLUDES := -I./$(WAYLAND_PROTOCOLS_TARGET_DIR) -I./blusher-collections/include

PKG_CONFIGS = `pkg-config --cflags --libs cairo` `pkg-config --cflags --libs pango` `pkg-config --cflags --libs pangocairo`

SRC = application.c \
	window.c \
	title-bar.c \
	surface.c \
	utils.c \
	color.c \
	label.c \
	pointer-event.c \
	main.c

default: $(XDG_SHELL_HEADER_PATH) $(XDG_SHELL_SOURCE_PATH) $(BLUSHER_COLLECTIONS_LIB)
	gcc $(C_INCLUDES) $(CFLAGS) $(PKG_CONFIGS) -lwayland-client -lwayland-egl -lwayland-cursor $(SRC) $(XDG_SHELL_SOURCE_PATH)

$(XDG_SHELL_HEADER_PATH):
	wayland-scanner client-header $(WAYLAND_PROTOCOLS_STABLE_DIR)/xdg-shell/xdg-shell.xml $(WAYLAND_PROTOCOLS_STABLE_TARGET_DIR)/$(XDG_SHELL_HEADER)

$(XDG_SHELL_SOURCE_PATH):
	wayland-scanner public-code $(WAYLAND_PROTOCOLS_STABLE_DIR)/xdg-shell/xdg-shell.xml $(WAYLAND_PROTOCOLS_STABLE_TARGET_DIR)/$(XDG_SHELL_SOURCE)

$(BLUSHER_COLLECTIONS_LIB):
	cd blusher-collections ; cargo build --release

run:
	LD_LIBRARY_PATH=blusher-collections/target/release ./a.out
