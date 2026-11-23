"""Регрессии проверки Qt: qtsvg — архив базового пакета, а не addon."""

import importlib.util
from pathlib import Path
import unittest


spec = importlib.util.spec_from_file_location(
    "checkCiQt", Path(__file__).resolve().parents[1] / "tools/checkCiQt.py"
)
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class QtPackagesTest(unittest.TestCase):
    def metadata(self, arch, archives):
        return (
            f"<Updates><PackageUpdate><Name>qt.qt6.6120.{arch}</Name>"
            f"<DownloadableArchives>{archives}</DownloadableArchives>"
            "</PackageUpdate></Updates>"
        )

    def test_svg_in_base_package_on_both_platforms(self):
        for arch in ("linux_gcc_64", "clang_64"):
            with self.subTest(arch=arch):
                check.validate_packages(
                    self.metadata(arch, "qtbase-platform.7z, qtsvg-platform.7z"),
                    "6.12.0", arch, ["qtbase", "qtsvg"]
                )

    def test_missing_archive_fails_before_install(self):
        with self.assertRaisesRegex(ValueError, "qtsvg"):
            check.validate_packages(
                self.metadata("clang_64", "qtbase-platform.7z"),
                "6.12.0", "clang_64", ["qtbase", "qtsvg"]
            )

    def test_wrong_version_or_arch_fails(self):
        xml = self.metadata("clang_64", "qtbase-platform.7z")
        for version, arch in (("6.11.0", "clang_64"), ("6.12.0", "linux_gcc_64")):
            with self.subTest(version=version, arch=arch):
                with self.assertRaisesRegex(ValueError, "не найден"):
                    check.validate_packages(xml, version, arch, ["qtbase"])

    def test_addon_is_not_a_base_archive(self):
        xml = self.metadata("addons.qtsvg.clang_64", "qtsvg-platform.7z")
        with self.assertRaisesRegex(ValueError, "не найден"):
            check.validate_packages(xml, "6.12.0", "clang_64", ["qtsvg"])

    def test_missing_bundled_icu_fails_before_install(self):
        xml = self.metadata("linux_gcc_64", "qtbase-platform.7z, icu-linux.7z")
        with self.assertRaisesRegex(ValueError, "требуется архив icu"):
            check.validate_packages(xml, "6.12.0", "linux_gcc_64", ["qtbase"])

    def test_bundled_icu_is_selected(self):
        xml = self.metadata("linux_gcc_64", "qtbase-platform.7z, icu-linux.7z")
        check.validate_packages(xml, "6.12.0", "linux_gcc_64", ["qtbase", "icu"])


if __name__ == "__main__":
    unittest.main()
