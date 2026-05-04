#include <blockstore/implementations/encrypted/EncryptedBlockStore2.h>
#include <blockstore/implementations/inmemory/InMemoryBlockStore2.h>
#include <cpp-utils/crypto/symmetric/ciphers.h>
#include <cpp-utils/data/Data.h>
#include <cpp-utils/pointer/unique_ref.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using blockstore::BlockId;
using blockstore::encrypted::EncryptedBlockStore2;
using blockstore::inmemory::InMemoryBlockStore2;
using cpputils::Data;
using cpputils::EncryptionKey;
using cpputils::make_unique_ref;
using Clock = std::chrono::steady_clock;

namespace {

struct CipherSpec final {
  std::string name;
  void (*run)(const std::vector<size_t> &, const std::vector<std::string> &, double);
};

struct Result final {
  std::string cipher;
  std::string op;
  size_t blockSize;
  uint64_t iterations;
  uint64_t bytes;
  double seconds;
  uint64_t checksum;
};

std::vector<std::string> split(const std::string &value) {
  std::vector<std::string> result;
  std::stringstream stream(value);
  std::string item;
  while (std::getline(stream, item, ',')) {
    if (!item.empty()) {
      result.push_back(item);
    }
  }
  return result;
}

std::vector<size_t> parseSizes(const std::string &value) {
  std::vector<size_t> result;
  for (const auto &item : split(value)) {
    result.push_back(static_cast<size_t>(std::stoull(item)));
  }
  return result;
}

bool contains(const std::vector<std::string> &values, const std::string &value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

Data makePlaintext(size_t size) {
  Data data(size);
  auto *bytes = static_cast<uint8_t *>(data.data());
  for (size_t i = 0; i < size; ++i) {
    bytes[i] = static_cast<uint8_t>((i * 131u + 17u) & 0xffu);
  }
  return data;
}

uint64_t checksumOf(const Data &data) {
  if (data.size() == 0) {
    return 0;
  }
  const auto *bytes = static_cast<const uint8_t *>(data.data());
  return static_cast<uint64_t>(bytes[0]) + static_cast<uint64_t>(bytes[data.size() - 1]);
}

void printHeader() {
  std::cout << "cipher,op,block_size,iterations,bytes,seconds,mib_per_s,checksum\n";
}

void printResult(const Result &result) {
  const double mib = static_cast<double>(result.bytes) / (1024.0 * 1024.0);
  const double mibPerSecond = mib / result.seconds;
  std::cout << result.cipher << ','
            << result.op << ','
            << result.blockSize << ','
            << result.iterations << ','
            << result.bytes << ','
            << std::fixed << std::setprecision(6) << result.seconds << ','
            << std::fixed << std::setprecision(2) << mibPerSecond << ','
            << result.checksum << '\n';
}

template<class Func>
Result timeLoop(const std::string &cipher, const std::string &op, size_t blockSize, double seconds, Func func) {
  const auto start = Clock::now();
  const auto minDuration = std::chrono::duration<double>(seconds);
  uint64_t iterations = 0;
  uint64_t checksum = 0;

  do {
    checksum += func();
    ++iterations;
  } while (Clock::now() - start < minDuration);

  const auto stop = Clock::now();
  const double elapsed = std::chrono::duration<double>(stop - start).count();
  return Result{cipher, op, blockSize, iterations, iterations * static_cast<uint64_t>(blockSize), elapsed, checksum};
}

template<class Cipher>
Result benchRawEncrypt(size_t blockSize, double seconds) {
  auto plaintext = makePlaintext(blockSize);
  const auto key = EncryptionKey::Null(Cipher::KEYSIZE);
  return timeLoop(Cipher::NAME, "raw_encrypt", blockSize, seconds, [&] {
    auto encrypted = Cipher::encrypt(static_cast<const CryptoPP::byte *>(plaintext.data()), plaintext.size(), key);
    return checksumOf(encrypted);
  });
}

template<class Cipher>
Result benchRawDecrypt(size_t blockSize, double seconds) {
  auto plaintext = makePlaintext(blockSize);
  const auto key = EncryptionKey::Null(Cipher::KEYSIZE);
  auto encrypted = Cipher::encrypt(static_cast<const CryptoPP::byte *>(plaintext.data()), plaintext.size(), key);
  return timeLoop(Cipher::NAME, "raw_decrypt", blockSize, seconds, [&] {
    auto decrypted = Cipher::decrypt(static_cast<const CryptoPP::byte *>(encrypted.data()), encrypted.size(), key);
    if (decrypted == boost::none) {
      throw std::runtime_error("decrypt failed");
    }
    return checksumOf(*decrypted);
  });
}

template<class Cipher>
Result benchBlockStore(size_t blockSize, double seconds, bool load) {
  auto plaintext = makePlaintext(blockSize);
  const auto key = EncryptionKey::Null(Cipher::KEYSIZE);
  EncryptedBlockStore2<Cipher> store(make_unique_ref<InMemoryBlockStore2>(), key);
  const auto blockId = BlockId::Random();
  store.store(blockId, plaintext);

  if (load) {
    return timeLoop(Cipher::NAME, "blockstore_load", blockSize, seconds, [&] {
      auto loaded = store.load(blockId);
      if (loaded == boost::none) {
        throw std::runtime_error("load failed");
      }
      return checksumOf(*loaded);
    });
  }

  return timeLoop(Cipher::NAME, "blockstore_store", blockSize, seconds, [&] {
    store.store(blockId, plaintext);
    return static_cast<uint64_t>(store.numBlocks());
  });
}

template<class Cipher>
void runCipher(const std::vector<size_t> &sizes, const std::vector<std::string> &ops, double seconds) {
  for (const size_t size : sizes) {
    if (contains(ops, "raw_encrypt")) {
      printResult(benchRawEncrypt<Cipher>(size, seconds));
    }
    if (contains(ops, "raw_decrypt")) {
      printResult(benchRawDecrypt<Cipher>(size, seconds));
    }
    if (contains(ops, "blockstore_store")) {
      printResult(benchBlockStore<Cipher>(size, seconds, false));
    }
    if (contains(ops, "blockstore_load")) {
      printResult(benchBlockStore<Cipher>(size, seconds, true));
    }
  }
}

template<class Cipher>
CipherSpec cipherSpec() {
  return CipherSpec{Cipher::NAME, runCipher<Cipher>};
}

std::vector<CipherSpec> allCiphers() {
  return {
    cipherSpec<cpputils::XChaCha20Poly1305>(),
    cipherSpec<cpputils::AES256_GCM>(),
    cipherSpec<cpputils::AES256_CFB>(),
    cipherSpec<cpputils::AES128_GCM>(),
    cipherSpec<cpputils::AES128_CFB>(),
    cipherSpec<cpputils::Twofish256_GCM>(),
    cipherSpec<cpputils::Twofish256_CFB>(),
    cipherSpec<cpputils::Twofish128_GCM>(),
    cipherSpec<cpputils::Twofish128_CFB>(),
    cipherSpec<cpputils::Serpent256_GCM>(),
    cipherSpec<cpputils::Serpent256_CFB>(),
    cipherSpec<cpputils::Serpent128_GCM>(),
    cipherSpec<cpputils::Serpent128_CFB>(),
    cipherSpec<cpputils::Cast256_GCM>(),
    cipherSpec<cpputils::Cast256_CFB>(),
    cipherSpec<cpputils::Mars448_GCM>(),
    cipherSpec<cpputils::Mars448_CFB>(),
    cipherSpec<cpputils::Mars256_GCM>(),
    cipherSpec<cpputils::Mars256_CFB>(),
    cipherSpec<cpputils::Mars128_GCM>(),
    cipherSpec<cpputils::Mars128_CFB>(),
  };
}

void usage(const char *argv0) {
  std::cerr
    << "Usage: " << argv0 << " [--seconds N] [--sizes a,b,c] [--ciphers a,b,c] [--ops a,b,c]\n"
    << "\n"
    << "Default sizes: 4096,16384,65536,1048576\n"
    << "Default ops: raw_encrypt,raw_decrypt,blockstore_store,blockstore_load\n";
}

} // namespace

int main(int argc, char **argv) {
  double seconds = 0.25;
  std::vector<size_t> sizes = {4096, 16384, 65536, 1048576};
  std::vector<std::string> ops = {"raw_encrypt", "raw_decrypt", "blockstore_store", "blockstore_load"};
  std::vector<std::string> selectedCiphers;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help") {
      usage(argv[0]);
      return 0;
    }
    if (i + 1 >= argc) {
      usage(argv[0]);
      return 2;
    }
    const std::string value = argv[++i];
    if (arg == "--seconds") {
      seconds = std::stod(value);
    } else if (arg == "--sizes") {
      sizes = parseSizes(value);
    } else if (arg == "--ciphers") {
      selectedCiphers = split(value);
    } else if (arg == "--ops") {
      ops = split(value);
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  printHeader();
  for (const auto &cipher : allCiphers()) {
    if (selectedCiphers.empty() || contains(selectedCiphers, cipher.name)) {
      cipher.run(sizes, ops, seconds);
    }
  }

  return 0;
}
