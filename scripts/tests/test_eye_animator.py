import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARD_DIR = ROOT / "main" / "boards" / "freenove-esp32s3-display-2.8-lcd"
EYE_DIR = BOARD_DIR / "eye"
TEST_SOURCE = ROOT / "scripts" / "tests" / "eye_animator_test.cc"


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


class EyeAnimatorHostTest(unittest.TestCase):
    """LVGL-free host test for the Slice 2 EyeAnimator / EyeIntentMailbox skeleton."""

    def test_eye_animator_and_mailbox_compile_and_run(self):
        compiler = find_cxx_compiler()
        if compiler is None:
            self.skipTest("g++ or clang++ is required for the host eye-animator test")

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
            sources = [
                str(TEST_SOURCE),
                str(EYE_DIR / "eye_animator.cc"),
                str(EYE_DIR / "eye_intent_mailbox.cc"),
            ]
            output = Path(directory) / "eye_animator_test"
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
            subprocess.run([str(output)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
