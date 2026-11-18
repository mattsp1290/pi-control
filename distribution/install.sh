#!/bin/sh
# Install or update the latest release; PI_CONTROL_INSTALL_VERSION pins vX.Y.Z.
# PI_CONTROL_INSTALL_UPGRADE_ONLY=1 (set by pi-control-update.service) installs
# the latest release only when it is newer than the installed package.
set -eu

fail() {
  printf 'install.sh: %s\n' "$1" >&2
  exit 1
}

fetch() {
  curl -q -fsSL --globoff --proto '=https' --proto-redir '=https' \
    --connect-timeout 10 --max-time 300 --retry 3 --retry-max-time 300 \
    --url "$1" -o "$2"
}

main() {
  [ "$#" -eq 0 ] || fail 'usage: sh install.sh (configure with environment variables)'
  [ "$(id -u)" -eq 0 ] || fail 'run as root (sudo sh install.sh)'
  [ "$(uname -s)" = Linux ] || fail 'Linux is required'
  command -v dpkg >/dev/null || fail 'Debian/Raspberry Pi OS is required'
  [ "$(dpkg --print-architecture)" = arm64 ] || fail '64-bit ARM Raspberry Pi OS is required'
  [ -d /run/systemd/system ] || fail 'a running systemd system is required'
  for cmd in curl python3 sha256sum apt-get flock dpkg-deb dpkg-query; do
    command -v "$cmd" >/dev/null || fail "$cmd is required"
  done
  # Serialize downloads and apt operations from concurrent installer runs.
  exec 9>/run/lock/pi-control-install.lock
  flock -n 9 || fail 'another pi-control install is running'

  version=${PI_CONTROL_INSTALL_VERSION:-}
  upgrade_only=${PI_CONTROL_INSTALL_UPGRADE_ONLY:-}
  case $upgrade_only in
    ''|1) ;;
    *) fail 'PI_CONTROL_INSTALL_UPGRADE_ONLY must be 1 or unset' ;;
  esac
  [ -z "$upgrade_only" ] || [ -z "$version" ] ||
    fail 'PI_CONTROL_INSTALL_UPGRADE_ONLY cannot be combined with PI_CONTROL_INSTALL_VERSION'
  if [ -n "$version" ]; then
    printf '%s\n' "$version" | python3 -c '
import re, sys
sys.exit(not re.fullmatch(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\n", sys.stdin.read()))
' || fail 'PI_CONTROL_INSTALL_VERSION must look like v1.2.3'
    release="download/$version"
  else
    release=latest/download
  fi
  base=https://github.com/mattsp1290/pi-control/releases
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' EXIT
  trap 'exit 1' HUP INT TERM
  fetch "$base/$release/pi-control-manifest.json" "$tmp/manifest.json"
  python3 - "$tmp/manifest.json" "$version" >"$tmp/asset" <<'PY'
import json
import re
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    manifest = json.load(stream)
version = manifest["version"]
if manifest["schema"] != 1 or not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
    sys.exit("invalid release manifest version/schema")
if sys.argv[2] and sys.argv[2] != "v" + version:
    sys.exit("manifest does not match requested version")
digest = manifest["sha256"]["linux-aarch64"]
if not re.fullmatch(r"[0-9a-f]{64}", digest):
    sys.exit("invalid release checksum")
print(version)
print(digest)
PY
  version=$(sed -n '1p' "$tmp/asset")
  digest=$(sed -n '2p' "$tmp/asset")
  if [ -n "$upgrade_only" ]; then
    installed=$(dpkg-query -W -f '${db:Status-Status} ${Version}' pi-control 2>/dev/null || true)
    case $installed in
      'installed '*)
        installed=${installed#installed }
        if dpkg --compare-versions "$version" le "$installed"; then
          printf 'pi-control %s is up to date (latest release v%s).\n' "$installed" "$version"
          return 0
        fi
        ;;
    esac
  fi
  package="pi-control_${version}_arm64.deb"
  fetch "$base/download/v$version/$package" "$tmp/$package"
  printf '%s  %s\n' "$digest" "$tmp/$package" | sha256sum -c -
  [ "$(dpkg-deb -f "$tmp/$package" Package)" = pi-control ] || fail 'unexpected package name'
  [ "$(dpkg-deb -f "$tmp/$package" Version)" = "$version" ] || fail 'unexpected package version'
  [ "$(dpkg-deb -f "$tmp/$package" Architecture)" = arm64 ] || fail 'unexpected package architecture'
  # Avoid overlapping a previous manual /usr/local deployment.
  { [ ! -e /etc/systemd/system/pi-control.service ] && [ ! -L /etc/systemd/system/pi-control.service ]; } ||
    fail 'remove the old manual /etc/systemd/system/pi-control.service first; see README'
  apt-get update
  if [ -n "${PI_CONTROL_INSTALL_VERSION:-}" ]; then
    apt-get install -y --reinstall --allow-downgrades "$tmp/$package"
  else
    apt-get install -y --reinstall "$tmp/$package"
  fi
  systemctl is-enabled --quiet pi-control.service
  sleep 5
  systemctl is-active --quiet pi-control.service ||
    fail 'service failed: inspect journalctl -u pi-control -b, fix the cause, then rerun the installer'
  printf 'Installed pi-control v%s; service enabled and running.\n' "$version"
}

main "$@"
