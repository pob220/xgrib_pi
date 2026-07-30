#include <cstdlib>
#include <iostream>

#include "TimeZoneDisplay.h"

namespace {

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

wxDateTime Utc(int year, int month, int day, int hour, int minute = 0) {
  const auto value =
      marine_time::FromWallClock(year, month, day, hour, minute, 0, "UTC");
  Check(value.status == marine_time::WallClockStatus::Valid,
        "UTC fixture must be valid");
  return value.utc;
}

}  // namespace

int main() {
  if (!marine_time::IsTimeZoneAvailable("Europe/London")) return 0;

  const wxDateTime winter = Utc(2026, 1, 15, 12);
  const wxDateTime summer = Utc(2026, 7, 15, 12);
  const wxDateTime winterWall =
      marine_time::ToWallClock(winter, "Europe/London");
  const wxDateTime summerWall =
      marine_time::ToWallClock(summer, "Europe/London");
  Check(winterWall.GetHour(wxDateTime::UTC) == 12,
        "London winter must remain UTC");
  Check(summerWall.GetHour(wxDateTime::UTC) == 13,
        "London summer must use BST");
  Check(marine_time::TimeZoneAbbreviation(winter, "Europe/London") == "GMT",
        "London winter abbreviation must be GMT");
  Check(marine_time::TimeZoneAbbreviation(summer, "Europe/London") == "BST",
        "London summer abbreviation must be BST");

  const auto gap =
      marine_time::FromWallClock(2026, 3, 29, 1, 30, 0, "Europe/London");
  Check(gap.status == marine_time::WallClockStatus::Nonexistent &&
            !gap.utc.IsValid(),
        "spring DST gap must be rejected");
  const auto repeated =
      marine_time::FromWallClock(2026, 10, 25, 1, 30, 0, "Europe/London");
  Check(repeated.status == marine_time::WallClockStatus::Ambiguous,
        "autumn repeated time must be flagged");
  Check(repeated.utc.GetTicks() == Utc(2026, 10, 25, 0, 30).GetTicks(),
        "autumn repeated time must resolve conservatively to first occurrence");
  return 0;
}
