# xGRIB Android tablet layout

The Android arm64 build uses a touch-sized toolbar icon and an in-chart panel
with a visible Close button, full-width Open GRIB action, Settings and Download
GRIB actions, a labelled forecast time picker, wider timeline, and a cursor
data readout. Desktop layout code is unchanged.

On a Samsung Galaxy Tab SM-X210 with Android 15 and OpenCPN 5.14.0, the panel
fit in portrait and landscape. A synthetic coastal GRIB opened through the
Android file picker; the next forecast control advanced the time from 17:00
to 18:00 UTC. Settings and Download GRIB opened their native Android screens.
The Download flow was inspected, but no online download was made. Weather
Routing received valid forecast frames from the loaded file and completed a
route using them.

The package build includes `data/grib-android.svg`. Another Android screen
size and a production OpenCPN installation should be checked before release.
