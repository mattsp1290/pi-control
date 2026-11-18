# Pi Control

C++20 fullscreen control screen for Raspberry Pi 4, targeting **64-bit
Raspberry Pi OS Lite (Bookworm or newer)** with systemd, full KMS, HDMI/DSI
screen and a mouse or touchscreen. Qt 6 Widgets handles display and input.

Three tabs run along the top:

- **Home**: the time and date, each network interface's IPv4 address, the
  Wi‑Fi network and signal strength, and CPU, memory and storage use
  (refreshed every 2 s).
- **Example**: placeholder text showing how to add a tab. See
  [Add a tab](docs/tabs.md).
- **Settings**, top to bottom:
  - **Wi‑Fi**: scan for networks, join one with the on-screen keyboard, and
    forget saved networks you are not connected to.
  - **Screensaver**: choose the screensaver and preview it.
  - **Software update**: the running version and, when a newer release is
    published, an **Update** button that installs it and restarts the screen.
    See [Update from the screen](#update-from-the-screen).
  - **Power**: Shut down, which requests an orderly poweroff through
    systemd-logind. A failure stays on screen and the button works again.

## Wi‑Fi

Wi‑Fi uses NetworkManager over D-Bus, the default on Raspberry Pi OS Bookworm.
Without NetworkManager the Settings section and Home say why Wi‑Fi is
unavailable; everything else keeps working. The package does not install
NetworkManager.

Open and WPA/WPA2/WPA3-Personal networks are supported; Enterprise and WEP
networks are listed but cannot be joined. A new network is saved only after
the Pi has joined it: a wrong password leaves nothing saved, and the Pi goes
back to the network it was on before. The active network is never offered for
Forget, so the kiosk cannot strand itself offline from the touchscreen.

**Changing Wi‑Fi from the screen can drop your SSH session** if SSH runs over
that Wi‑Fi link. Keep Ethernet or physical access when testing.

The packaged polkit rule lets the `pi-control` account use exactly three
NetworkManager actions: `wifi.scan`, `network-control` and
`settings.modify.system`. Passwords are never logged.

To try the interface without touching real Wi‑Fi, use canned networks (the
secured ones accept `password123`):

```sh
./build/pi-control --dry-run --fake-wifi
```

## Build and test

On the Pi (or Debian desktop for development):

```sh
sudo apt update
sudo apt install git build-essential cmake ninja-build qt6-base-dev qt6-qpa-plugins libegl1 libgles2 libgbm1 libinput10 libqt6gui6 polkitd
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
off the machine when Shut down is pressed, and really changes Wi‑Fi. Use
`--dry-run --fake-wifi` for desktop testing.

The service holds the Pi's display. To try a development build on the
touchscreen, stop the service first and start it again afterwards:

```sh
sudo systemctl stop pi-control
QT_QPA_PLATFORM=eglfs QT_QPA_EGLFS_INTEGRATION=eglfs_kms QT_QPA_EGLFS_KMS_ATOMIC=1 \
  ./build/pi-control --dry-run --fake-wifi
# Ctrl+C to stop, then:
sudo systemctl start pi-control
```

## Screensaver

After 15 seconds without input, a screensaver covers the screen: Conway's
Game of Life (the default), a Mandelbrot zoom, or rain on water. Choose it in
**Settings → Screensaver**; **Preview** shows it at once. The choice is saved
in `/var/lib/pi-control/settings.ini` and survives restarts. Any touch, click,
mouse movement or key press dismisses the screensaver without pressing
anything underneath.

To use one screensaver for a single run without saving it:

```sh
./build/pi-control --dry-run --screensaver mandelbrot
```

`pi-control --help` lists the screensaver ids. It still starts after 15
seconds idle. See [Screensavers](docs/screensavers.md) for the architecture
and how to add one.

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
shutdown, Wi‑Fi and update authorization rule and the updater unit, enables `pi-control.service` for every boot,
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

### Update from the screen

Settings → Software update shows the running version. Pi Control checks
GitHub's latest release 30 seconds after it starts and every six hours after
that; **Check for updates** checks at once. When the published release is newer
than the running version, an **Update to X.Y.Z** button appears.

Pressing it starts `pi-control-update.service`, a packaged oneshot unit that
runs the same installer as above (shipped as `/usr/lib/pi-control/install.sh`)
as root with `PI_CONTROL_INSTALL_UPGRADE_ONLY=1`. In that mode the installer
does nothing when the latest release is not newer than the installed package,
and never downgrades. The package upgrade restarts `pi-control.service`, so the
screen goes blank for a few seconds and comes back on the new version. If the
update fails, or reports nothing within 21 minutes, the screen says so and the
button works again; the reason is in `journalctl -u pi-control-update`. While
the update installs, Shut down is disabled and the unit holds a logind
shutdown inhibitor, so a poweroff cannot interrupt apt partway through.

The kiosk account cannot pass anything to the updater: the polkit rule lets it
*start* exactly `pi-control-update.service` and nothing else, and the unit only
installs the latest release, verified as described above. The update check
itself is read-only and runs as the kiosk account. With `--dry-run` the button
is simulated and nothing is started. Packages older than the first release with
this feature have no updater unit; update those once with the installer.

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
SSH sessions exist, but never inhibitor bypass, plus the three Wi‑Fi actions
above. Its only writable directory is `/var/lib/pi-control`, created by
systemd (`StateDirectory=`). No sudo password or root GUI
is needed. `NoNewPrivileges` does not prevent the system-bus authorization
performed by logind/polkit.

Service output goes to the journal. Inspect `journalctl -u pi-control -b` if startup or shutdown fails. After fixing
a problem that exhausted the startup retry limit, clear the failure and restart:

```sh
sudo systemctl reset-failed pi-control.service
sudo systemctl restart pi-control.service
```

Verify the
screen and touch input on the actual Pi, then reboot to verify autostart and
press Settings → Shut down to verify orderly poweroff. These hardware checks cannot be
proven by the offscreen test. Display output/card selection can be customized
with Qt's `QT_QPA_EGLFS_KMS_CONFIG` through a systemd override if needed.

To remove a packaged install, run `sudo apt remove pi-control`. This stops and
disables the service and removes its binary, unit, and authorization rule; the
system account, saved settings and local systemd overrides are retained.
`sudo apt purge pi-control` also deletes `/var/lib/pi-control`. Qt is LGPL/GPL or
commercial licensed; review distribution obligations when shipping images.

## Release and installer verification

Run the installer boundary tests without root or a running service:

```sh
python3 -m unittest discover -s distribution/tests -v
```

The release workflow builds and tests natively on ARM64 in Debian Bookworm on
every pull request and release tag, then packages the binary, systemd unit, and polkit rule. It derives runtime
library dependencies with `dpkg-shlibdeps`. To build a local package (requires
`dpkg-dev` and `file` in addition to the build dependencies):

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
