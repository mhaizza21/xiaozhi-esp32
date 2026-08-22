import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARD_DIR = ROOT / "main" / "boards" / "freenove-esp32s3-display-2.8-lcd"
DISPLAY_HEADER = BOARD_DIR / "mhaibot_display.h"
DISPLAY_SOURCE = BOARD_DIR / "mhaibot_display.cc"
SERVO_UART_SOURCE = BOARD_DIR / "mhaibot_servo_uart.cc"
RUNBOOK = ROOT / "obsidian-vault" / "MhaiBot-Servo-Integration-Runbook.md"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


class MhaiBotServoIntegrationContracts(unittest.TestCase):
    """Static contracts for the software-only servo integration slice."""

    def test_display_routes_behavior_through_model_and_disabled_bridge(self):
        header = _read(DISPLAY_HEADER)
        source = _read(DISPLAY_SOURCE)

        self.assertIn('#include "mhaibot_behavior_model.h"', header)
        self.assertIn('#include "mhaibot_servo_uart.h"', header)
        self.assertIn("MhaiBotBehaviorModel behavior_model_;", header)
        self.assertIn("MhaiBotServoUart servo_uart_;", header)
        self.assertIn("void PublishBehaviorIntentLocked(const char* emotion);", header)
        self.assertIn("input.servo_available = false;", source)
        self.assertIn("const MhaiBotBehaviorIntent intent = behavior_model_.FromInput(input);", source)
        self.assertIn("face_->PublishIntent(intent.eye);", source)
        self.assertIn("servo_uart_.ApplyIntent(intent);", source)

    def test_servo_uart_bridge_is_log_only(self):
        source = _read(SERVO_UART_SOURCE)

        self.assertIn("Servo UART bridge disabled; behavior intents are log-only", source)
        self.assertIn("Log-only intent:", source)
        self.assertNotIn("uart_write_bytes", source)
        self.assertNotIn("uart_driver_install", source)
        self.assertNotIn("uart_param_config", source)
        self.assertNotIn("uart_set_pin", source)

    def test_runbook_records_no_hardware_execution_for_this_slice(self):
        runbook = _read(RUNBOOK)

        self.assertIn("ESP32-S3 does not send UART bytes", runbook)
        self.assertIn("Servo hardware validation is not executed in this slice.", runbook)
        self.assertIn("S3 TX | C3 GPIO20 RX", runbook)
        self.assertIn("S3 RX | C3 GPIO21 TX", runbook)
        self.assertIn("No servo movement is caused by S3 firmware in this slice.", runbook)


if __name__ == "__main__":
    unittest.main()
