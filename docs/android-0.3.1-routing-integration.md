# Android 0.3.1 routing integration

The Android Close action now hides the weather controller while retaining the
selected forecast for GRIB_VALUES and GRIB_TIMELINE_RECORD requests. Previously
it always destroyed the controller. A routing request then recreated it using
the newest-file preference, which could silently select a different forecast.
Genuine dialog style changes still use the existing reconstruction path.
Desktop Close behavior is unchanged.

## Device evidence

Samsung SM-X210, Android 15, OpenCPN 5.14 development APK, 26 September 2026.
Two real files were available: Irish Sea/North Channel UKV with GFS waves, and
a newer North Atlantic GFS wind file whose eastern boundary excludes Conwy.
The Irish file was explicitly opened, then xGRIB was closed before computing.
Quick Conwy and Dun Laoghaire completed with Irish GRIB wind and wave source
accounting. All three Standard coastal routes subsequently completed, including
Foyle. These results verify the provider stayed available while its UI was hidden.

The real Irish forecast had already been generated on this tablet: 96 hours,
hourly UKV through hour 54 and three-hourly thereafter. The full plugin was
rebuilt for arm64 with the in-process generator and isolated TLS dependencies.
The existing desktop regression suite passed 33/33 checks and the Android TLS
isolation validator passed. The final import package was installed through the actual OpenCPN Plugin
Manager, followed by a cold restart.

## Build integration

Version 0.3.1.0 comes from the unified CMake version. Existing Android CircleCI
build, runtime verification and manual-import packaging use that version; no
additional job or workflow change is needed for this fix. No remote publication
is performed by this local development task.

## Native Android picker deadlock

A second Android fix keeps the Qt event loop running while the host's legacy
Java picker waits for a selection. Open after editing batch text in the routing
plugin reproduced an ANR at 12:26:48. Android main waited synchronously for Qt
IME composition while the Qt thread waited in the Java chooser's latch.
`OpenCPNAndroidFileSelector.h` runs only JNI on a worker, retaining all widget
operations on the Qt GUI thread. Both xGRIB Open and the generator's file-source
chooser use it; asynchronous scoped-storage selection remains supported.
Desktop file dialogs are unchanged. Post-fix Open selected the Irish forecast
after routing text edits without a new ANR. The scoped-storage picker also
returned normally, and imported bytes matched the source hash.
Both desktop rebuild and 33/33 regression checks passed after the picker fix;
Android TLS isolation was rechecked successfully.

## Generator preference persistence

Android generator Close now flushes its saved settings before returning to
OpenCPN. A cold restart retained the tested 96-hour, hourly UKV forecast options.
These controller, JNI and preference fixes are guarded for Android; desktop
behavior is retained. The final 0.3.1.0 package imported successfully through
Plugin Manager at 16:45 on 26 September. Its installed 80,161,528-byte library
matched archive SHA256
`3740dcf9e127ff5dc5f66084f5364a3b34b1fd78784e2b87358282fc88dcfebc`.
Cold restart loaded xGRIB and skipped the disabled bundled GRIB plugin.

The host's Plugin Manager still uses its own blocking Java file chooser; after
Qt text editing it can reproduce the host ANR independently of these plugin
adapters. Importing from a cold Activity succeeded. This is a host limitation
to track separately; it does not justify claiming the Plugin Manager deadlock
is fixed by xGRIB.
