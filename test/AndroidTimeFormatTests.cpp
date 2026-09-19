#include "AndroidTimeFormat.h"
#include <cstdlib>
#include <iostream>

int main() {
  for (const char* zone : {"UTC", "Europe/London", "America/New_York"}) {
    setenv("TZ", zone, 1);
    tzset();
    if (XgribAndroidUtcText(1789830000) != "2026-09-19 15:00:00" ||
        XgribAndroidUtcText(1609502400) != "2021-01-01 12:00:00" ||
        XgribAndroidUtcText(1709208000) != "2024-02-29 12:00:00" ||
        XgribAndroidUtcText(1789830000, true) != "Sat 2026-09-19\n15:00") {
      std::cerr << "Android UTC display changed with device timezone: " << zone << '\n';
      return 1;
    }
  }
  std::cout << "Android UTC display: summer, winter, leap day and table passed\n";
}
