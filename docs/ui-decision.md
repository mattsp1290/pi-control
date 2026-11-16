# UI decision — 2026-11-15

Recommendation: Qt 6 Widgets. Three independent advocates debated Qt, LVGL,
and Dear ImGui/SDL. The Qt and ImGui advocates favored Qt; the LVGL advocate
favored LVGL narrowly for its appliance footprint and CPM source integration.

| Option | Strength | Cost for this application |
| --- | --- | --- |
| Qt 6 Widgets | Native C++, event-driven button, input/font/accessibility support, documented Pi 4 EGLFS/KMS | Larger distro dependency; LGPL/commercial distribution obligations |
| LVGL | MIT, retained embedded widgets, CMake/CPM fit, DRM software rendering | C API in C++ application; explicit driver/input/timer setup |
| Dear ImGui + SDL | MIT, small C++ source, official SDL backends | Developer-tool focus; no accessibility; platform/render loop ownership |

The deciding inference is maintenance simplicity on a Pi 4, whose resources
comfortably support this small Qt application. CPM preference is honored for
fmt; Qt comes from the OS to match graphics packages. Choose LVGL instead if
minimal images or sourcing the entire UI through CPM becomes mandatory.

Primary research:

- [Qt embedded platforms and Widgets/EGLFS](https://doc.qt.io/qt-6/embedded-linux.html)
- [Qt device configuration, including Pi 4 DRM/GBM](https://doc.qt.io/qt-6/configure-linux-device.html)
- [Qt CMake integration](https://doc.qt.io/qt-6/cmake-get-started.html)
- [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- [LVGL source, license, and C++ compatibility](https://github.com/lvgl/lvgl)
- [LVGL DRM driver](https://lvgl.io/docs/open/integration/embedded_linux/drivers/drm)
- [LVGL evdev input](https://lvgl.io/docs/open/integration/embedded_linux/drivers/evdev)
- [Dear ImGui scope and limitations](https://github.com/ocornut/imgui#the-pitch)
- [Official SDL renderer example](https://github.com/ocornut/imgui/blob/master/examples/example_sdl2_sdlrenderer2/main.cpp)

Hardware acceptance remains essential: the offscreen test establishes button
wiring and safe simulation, not DRM ownership, touchscreen calibration, boot
ordering, polkit integration, or actual poweroff on the installed OS image.
