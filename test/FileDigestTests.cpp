#include "FileDigest.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
  const auto path = std::filesystem::temp_directory_path() /
                    "xgrib-file-digest-test.txt";
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "abc";
  }
  const auto digest = xgrib::Sha256File(path);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  if (!digest ||
      *digest !=
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
    std::cerr << "SHA-256 known-answer test failed\n";
    return 1;
  }
  if (xgrib::Sha256File(path)) {
    std::cerr << "Missing file did not fail closed\n";
    return 1;
  }
  return 0;
}
