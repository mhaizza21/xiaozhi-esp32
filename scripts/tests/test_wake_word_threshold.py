import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AUDIO_DIR = ROOT / "main" / "audio"
TEST_SOURCE = ROOT / "scripts" / "tests" / "wake_word_threshold_test.cc"


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


class WakeWordThresholdHostTest(unittest.TestCase):
    """Host test for WakeNet threshold scoping/validation.

    Covers ResolveWakeNetThreshold() (main/audio/wake_word_threshold.h), the
    pure validation extracted from AfeAudioEngine::Initialize() so board
    override behavior can be tested without the ESP-IDF/esp-sr runtime.
    """

    def test_wake_word_threshold_compiles_and_runs(self):
        compiler = find_cxx_compiler()
        if compiler is None:
            self.skipTest("g++ or clang++ is required for the host wake-word-threshold test")

        with tempfile.TemporaryDirectory() as directory:
            command = [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                # The esp-clang riscv32 baremetal fallback toolchain (used
                # when no host g++/clang++ is on PATH) has no unwind runtime
                # for a freestanding host link; std::optional's exception
                # scaffolding otherwise fails at link time. Firmware builds
                # separately with CONFIG_COMPILER_CXX_EXCEPTIONS=y and are
                # unaffected -- this only shapes the standalone test binary.
                "-fno-exceptions",
                "-I",
                str(AUDIO_DIR),
                str(TEST_SOURCE),
                "-o",
                str(Path(directory) / "wake_word_threshold_test"),
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

            output = Path(directory) / "wake_word_threshold_test"
            try:
                subprocess.run([str(output)], check=True, cwd=ROOT, capture_output=True, text=True)
            except OSError as error:
                self.skipTest(
                    "C++ runtime assertions were not executed: "
                    f"host toolchain produced a non-native binary ({error})"
                )


if __name__ == "__main__":
    unittest.main()
