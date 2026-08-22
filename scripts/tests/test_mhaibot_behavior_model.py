import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN_DIR = ROOT / "main"
BOARD_DIR = MAIN_DIR / "boards" / "freenove-esp32s3-display-2.8-lcd"
EYE_DIR = BOARD_DIR / "eye"
TEST_SOURCE = ROOT / "scripts" / "tests" / "mhaibot_behavior_model_test.cc"


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


class MhaiBotBehaviorModelHostTest(unittest.TestCase):
    """LVGL-free host test for the MhaiBot face/neck behavior intent model."""

    def test_mhaibot_behavior_model_compiles_and_runs(self):
        compiler = find_cxx_compiler()
        if compiler is None:
            self.skipTest("g++ or clang++ is required for the host MhaiBot behavior model test")

        with tempfile.TemporaryDirectory() as directory:
            common_args = [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(BOARD_DIR),
                "-I",
                str(MAIN_DIR),
            ]
            sources = [
                str(TEST_SOURCE),
                str(BOARD_DIR / "mhaibot_behavior_model.cc"),
                str(EYE_DIR / "eye_activity_adapter.cc"),
            ]
            output = Path(directory) / "mhaibot_behavior_model_test"
            command = common_args + sources + ["-o", str(output)]
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
