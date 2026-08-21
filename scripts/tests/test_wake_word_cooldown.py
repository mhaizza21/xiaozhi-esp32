import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN_DIR = ROOT / "main"
TEST_SOURCE = ROOT / "scripts" / "tests" / "wake_word_cooldown_test.cc"


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


class WakeWordCooldownHostTest(unittest.TestCase):
    """Host test for the wake-word self-trigger cooldown boundary/re-arm logic.

    Covers EvaluateWakeWordCooldown() (main/wake_word_cooldown.h), the pure
    decision extracted from Application::HandleWakeWordDetectedEvent so it can
    be tested without the ESP-IDF/FreeRTOS runtime.
    """

    def test_wake_word_cooldown_compiles_and_runs(self):
        compiler = find_cxx_compiler()
        if compiler is None:
            self.skipTest("g++ or clang++ is required for the host wake-word-cooldown test")

        with tempfile.TemporaryDirectory() as directory:
            command = [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fno-exceptions",
                "-I",
                str(MAIN_DIR),
                str(TEST_SOURCE),
                "-o",
                str(Path(directory) / "wake_word_cooldown_test"),
            ]
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

            output = Path(directory) / "wake_word_cooldown_test"
            try:
                subprocess.run([str(output)], check=True, cwd=ROOT, capture_output=True, text=True)
            except OSError as error:
                self.skipTest(
                    "C++ runtime assertions were not executed: "
                    f"host toolchain produced a non-native binary ({error})"
                )


if __name__ == "__main__":
    unittest.main()
