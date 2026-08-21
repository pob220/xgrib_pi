# External environmental-data provider

The `external-control/scheduler-preview` branch optionally registers xGRIB as
`environmental-data.xgrib.v1`.  Registration is resolved at runtime, outside
the numbered OpenCPN plugin ABI.  A stock OpenCPN host therefore runs the same
xGRIB GUI and simply has no external provider to register.

The provider accepts a typed, bounded acquisition request, writes a private
`environmental-grib` schema-v1 job and runs the packaged helper without a
shell.  It publishes dataset metadata only after the helper result, GRIB
inspection and SHA-256 digest have succeeded.  The private pathname is retained
as an opaque host handle and is not part of the HTTP response.

Publication does not change the displayed forecast.  A separate activation
callback opens the retained file through the existing xGRIB viewer on the GUI
thread.  Cancellation terminates the helper process and a failed acquisition
leaves the previously displayed dataset unchanged.

Current preview limits are deliberate: one acquisition runs at a time and the
bounding box must not cross the antimeridian.  Public providers require no
credential.  For unattended Copernicus Marine runs the non-secret username is
part of the typed request while the password is supplied only through the
helper's `ENVIRONMENTAL_GRIB_COPERNICUS_PASSWORD` environment variable.  The
password is never accepted in HTTP, provider JSON, job files or logs.
