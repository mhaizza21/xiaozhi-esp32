#ifndef MHAIBOT_EYE_PIXEL_SOURCE_CONSOLE_H
#define MHAIBOT_EYE_PIXEL_SOURCE_CONSOLE_H

class MhaiBotDisplay;

// Slice 11B: engineer-only console command for runtime PixelSource control
// during hardware bring-up/soak testing (09 Slice 11B). This board does
// not already run a console REPL, so this also starts one, over the
// native USB-Serial/JTAG controller -- this board's only host-visible
// serial transport (no separate UART-to-USB bridge chip). The project's
// console primary channel (sdkconfig) must be set to USB Serial/JTAG to
// match; see the .cc for the hardware-validation root cause this was
// fixed against.
//
// Registers a single command:
//   eye_pixel_source legacy   -- MhaiBotFaceV2::PixelSource::kLegacy
//   eye_pixel_source shadow   -- MhaiBotFaceV2::PixelSource::kShadow
//   eye_pixel_source get      -- prints the current source
//
// This is a bring-up tool only. It is not exposed via MCP, voice, or any
// user-facing UI/config, and it does not persist to NVS or change the
// default (MhaiBotFaceV2::PixelSource::kLegacy) — the command exists so an
// engineer can flip the source during a serial-attached session, not so
// anything flips it automatically.
void RegisterEyePixelSourceConsole(MhaiBotDisplay* display);

#endif  // MHAIBOT_EYE_PIXEL_SOURCE_CONSOLE_H
