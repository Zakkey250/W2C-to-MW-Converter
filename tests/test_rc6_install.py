"""Regressions for retaining pre-W2C originals and replacing legacy LB safely."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / name) for name in ("public-converter", "compiler/source-runtime", "tools")]
import install
from components import LB_ARCHIVE, encode
from install_components import plan_components


class InstallationProtection(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "tests")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / "game"
        self.package = self.root / "package"
        self.game.mkdir()
        self.package.mkdir()
        self.guard = patch.object(install, "require_game_closed", lambda: None)
        self.guard.start()
        self.addCleanup(self.guard.stop)

    def put(self, root, name, data):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_legacy_lb_duplicates_restore_and_failure_recovery(self):
        # Actual shipped archive, including the renderer and 64-bit host.
        self.put(self.package, "Optional/LBx64/" + LB_ARCHIVE,
                 (ROOT / "public-converter/optional/LBx64" / LB_ARCHIVE).read_bytes())
        self.put(self.package, "package.json", encode({"optionalFiles": [{"path": "Optional/LBx64/" + LB_ARCHIVE}]}))
        from preflight import CATALOG
        original_digest = CATALOG["targets"]["NFSPatcher EN-US 1.3 LAA"]["sha256"]
        with patch("preflight.file_digest", return_value=original_digest):
            names = ("scripts/NFSMWLimitBreaker.asi", "plugins/disabled/NFSMWLimitBreaker.asi",
                     "update/MWLimitBrakerX64.asi", "NFSMWLimitBreaker.asi", "d3d9.dll")
            original = {name: ("old-" + name).encode() for name in names}
            original["scripts/NFSMWLimitBreaker.ini"] = b"[VehicleLimit]\r\nMaxVehicles=48\r\n"
            for name, data in original.items():
                self.put(self.game, name, data)
            changes, _ = plan_components(self.package, self.game, addon=False, lbx64=True)
            self.assertIsNone(changes["plugins/disabled/NFSMWLimitBreaker.asi"])
            def fail_at(name):
                if name == "d3d9.dll":
                    raise RuntimeError("simulated interruption")
            with self.assertRaisesRegex(RuntimeError, "simulated"):
                install.commit_changes(self.game, changes, before_commit=fail_at)
            for name, data in original.items():
                self.assertEqual((self.game / name).read_bytes(), data)
            install.commit_changes(self.game, changes)
            first_backup = {p.relative_to(self.game).as_posix(): install.sha(p)
                            for p in (self.game / "W2CMWPublic/component-originals").rglob("*") if p.is_file()}
            self.assertIn(b"MaxVehicles = 48", (self.game / "scripts/NFSMWLimitBreaker.ini").read_bytes())
            self.assertFalse((self.game / "plugins/disabled/NFSMWLimitBreaker.asi").exists())
            config = self.game / "scripts/NFSMWLimitBreaker.ini"
            config.write_bytes(config.read_bytes().replace(b"MaxVehicles = 48", b"MaxVehicles = 40"))
            again, _ = plan_components(self.package, self.game, addon=False, lbx64=True)
            install.commit_changes(self.game, again)
            self.assertIn(b"MaxVehicles = 40", config.read_bytes())
            self.assertFalse((self.game / "plugins/disabled/NFSMWLimitBreaker.asi").exists())
            self.assertFalse((self.game / "update/MWLimitBrakerX64.asi").exists())
            for name, digest in first_backup.items():
                self.assertEqual(install.sha(self.game / name), digest)
            off, _ = plan_components(self.package, self.game, addon=False, lbx64=False)
            install.commit_changes(self.game, off)
            for name, data in original.items():
                self.assertEqual((self.game / name).read_bytes(), data)

    def test_rc5_world_upgrade_keeps_first_install_originals(self):
        old_generation, new_generation = "a" * 64, "b" * 64
        base = {}
        for name in (*install.CORE, install.STREAM):
            data = ("original-MW-" + name).encode()
            file = self.put(self.game, name, data)
            base[name] = install.sha(file)
            self.put(self.game, "W2CMWPublic/originals/" + name, data)
            if name in install.CORE:
                prior = ("RC5-W2C-" + name).encode()
                self.put(self.game, name, prior)
                self.put(self.game, f"W2CMWPublic/generations/{old_generation}/Core/" + name, prior)
                self.put(self.package, "Core/" + name, ("RC6-W2C-" + name).encode())
        self.put(self.package, install.ASI, b"new-runtime")
        self.put(self.package, "World/L5RA.w2i", b"new-world-index")
        old_asi = self.put(self.game, install.ASI, b"RC5-runtime")
        state = {"schema": 1, "packageId": install.CONTROL, "generation": old_generation,
                 "runtimeSha256": install.sha(old_asi), "targetCore": base, "streamSha256": base[install.STREAM]}
        self.put(self.game, install.MANIFEST, encode(state))
        self.put(self.game, install.SETTINGS, b"[user]\r\nPreserve=1\r\n")
        self.put(self.game, "GLOBAL/attributes.bin", b"user attributes, never replace")
        self.put(self.game, "profile/controls", b"user control bindings")
        info = {"buildId": new_generation, "quality": "WORLD", "targetCore": base,
                "streamSha256": base[install.STREAM], "files": [
                    {"path": p.relative_to(self.package).as_posix(), "bytes": p.stat().st_size, "sha256": install.sha(p)}
                    for p in self.package.rglob("*") if p.is_file()]}
        originals_before = {p.relative_to(self.game).as_posix(): install.sha(p)
                            for p in (self.game / "W2CMWPublic/originals").rglob("*") if p.is_file()}
        import build_world
        with patch.object(install, "check_target", return_value={"status": "ok"}), \
                patch.object(install, "checked_package", return_value=info), \
                patch.object(install, "runtime_rows", return_value=[]), \
                patch.object(build_world, "RUNTIME_ASI", self.package / install.ASI), \
                patch("install_components.plan_components", return_value=({}, {})), \
                patch.object(install, "ROLLBACK_SOURCE", self.root / "absent-rollback"):
            install.install(self.package, self.game)
            self.assertNotIn("components", json.loads((self.game / install.MANIFEST).read_bytes()))
            self.assertNotIn("converterVersion", json.loads((self.game / install.MANIFEST).read_bytes()))
            for name, digest in originals_before.items():
                self.assertEqual(install.sha(self.game / name), digest)
            self.assertEqual((self.game / "GLOBAL/attributes.bin").read_bytes(), b"user attributes, never replace")
            self.assertEqual((self.game / "profile/controls").read_bytes(), b"user control bindings")
            install.uninstall(self.game)
            for name in install.CORE:
                self.assertEqual(install.sha(self.game / name), base[name])
            self.assertEqual((self.game / install.SETTINGS).read_bytes(), b"[user]\r\nPreserve=1\r\n")


if __name__ == "__main__":
    unittest.main()
