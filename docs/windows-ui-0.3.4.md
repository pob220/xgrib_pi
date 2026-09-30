# xGRIB 0.3.4 Windows UI corrections

- Scale fallback bitmap images with high quality resampling and create them
  with the default bitmap depth. Passing the resampling enum as the depth
  caused the black, corrupted icons on Windows.
- Restore the stable Open, Settings and Download control IDs so saved toolbar
  visibility preferences apply, and restore right-click menus on action buttons.
- Rebuild the parameter controls immediately after opening generated output.
- Balance busy cursors after file and settings dialogs.
- Allow desktop window resizing and size action buttons for their actual
  translated labels, icons and dialog font.

Parameter controls are offered for fields present in the loaded GRIB. Display
options cannot add a weather field absent from that file. The Windows runtime
regression generates and reopens a wind/current file, selects a forecast time
where both are available, and verifies the painted selections change and restore.

Android retains its touch layout. All existing GRIB memory hardening is retained.

Native Windows CI uses the SHA-256 pinned upstream OpenCPN 5.14.2 development
installer, revision de7e706, built 2026-09-28. Matching x86/x64 hosts are also
exercised in isolated portable Wine 11.18 profiles; Wine is not a substitute for
all physical Windows graphics driver and DPI combinations.
