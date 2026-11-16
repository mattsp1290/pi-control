# Pi Control

C++20 fullscreen shutdown appliance for Raspberry Pi 4, targeting **64-bit
Raspberry Pi OS Lite (Bookworm or newer)** with systemd, full KMS, HDMI/DSI
screen and a mouse or touchscreen. One button requests an orderly shutdown
through systemd-logind. Failure stays on screen and allows retry; requests
in progress disable the button. Qt 6 Widgets handles display and input.

## Build and test

On the Pi (or Debian desktop for development):

```sh
sudo apt update
sudo apt install git build-essential cmake ninja-build qt6-base-dev qt6-qpa-plugins libegl1 libgles2 libgbm1 libinput10 libqt6gui6 policykit-1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/pi-control --dry-run
```

CMake 3.22+, Qt 6.4+, and C++20 are required. CPM 0.42.0 is vendored;
it fetches pinned fmt 11.1.4 on first configure (internet required). Qt uses
OS packages to preserve the Pi graphics integration. The host build is useful
for tests; build natively on the Pi for deployment. `--smoke-test` exercises
the actual button using dry-run logic and exits. Normal launch really powers
off the machine when clicked. Do not use normal launch for desktop testing.

## Start on boot

Use Lite with no desktop compositor/display manager competing for DRM. Full
KMS (`dtoverlay=vc4-kms-v3d`) must be enabled in `/boot/firmware/config.txt`.
Do not enable legacy framebuffer or FKMS modes. Keep SSH available during
initial hardware commissioning.

```sh
sudo cmake --install build
sudo useradd --system --user-group --no-create-home --shell /usr/sbin/nologin pi-control
sudo install -m 0644 deploy/49-pi-control.rules /etc/polkit-1/rules.d/49-pi-control.rules
sudo install -m 0644 deploy/pi-control.service /etc/systemd/system/pi-control.service
sudo systemctl daemon-reload
sudo systemctl enable --now pi-control.service
```

Skip `useradd` if that dedicated account already exists. The service expects
the default `/usr/local` install prefix. It runs as that account with only
the device groups needed for graphics and input; the polkit rule permits its
poweroff requests including when SSH sessions exist, but never inhibitor
bypass. No sudo password or root GUI is needed. `NoNewPrivileges` does not
prevent the system-bus authorization performed by logind/polkit.

Inspect `journalctl -u pi-control -b` if startup or shutdown fails. After fixing
a problem that exhausted the startup retry limit, clear the failure and restart:

```sh
sudo systemctl reset-failed pi-control.service
sudo systemctl restart pi-control.service
```

Verify the
screen and touch input on the actual Pi, then reboot to verify autostart and
press the button to verify orderly poweroff. These hardware checks cannot be
proven by the offscreen test. Display output/card selection can be customized
with Qt's `QT_QPA_EGLFS_KMS_CONFIG` through a systemd override if needed.

To remove: `sudo systemctl disable --now pi-control`, remove the installed
unit and polkit rule, then `sudo systemctl daemon-reload`. Qt is LGPL/GPL or
commercial licensed; review distribution obligations when shipping images.
See [UI decision](docs/ui-decision.md) for research and tradeoffs.
