# Android 0.3.2.0 usability follow-up

This revision adds Android UI features to the common xGRIB release version.
The desktop generator and routing engine are unchanged. CircleCI's existing
platform matrix, Android QtAndroidExtras linkage and TLS isolation checks apply;
no workflow changes are required for these features.

## Forecast start

Area / time → Start date and time opens a calendar and touch hour/minute
selectors. All values are UTC. Use current UTC hour selects today's UTC date,
current UTC hour and minute zero. Use date and time accepts; Cancel or Android
Back retains the previous value. Requests serialize the timestamp internally.
Weather forecast duration is still measured from the model cycle. The start
also controls current/tidal predictions and forecast extension; inspect actual
field coverage after generation.

## Copernicus credentials

Account → Remember password on this tablet is optional and requires remembering
the username. Close encrypts the current password on a worker before clearing
its editor. Reopening restores it masked; Show password is always reset on
Close. Unchecking Remember password and closing removes the saved credential
and its key. Changing a restored account clears its password.

The password is encrypted using Android Keystore AES/GCM/NoPadding with a new
random IV and the trimmed username as authenticated additional data. Settings
contain a versioned IV/ciphertext record, account identifier and opt-in flags.
The nonexportable key remains in this app installation's Keystore. A copied
configuration cannot carry that key to another installation. Secure-storage
errors leave the form open for retry or re-entry; there is no plaintext fallback.
JNI exceptions are cleared without printing account data; temporary byte arrays
are erased after the cryptographic operation. As with all password clients,
plaintext necessarily exists in the active editor and provider request memory.

References: [Android Keystore](https://developer.android.com/privacy-and-security/keystore)
and [KeyGenParameterSpec](https://developer.android.com/reference/android/security/keystore/KeyGenParameterSpec).

## Modal Back and host integration

The pinned OpenCPN 5.14 Android activity counts visible wx top-level windows
before forwarding Back key-up to Qt. xGRIB's private wx linkage (required for
TLS isolation) and wxQt's direct QDialog modal path need owned sheets registered
with the host and their wx visibility synchronized. The Back filter dismisses
keyboard, popup, then sheet, consuming both press and release. Scope the filter
before its wxDialog is destroyed; a QObject child alone outlives parts of wx's
destruction and can dereference an invalid wx vtable during event delivery.

Helpers target this pinned host/support ABI. Keep `--exclude-libs,ALL` and run
`ci/verify-android-runtime.py`; making static symbols public would reintroduce
the mixed OpenSSL runtime failure. Android predictive Back and another host APK
require separate testing.

## Device acceptance record

Samsung SM-X210, Android 15, OpenCPN 5.14.0/API 1.21, arm64, 26 September 2026.
Evidence and exact package hashes are in
`artifacts/xgrib-0.3.2-android-20260926/` in the parent workspace.

- Date/hour/minute acceptance, cancellation and explicit UTC under BST.
- Calendar layout in landscape and portrait; touch dropdown scrolling.
- Synthesized mouse drags scroll the minute popup without selecting a value;
  selecting 25 after scrolling applied 21:25 UTC correctly. Current UTC hour
  restored 21:00 UTC while the tablet showed 22:13 BST.
- Back dismissed dropdown, calendar and generator in order with the host PID
  unchanged and visible top-level counts 3, 3 and 2. Settings Back discarded
  an interpolation edit; forecast-time Back discarded a changed selection.
- Disposable password with significant leading/trailing spaces: encrypted
  close, cold restart, masked restore, account-change clearing and opt-out.
  The settings check found no plaintext, a 12-byte IV and authenticated
  ciphertext of the expected length. The disposable credential was forgotten.
- Real 96-hour Irish Sea/North Channel forecast generated on the tablet before
  this follow-up, including UKV wind through hour 96, GFS waves and Copernicus
  Northwest Shelf currents. After the user entered the real password once with
  Remember enabled, Close and cold restart restored it masked. A second live
  96-hour generation succeeded using that restored password: 761 messages,
  48.95 MiB, overall coverage 26 September 12:00 to 30 September 21:00 UTC.
  ecCodes independently verified wind hourly through 54 hours and every three
  hours through 96, bounds 50.5–56.5 N / 8.5–2.5 W. Settings contained the
  encrypted record and 12-byte IV, with no plaintext password setting.

These checks qualify this device/host combination. They do not substitute for
production APK, additional screen/font sizes, every live provider or a fresh
remote CircleCI platform run. 0.3.1.0 alpha publication is separate from this
new local 0.3.2.0 revision.
