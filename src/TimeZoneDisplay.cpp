/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN xGRIB contributors                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "TimeZoneDisplay.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <stdexcept>
#include <string>

namespace marine_time {
namespace {

#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
#define MARINE_TIME_HAS_CHRONO_TZDB 1
#else
#define MARINE_TIME_HAS_CHRONO_TZDB 0
#endif

wxDateTime DateTimeFromSeconds(std::chrono::seconds value) {
  return wxDateTime(static_cast<time_t>(value.count()));
}

#if MARINE_TIME_HAS_CHRONO_TZDB
const std::chrono::time_zone* Locate(const wxString& name) {
  try {
    return std::chrono::locate_zone(name.ToStdString());
  } catch (const std::runtime_error&) {
    return nullptr;
  }
}

std::chrono::sys_seconds ToSysSeconds(const wxDateTime& value) {
  return std::chrono::sys_seconds{
      std::chrono::seconds{static_cast<std::int64_t>(value.GetTicks())}};
}
#endif

}  // namespace

std::vector<wxString> AvailableTimeZones() {
  std::vector<wxString> result;
#if MARINE_TIME_HAS_CHRONO_TZDB
  try {
    const auto& zones = std::chrono::get_tzdb().zones;
    result.reserve(zones.size() + 1);
    result.emplace_back("UTC");
    for (const auto& zone : zones) {
      const auto zoneName = zone.name();
      const wxString name =
          wxString::FromUTF8(zoneName.data(), zoneName.size());
      if (name != "UTC") result.push_back(name);
    }
  } catch (const std::runtime_error&) {
  }
#endif
  if (result.empty()) result.emplace_back("UTC");
  std::sort(result.begin() + 1, result.end());
  return result;
}

wxString SystemTimeZone() {
#if MARINE_TIME_HAS_CHRONO_TZDB
  try {
    const auto name = std::chrono::current_zone()->name();
    return wxString::FromUTF8(name.data(), name.size());
  } catch (const std::runtime_error&) {
  }
#endif
  return "UTC";
}

bool IsTimeZoneAvailable(const wxString& zoneName) {
  if (zoneName == "UTC") return true;
#if MARINE_TIME_HAS_CHRONO_TZDB
  return Locate(zoneName) != nullptr;
#else
  return false;
#endif
}

wxDateTime ToWallClock(const wxDateTime& utc, const wxString& zoneName) {
  if (!utc.IsValid()) return wxDateTime();
  if (zoneName == "UTC") return DateTimeFromSeconds(
      std::chrono::seconds{static_cast<std::int64_t>(utc.GetTicks())});
#if MARINE_TIME_HAS_CHRONO_TZDB
  if (const auto* zone = Locate(zoneName)) {
    const auto wall = zone->to_local(ToSysSeconds(utc));
    return DateTimeFromSeconds(
        std::chrono::duration_cast<std::chrono::seconds>(
            wall.time_since_epoch()));
  }
#endif
  return wxDateTime();
}

WallClockConversion FromWallClock(int year, int month, int day, int hour,
                                  int minute, int second,
                                  const wxString& zoneName) {
  using namespace std::chrono;
  WallClockConversion result;
  const year_month_day date{std::chrono::year{year},
                            std::chrono::month{static_cast<unsigned>(month)},
                            std::chrono::day{static_cast<unsigned>(day)}};
  if (!date.ok() || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59)
    return result;
  const local_seconds wall{local_days{date}.time_since_epoch() + hours{hour} +
                           minutes{minute} + seconds{second}};
  if (zoneName == "UTC") {
    result.utc = DateTimeFromSeconds(
        duration_cast<seconds>(wall.time_since_epoch()));
    result.status = WallClockStatus::Valid;
    return result;
  }
#if MARINE_TIME_HAS_CHRONO_TZDB
  const auto* zone = Locate(zoneName);
  if (!zone) return result;
  const auto info = zone->get_info(wall);
  if (info.result == local_info::nonexistent) {
    result.status = WallClockStatus::Nonexistent;
    return result;
  }
  const bool ambiguous = info.result == local_info::ambiguous;
  const auto instant = zone->to_sys(wall, choose::earliest);
  result.utc = DateTimeFromSeconds(
      duration_cast<seconds>(instant.time_since_epoch()));
  result.status =
      ambiguous ? WallClockStatus::Ambiguous : WallClockStatus::Valid;
#endif
  return result;
}

wxString TimeZoneAbbreviation(const wxDateTime& utc,
                              const wxString& zoneName) {
  if (!utc.IsValid()) return wxEmptyString;
  if (zoneName == "UTC") return "UTC";
#if MARINE_TIME_HAS_CHRONO_TZDB
  if (const auto* zone = Locate(zoneName))
    return wxString::FromUTF8(zone->get_info(ToSysSeconds(utc)).abbrev);
#endif
  return wxEmptyString;
}

}  // namespace marine_time
