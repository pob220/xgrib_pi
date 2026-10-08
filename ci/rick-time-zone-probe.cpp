#include <chrono>
#include <iostream>
#include <string>

template <class Function>
void Measure(const char* operation, int count, Function function) {
  const auto start = std::chrono::steady_clock::now();
  try {
    for (int i = 0; i < count; ++i) function();
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << operation << " count=" << count << " elapsed_ms=" << elapsed
              << std::endl;
  } catch (const std::exception& error) {
    std::cout << operation << " exception=" << error.what() << std::endl;
  }
}

int main() {
  std::cout << "pointer_bits=" << 8 * sizeof(void*) << std::endl;
  Measure("get_tzdb_cold", 1, [] {
    std::cout << "zone_count=" << std::chrono::get_tzdb().zones.size() << std::endl;
  });
  Measure("enumerate_names_warm", 100, [] {
    std::size_t length = 0;
    for (const auto& zone : std::chrono::get_tzdb().zones)
      length += zone.name().size();
    if (length == 0) throw std::runtime_error("Empty zone database");
  });
  Measure("current_zone_cold", 1, [] { std::chrono::current_zone(); });
  Measure("current_zone_warm", 100, [] { std::chrono::current_zone(); });
  Measure("london_info_warm", 100, [] {
    std::chrono::locate_zone("Europe/London")->get_info(
        std::chrono::system_clock::now());
  });
}
