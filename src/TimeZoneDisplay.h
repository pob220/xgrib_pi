/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN xGRIB contributors                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef XGRIB_TIME_ZONE_DISPLAY_H
#define XGRIB_TIME_ZONE_DISPLAY_H

#include <vector>

#include <wx/datetime.h>
#include <wx/string.h>

namespace marine_time {

enum class WallClockStatus { Valid, Ambiguous, Nonexistent, Invalid };

struct WallClockConversion {
  wxDateTime utc;
  WallClockStatus status = WallClockStatus::Invalid;
};

std::vector<wxString> AvailableTimeZones();
wxString SystemTimeZone();
bool IsTimeZoneAvailable(const wxString& zoneName);
wxDateTime ToWallClock(const wxDateTime& utc, const wxString& zoneName);
WallClockConversion FromWallClock(int year, int month, int day, int hour,
                                  int minute, int second,
                                  const wxString& zoneName);
wxString TimeZoneAbbreviation(const wxDateTime& utc,
                              const wxString& zoneName);

}  // namespace marine_time

#endif
