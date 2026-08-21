import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARD_H = ROOT / "main" / "boards" / "common" / "board.h"
AFE_AUDIO_ENGINE_CC = ROOT / "main" / "audio" / "engines" / "afe_audio_engine.cc"
APPLICATION_CC = ROOT / "main" / "application.cc"
BOARDS_DIR = ROOT / "main" / "boards"
FREENOVE_CC = BOARDS_DIR / "freenove-esp32s3-display-2.8-lcd" / "freenove-esp32s3-display-2.8-lcd.cc"

# All board .cc/.h sources, used to prove a capability override is unique to
# the hardware-validated Freenove board and not silently copy-pasted elsewhere.
BOARD_SOURCE_FILES = sorted(BOARDS_DIR.rglob("*.cc")) + sorted(BOARDS_DIR.rglob("*.h"))


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _files_matching(pattern: str):
    regex = re.compile(pattern)
    return [f for f in BOARD_SOURCE_FILES if regex.search(_read(f))]


class WakeWordSourceContractTest(unittest.TestCase):
    """Static source-contract checks for the wake-word threshold/cooldown
    board-capability boundary. These don't execute code -- they lock down
    architectural invariants (default is a no-op, only one board overrides
    it, core audio never hardcodes a board-specific constant) that a
    behavioral unit test can't observe directly.
    """

    def test_board_default_threshold_is_nullopt(self):
        source = _read(BOARD_H)
        self.assertIn(
            "virtual std::optional<float> GetWakeNetThreshold() { return std::nullopt; }",
            source,
        )

    def test_only_freenove_overrides_threshold_to_0_4(self):
        matches = _files_matching(r"GetWakeNetThreshold\(\)\s*override")
        self.assertEqual(
            matches,
            [FREENOVE_CC],
            "GetWakeNetThreshold() must be overridden on exactly the Freenove board",
        )
        freenove_source = _read(FREENOVE_CC)
        self.assertIn(
            "virtual std::optional<float> GetWakeNetThreshold() override { return 0.4f; }",
            freenove_source,
        )

    def test_afe_audio_engine_resolves_board_capability_not_a_global_constant(self):
        source = _read(AFE_AUDIO_ENGINE_CC)
        self.assertIn(
            "ResolveWakeNetThreshold(Board::GetInstance().GetWakeNetThreshold())",
            source,
        )
        # The old fix hardcoded set_wakenet_threshold(afe_data_, 1, 0.4f)
        # directly -- that literal must not reappear; the value now only
        # ever flows in from the resolved Board capability.
        self.assertNotIn("set_wakenet_threshold(afe_data_, 1, 0.4f)", source)

    def test_cooldown_defaults_disabled(self):
        source = _read(BOARD_H)
        self.assertIn(
            "virtual uint32_t GetWakeWordCooldownAfterAudioCloseMs() { return 0; }",
            source,
        )

    def test_only_freenove_overrides_cooldown_to_1500ms(self):
        matches = _files_matching(r"GetWakeWordCooldownAfterAudioCloseMs\(\)\s*override")
        self.assertEqual(
            matches,
            [FREENOVE_CC],
            "GetWakeWordCooldownAfterAudioCloseMs() must be overridden on exactly the Freenove board",
        )
        freenove_source = _read(FREENOVE_CC)
        self.assertIn(
            "virtual uint32_t GetWakeWordCooldownAfterAudioCloseMs() override { return 1500; }",
            freenove_source,
        )

    def test_suppressed_branch_reenables_wake_word_detection(self):
        source = _read(APPLICATION_CC)
        match = re.search(
            r"if \(EvaluateWakeWordCooldown\([^)]*\) ==\s*"
            r"WakeWordCooldownDecision::kSuppressAndRearm\) \{(?P<body>.*?)\n    \}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(match, "could not locate the cooldown-suppressed branch")
        body = match.group("body")
        self.assertIn("audio_service_.EnableWakeWordDetection(true);", body)
        self.assertIn("return;", body)


if __name__ == "__main__":
    unittest.main()
