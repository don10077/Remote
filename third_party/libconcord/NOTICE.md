# libconcord

Source: ../concordance/libconcord, revision dca4505, copied 2026-11-07.
Upstream: https://github.com/jaymzh/concordance
License: GPL-3.0-or-later; original notices are retained in every source file.

Harmony builds this snapshot statically with HIDAPI on Windows, Linux and macOS.
The library implements classic HID, Z-HID, USB-network and MH configuration transfer.
No Logitech web API is called by Harmony's transfer adapter.

Local changes: CMake build; memory-based loading of already validated configuration
data (no temporary ANSI filename); correct array destruction in OperationFile.
Further patches, if any, are documented here alongside their reason.

Additional Harmony patches:
- Clear operation-file and HID handles on destruction to allow repeated sessions.
- Validate classic configuration bounds, cookie and end vector before flash writes.
- Fix a descending sector boundary (0x1d0000 -> 0x3d0000) and stop erase-sector
  iteration at the sentinel, including after the last required sector.
- Use HIDAPI's target-provided include path.
- Disable upstream HTTPS posting (LC_ERROR_UNSUPP); retain shared XML helpers.
  Configuration transfer is local and does not require libcurl.
