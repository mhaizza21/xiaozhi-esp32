import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARD_DIR = ROOT / "main" / "boards" / "freenove-esp32s3-display-2.8-lcd"
DISPLAY_HEADER = BOARD_DIR / "mhaibot_display.h"
DISPLAY_SOURCE = BOARD_DIR / "mhaibot_display.cc"
SERVO_UART_SOURCE = BOARD_DIR / "mhaibot_servo_uart.cc"
BOARD_SOURCE = BOARD_DIR / "freenove-esp32s3-display-2.8-lcd.cc"
RUNBOOK = ROOT / "obsidian-vault" / "MhaiBot-Servo-Integration-Runbook.md"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


class MhaiBotServoIntegrationContracts(unittest.TestCase):
    """Static contracts for the S3-to-C3 servo UART validation slice."""

    def test_display_routes_behavior_through_model_and_servo_bridge(self):
        header = _read(DISPLAY_HEADER)
        source = _read(DISPLAY_SOURCE)

        self.assertIn('#include "mhaibot_behavior_model.h"', header)
        self.assertIn('#include "mhaibot_servo_uart.h"', header)
        self.assertIn("MhaiBotBehaviorModel behavior_model_;", header)
        self.assertIn("MhaiBotServoUart servo_uart_;", header)
        self.assertIn("void PublishBehaviorIntentLocked(const char* emotion);", header)
        self.assertIn("input.servo_available = servo_uart_.IsEnabled();", source)
        self.assertIn("const MhaiBotBehaviorIntent intent = behavior_model_.FromInput(input);", source)
        self.assertIn("face_->PublishIntent(intent.eye);", source)
        self.assertIn("servo_uart_.ApplyIntent(intent);", source)
        self.assertIn("bool MoveNeck(const std::string& action);", header)
        self.assertIn("servo_uart_.SendActionCommand(action)", source)

    def test_servo_uart_bridge_exposes_human_like_safe_actions(self):
        source = _read(SERVO_UART_SOURCE)
        header = _read(BOARD_DIR / "mhaibot_servo_uart.h")

        self.assertIn("Servo UART bridge is in human-like manual motion validation mode", source)
        self.assertIn('constexpr const char* kCenterCommand = "move 1500 1500\\n";', source)
        self.assertIn("constexpr int kLeftUs = 1350;", source)
        self.assertIn("constexpr int kRightUs = 1650;", source)
        self.assertIn("constexpr int kUpUs = 1400;", source)
        self.assertIn("constexpr int kDownUs = 1600;", source)
        self.assertIn("constexpr int kSmoothStepDelayMs = 90;", source)
        self.assertIn("constexpr int kLookHoldMs = 2500;", source)
        self.assertIn("bool SendSmoothMoveCommand", header)
        self.assertIn("bool SendLookAndReturnCommand", header)
        self.assertIn("bool ManualMotionActive() const;", header)
        self.assertIn("constexpr gpio_num_t kServoUartTxGpio = GPIO_NUM_43;", source)
        self.assertIn("constexpr gpio_num_t kServoUartRxGpio = GPIO_NUM_44;", source)
        self.assertIn("uart_write_bytes", source)
        self.assertIn("uart_driver_install", source)
        self.assertIn("uart_param_config", source)
        self.assertIn("uart_set_pin", source)
        self.assertIn('action == "left"', source)
        self.assertIn('action == "right"', source)
        self.assertIn('action == "up"', source)
        self.assertIn('action == "down"', source)
        self.assertIn('action == "shake"', source)
        self.assertIn('action == "nod"', source)
        self.assertIn("SendLookAndReturnCommand(kLeftUs, kCenterUs, \"look-left\")", source)
        self.assertIn("SendSmoothMoveCommand(kCenterUs, kCenterUs, \"soft-return-center\")", source)
        self.assertIn("Skipping behavior center command while manual neck motion is active", source)
        self.assertIn("SendCenterCommand(intent.motion);", source)

    def test_board_exposes_neck_mcp_tool(self):
        source = _read(BOARD_SOURCE)

        self.assertIn('"self.neck.move"', source)
        self.assertIn("Allowed action values: left, right, up, down, center, shake, nod", source)
        self.assertIn('Property("action", kPropertyTypeString)', source)
        self.assertIn("display_->MoveNeck(action)", source)

    def test_runbook_records_center_only_hardware_validation_for_this_slice(self):
        runbook = _read(RUNBOOK)

        self.assertIn("center-only UART validation command", runbook)
        self.assertIn("move 1500 1500", runbook)
        self.assertIn("S3 TX | C3 GPIO20 RX", runbook)
        self.assertIn("S3 RX | C3 GPIO21 TX", runbook)
        self.assertIn("No idle/random automatic neck movement is enabled in this slice.", runbook)
        self.assertIn("Manual MCP tool `self.neck.move`", runbook)
        self.assertIn("Manual commands move smoothly, hold briefly, then return center smoothly", runbook)
        self.assertIn("shake  -> smooth left/right/left/center sequence", runbook)
        self.assertIn("nod    -> smooth up/down/up/center sequence", runbook)


if __name__ == "__main__":
    unittest.main()
