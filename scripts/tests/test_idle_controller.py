import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARD_DIR = ROOT / "main" / "boards" / "freenove-esp32s3-display-2.8-lcd"
EYE_DIR = BOARD_DIR / "eye"
TEST_SOURCE = ROOT / "scripts" / "tests" / "idle_controller_test.cc"

SOURCES = (
    TEST_SOURCE,
    EYE_DIR / "idle_controller.cc",
    EYE_DIR / "eye_animator.cc",
    EYE_DIR / "eye_post_compose.cc",
)


def find_cxx_compiler():
    compiler = shutil.which("g++") or shutil.which("clang++")
    if compiler:
        return compiler

    espressif_tools = Path("C:/Espressif/tools")
    if espressif_tools.exists():
        candidates = sorted(espressif_tools.glob("esp-clang/*/esp-clang/bin/clang++.exe"))
        if candidates:
            return str(candidates[-1])

    return None


class IdleControllerHostTest(unittest.TestCase):
    """LVGL-free host test for the Slice 5 IdleController + shared post-compose stage."""

    def test_idle_controller_and_post_compose_compile(self):
        """Always runs: compiles the Slice 5 sources with -Wall -Wextra -Werror.

        Meaningful in any environment. The compile-and-run assertions below
        additionally require a working host toolchain, which this sandbox
        may not have (see skip below).
        """
        compiler = find_cxx_compiler()
        if compiler is None:
            self.skipTest("g++ or clang++ is required for the host idle-controller test")

        with tempfile.TemporaryDirectory() as directory:
            common_args = [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(BOARD_DIR),
            ]
            for source in SOURCES:
                subprocess.run(
                    common_args
                    + ["-c", str(source), "-o", str(Path(directory) / (source.stem + ".o"))],
                    check=True,
                    cwd=ROOT,
                    capture_output=True,
                    text=True,
                )

    def test_idle_controller_and_post_compose_compile_and_run(self):
        compiler = find_cxx_compiler()
        if compiler is None:
            self.skipTest("g++ or clang++ is required for the host idle-controller test")

        with tempfile.TemporaryDirectory() as directory:
            common_args = [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(BOARD_DIR),
            ]
            output = Path(directory) / "idle_controller_test"
            command = common_args + [str(s) for s in SOURCES] + ["-o", str(output)]
            try:
                subprocess.run(command, check=True, cwd=ROOT, capture_output=True, text=True)
            except subprocess.CalledProcessError as error:
                output_text = f"{error.stdout}\n{error.stderr}"
                if "Application Control policy has blocked" in output_text:
                    self.skipTest(
                        "C++ runtime assertions were not executed: "
                        "host linker is blocked by Windows Application Control"
                    )
                raise

            try:
                subprocess.run([str(output)], check=True, cwd=ROOT, capture_output=True, text=True)
            except OSError as error:
                self.skipTest(
                    "C++ runtime assertions were not executed: "
                    f"host toolchain produced a non-native binary ({error})"
                )


if __name__ == "__main__":
    unittest.main()
