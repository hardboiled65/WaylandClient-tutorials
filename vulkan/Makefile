PKG_CONFIG=`pkg-config --cflags --libs cairo`
default:
	wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.h
	wayland-scanner public-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.c
	$(CC) -O2 main.c xdg-shell.c -lwayland-client -lvulkan $(PKG_CONFIG)

shaders:
	glslc shader.vert -o vert.spv
	glslc shader.frag -o frag.spv

