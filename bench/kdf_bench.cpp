#include <cpp-utils/crypto/kdf/Argon2id.h>
#include <cpp-utils/crypto/kdf/SensitivePassword.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void usage(const char *argv0) {
  std::cerr
    << "Usage: " << argv0
    << " [--memory-kib N] [--iterations N] [--parallelism N]\n";
}

}

int main(int argc, char **argv) {
  auto settings = cpputils::Argon2id::DefaultSettings;
  for (int i = 1; i < argc; i += 2) {
    if (i + 1 >= argc) {
      usage(argv[0]);
      return 2;
    }
    const std::string option = argv[i];
    const auto value = static_cast<uint32_t>(std::stoul(argv[i + 1]));
    if (option == "--memory-kib") {
      settings.memoryKiB = value;
    } else if (option == "--iterations") {
      settings.iterations = value;
    } else if (option == "--parallelism") {
      settings.parallelism = value;
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  auto password = cpputils::SensitivePassword::FromString(
    "cryfs-argon2id-benchmark-password");
  cpputils::Argon2id argon2id(settings);

  const auto start = std::chrono::steady_clock::now();
  const auto result = argon2id.deriveNewKey(32, password);
  const auto stop = std::chrono::steady_clock::now();
  const auto seconds = std::chrono::duration<double>(stop - start).count();

  const auto firstByte = *static_cast<const uint8_t*>(result.key.data());
  std::cout << "memory_kib,iterations,parallelism,seconds,check\n"
            << settings.memoryKiB << ',' << settings.iterations << ','
            << settings.parallelism << ',' << seconds << ','
            << static_cast<unsigned int>(firstByte) << '\n';
  return 0;
}
