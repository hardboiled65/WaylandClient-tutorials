PKG_CONFIG=`pkg-config --cflags --libs cairo`
default:
	wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.h
	wayland-scanner public-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.c
	gcc main.c xdg-shell.c -lwayland-client -lwayland-egl -lEGL -lGL -lGLEW $(PKG_CONFIG)
