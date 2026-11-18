"""Exercise installer boundaries without apt, root access, or a running service."""
import hashlib
import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SHIM = r'''#!/usr/bin/env python3
import json, os, pathlib, shutil, sys
tool = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
root = pathlib.Path(os.environ["FIXTURE"])
with (root / "calls").open("a") as stream:
    stream.write(json.dumps([tool, *args]) + "\n")
if tool == "id":
    print("123" if "pi-control" in args else "0")
elif tool == "uname":
    print("Linux")
elif tool == "dpkg" and args[0] == "--compare-versions":
    def key(version):
        return tuple(int(part) for part in version.split("."))
    left, op, right = args[1:]
    assert op == "le", op
    sys.exit(0 if key(left) <= key(right) else 1)
elif tool == "dpkg":
    print(os.environ.get("TEST_ARCH", "arm64"))
elif tool == "dpkg-query":
    installed = os.environ.get("TEST_INSTALLED")
    if not installed:
        sys.exit(1)
    print(installed, end="")
elif tool == "curl":
    url = args[args.index("--url") + 1]
    source = "manifest.json" if url.endswith(".json") else "pi-control_0.1.0_arm64.deb"
    shutil.copyfile(root / source, args[args.index("-o") + 1])
elif tool == "getent":
    if os.environ.get("TEST_FRESH_SYSTEM") and not (root / ("created-" + args[-1])).exists():
        sys.exit(2)
    if args[:2] == ["passwd", "pi-control"]:
        print("pi-control:x:123:123::/nonexistent:" + os.environ.get("TEST_SHELL", "/usr/sbin/nologin"))
    else:
        print(args[-1] + ":x:44:")
elif tool in ("adduser", "addgroup"):
    (root / ("created-" + args[-1])).touch()
elif tool == "systemctl" and "is-active" in args:
    sys.exit(int(os.environ.get("TEST_INACTIVE", "0")))
elif tool == "apt-get":
    sys.exit(int(os.environ.get("TEST_APT_FAILURE", "0")))
'''


class DistributionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        shim = self.bin / "shim"
        shim.write_text(SHIM)
        shim.chmod(0o755)
        for tool in ("id", "uname", "dpkg", "dpkg-query", "curl", "apt-get", "systemctl", "sleep", "getent", "adduser", "addgroup"):
            (self.bin / tool).symlink_to(shim)
        control = self.root / "package/DEBIAN"
        control.mkdir(parents=True)
        (control / "control").write_text(
            "Package: pi-control\nVersion: 0.1.0\nArchitecture: arm64\n"
            "Maintainer: Test <test@example.com>\nDescription: Fixture\n")
        self.package = self.root / "pi-control_0.1.0_arm64.deb"
        subprocess.run(["dpkg-deb", "--build", str(control.parent), str(self.package)], check=True, capture_output=True)
        self.manifest = {"schema": 1, "version": "0.1.0", "sha256": {
            "linux-aarch64": hashlib.sha256(self.package.read_bytes()).hexdigest()}}
        (self.root / "manifest.json").write_text(json.dumps(self.manifest))
        (self.root / "systemd").mkdir()
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}", FIXTURE=str(self.root))

    def run_script(self, path, *args, **env):
        source = (ROOT / path).read_text()
        for old, new in (("/run/systemd/system", str(self.root / "systemd")),
                         ("/run/pi-control-install.lock", str(self.root / "install.lock")),
                         ("/etc/systemd/system/pi-control.service", str(self.root / "manual.service")),
                         ("/var/lib/pi-control", str(self.root / "state"))):
            source = source.replace(old, new)
        script = self.root / "script.sh"
        script.write_text(source)
        return subprocess.run(["sh", str(script), *args], env=dict(self.env, **env), capture_output=True, text=True)

    def calls(self):
        return [json.loads(line) for line in (self.root / "calls").read_text().splitlines()]

    def test_latest_install_and_reinstall(self):
        for _ in range(2):
            result = self.run_script("distribution/install.sh")
            self.assertEqual(result.returncode, 0, result.stderr)
        installs = [c for c in self.calls() if c[:2] == ["apt-get", "install"]]
        self.assertEqual(len(installs), 2)
        self.assertIn("--reinstall", installs[0])
        self.assertNotIn("--allow-downgrades", installs[0])
        urls = [c[c.index("--url") + 1] for c in self.calls() if c[0] == "curl"]
        self.assertIn("/latest/download/", urls[0])
        self.assertTrue(urls[1].endswith("/download/v0.1.0/pi-control_0.1.0_arm64.deb"))

    def test_pinned_version_allows_explicit_downgrade(self):
        result = self.run_script("distribution/install.sh", PI_CONTROL_INSTALL_VERSION="v0.1.0")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(any("--allow-downgrades" in c for c in self.calls()))

    def test_upgrade_only_skips_same_or_newer_installed_version(self):
        for installed in ("installed 0.1.0", "installed 0.2.0"):
            with self.subTest(installed=installed):
                result = self.run_script("distribution/install.sh", PI_CONTROL_INSTALL_UPGRADE_ONLY="1",
                                         TEST_INSTALLED=installed)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("is up to date", result.stdout)
        self.assertFalse(any(c[0] == "apt-get" for c in self.calls()))
        self.assertFalse(any(c[0] == "curl" and c[-1].endswith(".deb") for c in self.calls()))

    def test_upgrade_only_installs_newer_release(self):
        for installed in ("installed 0.0.9", "config-files 0.2.0", ""):
            with self.subTest(installed=installed):
                result = self.run_script("distribution/install.sh", PI_CONTROL_INSTALL_UPGRADE_ONLY="1",
                                         TEST_INSTALLED=installed)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("service enabled and running", result.stdout)
        installs = [c for c in self.calls() if c[:2] == ["apt-get", "install"]]
        self.assertEqual(len(installs), 3)
        self.assertNotIn("--allow-downgrades", installs[0])

    def test_upgrade_only_rejects_pins_and_other_values(self):
        for env in ({"PI_CONTROL_INSTALL_UPGRADE_ONLY": "1", "PI_CONTROL_INSTALL_VERSION": "v0.1.0"},
                    {"PI_CONTROL_INSTALL_UPGRADE_ONLY": "yes"}):
            with self.subTest(env=env):
                result = self.run_script("distribution/install.sh", **env)
                self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(c[0] in ("curl", "apt-get") for c in self.calls()))

    def test_update_unit_runs_packaged_installer_in_upgrade_only_mode(self):
        unit = (ROOT / "deploy/pi-control-update.service").read_text()
        self.assertIn("Type=oneshot\n", unit)
        self.assertIn("Environment=PI_CONTROL_INSTALL_UPGRADE_ONLY=1\n", unit)
        self.assertIn("ExecStart=/usr/bin/systemd-inhibit --what=shutdown:sleep --mode=block", unit)
        self.assertIn(" /bin/sh /usr/lib/pi-control/install.sh\n", unit)
        self.assertNotIn("[Install]", unit)
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn("install(FILES distribution/install.sh DESTINATION lib/pi-control)", cmake)
        self.assertIn("install(FILES deploy/pi-control-update.service DESTINATION lib/systemd/system)", cmake)

    def test_concurrent_installer_rejected_then_succeeds_after_unlock(self):
        with (self.root / "install.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            result = self.run_script("distribution/install.sh")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("another pi-control install is running", result.stderr)
            self.assertFalse(any(c[0] in ("curl", "apt-get") for c in self.calls()))
        result = self.run_script("distribution/install.sh")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_invalid_version_rejected_before_download(self):
        result = self.run_script("distribution/install.sh", PI_CONTROL_INSTALL_VERSION="../../bad")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(c[0] in ("curl", "apt-get") for c in self.calls()))

    def test_bad_checksum_preserves_existing_install(self):
        self.package.write_bytes(b"corrupt")
        result = self.run_script("distribution/install.sh")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(c[0] == "apt-get" for c in self.calls()))

    def test_manifest_version_mismatch_preserves_existing_install(self):
        result = self.run_script("distribution/install.sh", PI_CONTROL_INSTALL_VERSION="v0.2.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(c[0] == "apt-get" for c in self.calls()))

    def test_wrong_architecture_rejected(self):
        result = self.run_script("distribution/install.sh", TEST_ARCH="armhf")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(c[0] == "curl" for c in self.calls()))

    def test_manual_unit_rejected(self):
        (self.root / "manual.service").symlink_to("missing")
        result = self.run_script("distribution/install.sh")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("old manual", result.stderr)
        self.assertFalse(any(c[0] == "apt-get" for c in self.calls()))

    def test_apt_and_service_failures_propagate(self):
        for env in ({"TEST_APT_FAILURE": "1"}, {"TEST_INACTIVE": "1"}):
            with self.subTest(env=env):
                result = self.run_script("distribution/install.sh", **env)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("service enabled and running", result.stdout)

    def test_package_configure_enables_and_restarts(self):
        result = self.run_script("distribution/debian/postinst", "configure")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([c for c in self.calls() if c[0] == "systemctl"], [
            ["systemctl", "daemon-reload"], ["systemctl", "enable", "pi-control.service"],
            ["systemctl", "reset-failed", "pi-control.service"], ["systemctl", "restart", "pi-control.service"]])

    def test_package_rejects_login_account(self):
        result = self.run_script("distribution/debian/preinst", "install", TEST_SHELL="/bin/bash")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(c[0] == "systemctl" for c in self.calls()))

    def test_fresh_install_creates_account_and_groups_once_before_start(self):
        for path, arg in (("preinst", "install"), ("postinst", "configure"), ("postinst", "configure")):
            result = self.run_script("distribution/debian/" + path, arg, TEST_FRESH_SYSTEM="1")
            self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        creates = [c for c in calls if c[0] in ("adduser", "addgroup")]
        self.assertEqual(creates, [
            ["adduser", "--system", "--group", "--no-create-home", "--home", "/nonexistent", "pi-control"],
            ["addgroup", "--system", "video"], ["addgroup", "--system", "render"],
            ["addgroup", "--system", "input"]])
        first_restart = calls.index(["systemctl", "restart", "pi-control.service"])
        self.assertTrue(all(calls.index(c) < first_restart for c in creates))

    def test_remove_stops_service_but_upgrade_keeps_it_running(self):
        result = self.run_script("distribution/debian/prerm", "upgrade")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / "calls").exists())
        result = self.run_script("distribution/debian/prerm", "remove")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), [["systemctl", "disable", "--now", "pi-control.service"]])

    def test_manifest_generator_validates_package(self):
        command = ["python3", str(ROOT / "distribution/manifest.py"), str(self.package)]
        result = subprocess.run([*command, "v0.1.0"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), self.manifest)
        result = subprocess.run([*command, "v0.2.0"], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)

    def test_purge_removes_state_but_remove_keeps_it(self):
        state = self.root / "state"
        state.mkdir()
        (state / "settings.ini").write_text("[screensaver]\nid=water\n")
        result = self.run_script("distribution/debian/postrm", "remove")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((state / "settings.ini").exists())
        result = self.run_script("distribution/debian/postrm", "purge")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(state.exists())

    def test_service_has_private_state_directory(self):
        unit = (ROOT / "deploy/pi-control.service").read_text()
        self.assertIn("StateDirectory=pi-control\n", unit)
        self.assertIn("StateDirectoryMode=0700\n", unit)

    def test_polkit_rule_grants_exactly_the_kiosk_actions(self):
        rules = (ROOT / "deploy/49-pi-control.rules").read_text()
        actions = set(re.findall(r'action\.id === "([^"]+)"', rules))
        self.assertEqual(actions, {
            "org.freedesktop.login1.power-off",
            "org.freedesktop.login1.power-off-multiple-sessions",
            "org.freedesktop.NetworkManager.wifi.scan",
            "org.freedesktop.NetworkManager.network-control",
            "org.freedesktop.NetworkManager.settings.modify.system",
            "org.freedesktop.systemd1.manage-units",
        })
        # Unit management is limited to starting the updater.
        self.assertEqual(re.findall(r'action\.lookup\("(\w+)"\) === "([^"]+)"', rules),
                         [("unit", "pi-control-update.service"), ("verb", "start")])
        self.assertNotIn("startsWith", rules)
        self.assertEqual(rules.count('subject.user === "pi-control"'), rules.count("polkit.addRule"))


if __name__ == "__main__":
    unittest.main()
