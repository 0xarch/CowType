// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "../compat/strings.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/pinyin/pinyinencoder.h>
#include <limits>
#include <memory>
#include <openssl/evp.h>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <unistd.h>
#include <vector>

#ifndef COWTYPE_LIBIME_VERSION
#define COWTYPE_LIBIME_VERSION "unknown"
#endif

namespace cowtype {
namespace fs = std::filesystem;
struct PinyinSources {
  std::string ice, wanxiang, system, cache, precompiled;
};
enum DictionaryWarning : unsigned {
  OptionalInvalid = 1,
  SystemUnavailable = 2
};
struct CompiledPinyin {
  std::unique_ptr<libime::PinyinDictionary> dictionary;
  unsigned warnings = 0;
  bool cacheHit = false, cacheWritten = false;
};

inline double logAdd(double a, double b) {
  if (a == -std::numeric_limits<double>::infinity())
    return b;
  if (b == -std::numeric_limits<double>::infinity())
    return a;
  if (a < b)
    std::swap(a, b);
  return a + std::log1p(std::pow(10., b - a)) / std::log(10.);
}
class Digest {
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx_{EVP_MD_CTX_new(),
                                                               EVP_MD_CTX_free};

public:
  Digest() {
    if (!ctx_ || EVP_DigestInit_ex(ctx_.get(), EVP_sha256(), nullptr) != 1)
      throw std::runtime_error("Dictionary digest initialization failed");
  }
  void add(const char *data, size_t size) {
    if (EVP_DigestUpdate(ctx_.get(), data, size) != 1)
      throw std::runtime_error("Dictionary digest update failed");
  }
  void add(const std::string &s) { add(s.data(), s.size()); }
  std::string finish() {
    unsigned char bytes[EVP_MAX_MD_SIZE];
    unsigned size = 0;
    if (EVP_DigestFinal_ex(ctx_.get(), bytes, &size) != 1)
      throw std::runtime_error("Dictionary digest failed");
    std::ostringstream out;
    for (unsigned i = 0; i < size; ++i)
      out << std::hex << std::setw(2) << std::setfill('0')
          << unsigned(bytes[i]);
    return out.str();
  }
};
inline std::string hashBytes(const std::string &s) {
  Digest hash;
  hash.add(s);
  return hash.finish();
}
inline std::string hashFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("Dictionary source cannot be read");
  Digest hash;
  std::array<char, 65536> buffer;
  while (in.read(buffer.data(), buffer.size()) || in.gcount())
    hash.add(buffer.data(), in.gcount());
  if (!in.eof())
    throw std::runtime_error("Dictionary source read failed");
  return hash.finish();
}
inline std::string sourceKey(const PinyinSources &s) {
  Digest key;
  key.add("cowtype-pinyin-1-sum-source-order\n" COWTYPE_LIBIME_VERSION "\n");
  key.add(hashFile(s.ice));
  for (unsigned source = 0; source < 2; ++source) {
    const auto &path = source == 0 ? s.wanxiang : s.system;
    key.add("\n");
    std::error_code error;
    if (path.empty())
      key.add("disabled");
    else if (!fs::exists(path, error))
      key.add(error ? "unreadable" : (source == 0 ? "disabled" : "absent"));
    else {
      try {
        key.add(hashFile(path));
      } catch (const std::exception &) {
        key.add("unreadable");
      }
    }
  }
  return key.finish();
}
inline std::unique_ptr<libime::PinyinDictionary> emptyLayers() {
  auto dict = std::make_unique<libime::PinyinDictionary>();
  dict->addEmptyDict();
  return dict;
}
// A compressed trie maps shared reading/text prefixes to dense IDs. The two
// columns hold log weights and source IDs; no per-entry String/vector is
// retained.
class PinyinCompiler {
  libime::DATrie<uint32_t> ids_;
  std::vector<double> weights_;
  std::vector<uint8_t> sources_;
  double total_ = -std::numeric_limits<double>::infinity();

public:
  bool add(std::string reading, const std::string &text, double logWeight,
           uint8_t source) {
    std::replace(reading.begin(), reading.end(), ' ', '\'');
    try {
      auto encoded = libime::PinyinEncoder::encodeFullPinyinWithFlags(
          reading, libime::PinyinFuzzyFlag::VE_UE);
      reading = libime::PinyinEncoder::decodeFullPinyin(encoded.data(),
                                                        encoded.size());
    } catch (const std::invalid_argument &) {
      return false;
    }
    const auto key = reading + '\t' + text;
    auto id = ids_.exactMatchSearch(key);
    if (libime::DATrie<uint32_t>::isValid(id)) {
      weights_[id] = logAdd(weights_[id], logWeight);
      sources_[id] = std::min(sources_[id], source);
    } else {
      if (weights_.size() >= uint64_t(std::numeric_limits<int32_t>::max()))
        throw std::runtime_error("Dictionary has too many entries");
      id = weights_.size();
      ids_.set(key, id);
      weights_.push_back(logWeight);
      sources_.push_back(source);
    }
    total_ = logAdd(total_, logWeight);
    return true;
  }
  std::unique_ptr<libime::PinyinDictionary> finish() const {
    if (weights_.empty())
      throw std::runtime_error("No supported pinyin dictionary entries");
    auto dict = emptyLayers();
    std::string key;
    ids_.foreach ([&](uint32_t id, size_t length, auto pos) {
      ids_.suffix(key, length, pos);
      const auto split = key.find('\t');
      dict->addWord(sources_[id], key.substr(0, split), key.substr(split + 1),
                    float(weights_[id] - total_));
      return true;
    });
    return dict;
  }
};
struct TextEntry {
  std::string reading, text;
  double logWeight;
};
inline TextEntry parseTSV(const std::string &line) {
  auto a = line.find('\t'), b = line.find('\t', a == line.npos ? 0 : a + 1);
  if (a == line.npos || b == line.npos || line.find('\t', b + 1) != line.npos)
    throw std::runtime_error("Invalid dictionary row");
  TextEntry e{line.substr(0, a), line.substr(a + 1, b - a - 1), 0};
  if (e.reading.empty() || e.text.empty() ||
      !std::all_of(e.reading.begin(), e.reading.end(),
                   [](unsigned char c) {
                     return (c >= 'a' && c <= 'z') || c == ' ' || c == '\'';
                   }) ||
      std::any_of(e.text.begin(), e.text.end(),
                  [](unsigned char c) { return c < 32 || c == 127; }))
    throw std::runtime_error("Invalid dictionary text or reading");
  // Validate UTF-8 without locale-dependent conversion or retaining decoded
  // strings.
  for (size_t i = 0; i < e.text.size();) {
    auto c = static_cast<unsigned char>(e.text[i++]);
    if (c < 128)
      continue;
    unsigned n;
    uint32_t cp, minimum;
    if (c >= 0xc2 && c <= 0xdf) {
      n = 1;
      cp = c & 31;
      minimum = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      n = 2;
      cp = c & 15;
      minimum = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      n = 3;
      cp = c & 7;
      minimum = 0x10000;
    } else
      throw std::runtime_error("Invalid dictionary UTF-8");
    while (n--) {
      if (i == e.text.size())
        throw std::runtime_error("Invalid dictionary UTF-8");
      auto next = static_cast<unsigned char>(e.text[i++]);
      if ((next & 0xc0) != 0x80)
        throw std::runtime_error("Invalid dictionary UTF-8");
      cp = (cp << 6) | (next & 63);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0x80 && cp <= 0x9f) ||
        (cp >= 0xd800 && cp <= 0xdfff))
      throw std::runtime_error("Invalid dictionary UTF-8");
  }
  size_t consumed = 0;
  double weight;
  try {
    weight = std::stod(line.substr(b + 1), &consumed);
  } catch (const std::exception &) {
    throw std::runtime_error("Invalid dictionary weight");
  }
  if (consumed != line.size() - b - 1 || !std::isfinite(weight) || weight <= 0)
    throw std::runtime_error("Invalid dictionary weight");
  e.logWeight = std::log10(weight);
  return e;
}
template <class Callback>
inline void readTSV(const std::string &path, Callback callback) {
  std::ifstream in(path);
  if (!in)
    throw std::runtime_error("Dictionary source cannot be read");
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty() || line[0] == '#')
      continue;
    callback(parseTSV(line));
  }
  if (!in.eof())
    throw std::runtime_error("Dictionary source read failed");
}
class LineSink : public std::streambuf {
  std::string line_;
  std::function<void(const std::string &)> callback_;
  int_type overflow(int_type c) override {
    if (traits_type::eq_int_type(c, traits_type::eof()))
      return traits_type::not_eof(c);
    if (char(c) == '\n') {
      callback_(line_);
      line_.clear();
    } else
      line_ += char(c);
    return c;
  }

public:
  explicit LineSink(std::function<void(const std::string &)> callback)
      : callback_(std::move(callback)) {}
};
inline CompiledPinyin compilePinyin(const PinyinSources &s) {
  CompiledPinyin result;
  auto compiler = std::make_unique<PinyinCompiler>();
  // A malformed optional file must not leave partially inserted entries.
  std::error_code optionalError;
  if (!s.wanxiang.empty() &&
      (fs::exists(s.wanxiang, optionalError) || optionalError)) {
    try {
      readTSV(s.wanxiang, [&](const TextEntry &e) {
        compiler->add(e.reading, e.text, e.logWeight, 1);
      });
    } catch (const std::exception &) {
      result.warnings |= OptionalInvalid;
      compiler = std::make_unique<PinyinCompiler>();
    }
  }
  if (!s.system.empty()) {
    libime::PinyinDictionary system;
    bool loaded = false;
    try {
      system.load(0, s.system.c_str(), libime::PinyinDictFormat::Binary);
      loaded = true;
    } catch (const std::exception &) {
      result.warnings |= SystemUnavailable;
    }
    if (loaded) {
      try {
        LineSink sink([&](const std::string &line) {
          std::string_view rest(line);
          std::vector<std::string> fields;
          while (!rest.empty()) {
            std::string field;
            auto used = cowtype::compat::consumeValue(rest, &field);
            if (used.empty())
              break;
            fields.push_back(std::move(field));
          }
          if (fields.size() != 3)
            throw std::runtime_error("Invalid system dictionary row");
          double cost = std::stod(fields[2]);
          if (!std::isfinite(cost))
            throw std::runtime_error("Invalid system dictionary score");
          compiler->add(fields[1], fields[0], cost, 0);
        });
        std::ostream stream(&sink);
        system.save(0, stream, libime::PinyinDictFormat::Text);
        stream.flush();
        if (!stream)
          throw std::runtime_error("System dictionary conversion failed");
      } catch (const std::exception &) {
        // A readable binary can still contain unusable scores. Discard every
        // partially imported system contribution before falling back.
        compiler.reset();
        auto fallback = s;
        fallback.system.clear();
        auto recovered = compilePinyin(fallback);
        recovered.warnings |= SystemUnavailable;
        return recovered;
      }
    }
  }
  readTSV(s.ice, [&](const TextEntry &e) {
    compiler->add(e.reading, e.text, e.logWeight, 2);
  });
  result.dictionary = compiler->finish();
  return result;
}
inline bool readCache(const fs::path &file, const std::string &key,
                      CompiledPinyin &result) {
  try {
    std::ifstream in(file, std::ios::binary);
    std::string line;
    auto header = [&]() {
      std::string value;
      for (unsigned i = 0; i < 256; ++i) {
        int c = in.get();
        if (c == '\n')
          return value;
        if (c == EOF)
          break;
        value += char(c);
      }
      throw std::runtime_error("Invalid dictionary cache header");
    };
    if (header() != "COWTYPE-PINYIN-1" || header() != key)
      return false;
    auto warnings = header();
    if (warnings != "0" && warnings != "1" && warnings != "2" &&
        warnings != "3")
      return false;
    auto dict = emptyLayers();
    for (size_t layer = 0; layer < 3; ++layer) {
      std::istringstream meta(header());
      uint64_t size;
      std::string digest, extra;
      if (!(meta >> size >> digest) || meta >> extra ||
          size > 512 * 1024 * 1024 || digest.size() != 64)
        return false;
      const auto position = in.tellg();
      const auto length = fs::file_size(file);
      if (position < 0 || static_cast<uint64_t>(position) > length ||
          size > length - static_cast<uint64_t>(position))
        return false;
      std::string bytes(size, '\0');
      if (!in.read(bytes.data(), size) || hashBytes(bytes) != digest)
        return false;
      std::istringstream data(std::move(bytes),
                              std::ios::in | std::ios::binary);
      dict->load(layer, data, libime::PinyinDictFormat::Binary);
    }
    if (in.peek() != EOF)
      return false;
    result.dictionary = std::move(dict);
    result.warnings = std::stoul(warnings);
    result.cacheHit = true;
    return true;
  } catch (const std::exception &) {
    return false;
  }
}
inline bool writeCache(const fs::path &root, const std::string &key,
                       const CompiledPinyin &result) {
  if (root.empty())
    return false;
  std::string temporary;
  try {
    fs::create_directories(root);
    temporary = (root / ".pinyin-XXXXXX").string();
    int fd = mkstemp(temporary.data());
    if (fd < 0)
      return false;
    close(fd);
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << "COWTYPE-PINYIN-1\n" << key << '\n' << result.warnings << '\n';
    for (size_t layer = 0; layer < 3; ++layer) {
      std::ostringstream data(std::ios::out | std::ios::binary);
      result.dictionary->save(layer, data, libime::PinyinDictFormat::Binary);
      auto bytes = std::move(data).str();
      out << bytes.size() << ' ' << hashBytes(bytes) << '\n';
      out.write(bytes.data(), bytes.size());
    }
    out.close();
    fs::rename(temporary, root / (key + ".ctdict"));
    return true;
  } catch (const std::exception &) {
    std::error_code error;
    if (!temporary.empty())
      fs::remove(temporary, error);
    return false;
  }
}
inline CompiledPinyin loadPinyin(const PinyinSources &sources) {
  const auto key = sourceKey(sources);
  CompiledPinyin result;
  for (const auto &root : {sources.precompiled, sources.cache})
    if (!root.empty() &&
        readCache(fs::path(root) / (key + ".ctdict"), key, result))
      return result;
  result = compilePinyin(sources);
  // Never publish a cache under a key computed from older source contents.
  if (sourceKey(sources) == key)
    result.cacheWritten = writeCache(sources.cache, key, result);
  return result;
}
} // namespace cowtype
