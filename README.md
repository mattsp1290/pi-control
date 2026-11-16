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

## Install, update, and start on boot

Use **64-bit Raspberry Pi OS Lite Bookworm or newer** with systemd and no
desktop compositor/display manager competing for DRM. Full KMS
(`dtoverlay=vc4-kms-v3d`) must be enabled in `/boot/firmware/config.txt`.
Do not enable legacy framebuffer or FKMS modes. Keep SSH available during
initial hardware commissioning. The installer starts the real shutdown UI.

Once a release has been published, install the latest release on the Pi:

```sh
sudo apt update
sudo apt install curl ca-certificates python3
pi_control_installer=$(mktemp)
curl -fsSL https://github.com/mattsp1290/pi-control/releases/latest/download/install.sh -o "$pi_control_installer"
sudo sh "$pi_control_installer"
rm -f "$pi_control_installer"
```

Run the same commands to update. The installer downloads the ARM64 Debian
package, verifies its SHA-256 against the release manifest and checks its
package identity before apt installs it and its runtime dependencies. It
requires root, ARM64 Debian/Raspberry Pi OS, and a running systemd system.
No source checkout, compiler, or Qt development packages are needed on the Pi.
Concurrent installer runs are rejected.

The package creates a dedicated `pi-control` system account, installs the
shutdown authorization rule, enables `pi-control.service` for every boot,
and restarts the service on install, reinstall, or upgrade. An existing
`pi-control` account must have a system UID (1–999) and a nologin shell.
The installer checks that the service remains active after five seconds;
verify display and input on the physical Pi as described below.

To install a specific release or roll back, download that release's installer
and set `PI_CONTROL_INSTALL_VERSION` (explicit pins permit downgrades):

```sh
pi_control_installer=$(mktemp)
curl -fsSL https://github.com/mattsp1290/pi-control/releases/download/v0.1.0/install.sh -o "$pi_control_installer"
sudo env PI_CONTROL_INSTALL_VERSION=v0.1.0 sh "$pi_control_installer"
rm -f "$pi_control_installer"
```

Without a pin, updates use GitHub's latest published release. Checksums detect
corrupt downloads; the release account and HTTPS are the trust boundary. A
download or checksum failure leaves the installed package untouched. An apt
or service failure is reported with a nonzero exit status; there is no
automatic rollback. Fix the reported cause and rerun the installer. Systemd
overrides in `/etc/systemd/system/pi-control.service.d/` survive updates.

For an existing manual installation, stop the service and remove the old unit
and polkit rule before running the installer; these would override the packaged
files. Remove the old binary after the package has installed successfully:

```sh
sudo systemctl disable --now pi-control.service
sudo rm -f /etc/systemd/system/pi-control.service /etc/polkit-1/rules.d/49-pi-control.rules
sudo systemctl daemon-reload
# Run the installer above, then:
sudo rm -f /usr/local/bin/pi-control /etc/polkit-1/rules.d/49-pi-control.rules
```

The service runs as the dedicated account with the device groups needed for
graphics and input; the polkit rule permits poweroff requests including when
SSH sessions exist, but never inhibitor bypass. No sudo password or root GUI
is needed. `NoNewPrivileges` does not prevent the system-bus authorization
performed by logind/polkit.

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

To remove a packaged install, run `sudo apt remove pi-control`. This stops and
disables the service and removes its binary, unit, and authorization rule; the
system account and local systemd overrides are retained. Qt is LGPL/GPL or
commercial licensed; review distribution obligations when shipping images.

## Release and installer verification

Run the installer boundary tests without root or a running service:

```sh
python3 -m unittest discover -s distribution/tests -v
```

The release workflow builds and tests natively on ARM64 in Debian Bookworm,
then packages the binary, systemd unit, and polkit rule. It derives runtime
library dependencies with `dpkg-shlibdeps`. To build a local package (requires
`dpkg-dev` in addition to the build dependencies):

```sh
cmake -S . -B build-package -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DPI_CONTROL_PACKAGE=ON
cmake --build build-package
ctest --test-dir build-package --output-on-failure
(cd build-package && cpack)
```

A development package built on a newer distribution may require newer runtime
libraries; publish only the Bookworm workflow output. To publish, update
`project(... VERSION X.Y.Z ...)` in `CMakeLists.txt`, commit, and push a matching
`vX.Y.Z` tag. The workflow rejects a tag/package version mismatch and publishes
`pi-control_X.Y.Z_arm64.deb`, `pi-control-manifest.json`, and `install.sh`.
The one-command install path becomes available with the first such release.
See [UI decision](docs/ui-decision.md) for research and tradeoffs.
