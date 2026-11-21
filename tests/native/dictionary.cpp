// SPDX-License-Identifier: GPL-3.0-or-later
#include "libime.h"
#include <cassert>
#include <iostream>
struct Result {
  std::string text;
  size_t consumed;
};
static void emit(void *p, const char *text, const char *, size_t consumed,
                 uint32_t) {
  static_cast<std::vector<Result> *>(p)->push_back({text, consumed});
}
static std::vector<Result> query(LibimeDecoder &decoder, const char *raw) {
  std::vector<Result> out;
  decoder.queryImpl(raw, 9, emit, &out);
  return out;
}
static void put(const std::filesystem::path &path, const std::string &data) {
  std::ofstream(path) << data;
}
int main() {
  char pattern[] = "/tmp/cowtype-dictionary-XXXXXX";
  const auto root = std::filesystem::path(mkdtemp(pattern));
  assert(!root.empty());
  struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() { std::filesystem::remove_all(root); }
  } cleanup{root};
  const auto ice = root / "ice.tsv", wan = root / "wan.tsv",
             system = root / "system.dict";
  put(ice, "lin\t林\t100\nlin\t林\t10\nlin\t林\t1\nlin\t临\t110\nlu\t路\t900000"
           "0\nlu wei\t芦苇\t1\n");
  put(wan, "lu\t鹿\t1000000\n");
  libime::PinyinDictionary dict;
  dict.addWord(0, "lu", "陆", 0);
  dict.addWord(0, "lin", "林", -1);
  dict.save(0, system.c_str(), libime::PinyinDictFormat::Binary);
  cowtype::PinyinSources s{ice, wan, system, (root / "cache").string(), ""};
  LibimeDecoder cold(s, false);
  assert(!cold.cacheHit && !cold.modelLoaded && !cold.warnings);
  auto lin = query(cold, "lin");
  assert(lin.size() >= 2 && lin[0].text == "林" && lin[1].text == "临");
  // Reordering contributions must not restore the old last-row-wins behavior.
  auto reordered = s;
  reordered.ice = (root / "reordered.tsv").string();
  put(reordered.ice, "lin\t林\t1\nlin\t临\t110\nlin\t林\t10\nlin\t林\t100\n");
  LibimeDecoder reverse(reordered, false);
  auto reversed = query(reverse, "lin");
  assert(reversed[0].text == lin[0].text && reversed[1].text == lin[1].text);
  auto lu = query(cold, "lu");
  assert(lu.size() >= 3 && lu[0].text == "陆" && lu[1].text == "鹿" &&
         lu[2].text == "路");
  auto full = query(cold, "luwei");
  assert(full[0].text == "芦苇" && full[0].consumed == 5);
  LibimeDecoder warm(s, false);
  assert(warm.cacheHit);
  auto packaged = s;
  packaged.precompiled = s.cache;
  packaged.cache = "";
  LibimeDecoder packageCache(packaged, false);
  assert(packageCache.cacheHit);
  // Missing optional file and explicitly disabled optional source are
  // identical.
  auto noOptional = s;
  noOptional.wanxiang = "";
  auto missingOptional = noOptional;
  missingOptional.wanxiang = (root / "absent.tsv").string();
  assert(cowtype::sourceKey(noOptional) == cowtype::sourceKey(missingOptional));
  for (const auto *raw : {"l", "li", "lin", "lu", "luw", "luwei"}) {
    const auto a = query(cold, raw), b = query(warm, raw);
    assert(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
      assert(a[i].text == b[i].text && a[i].consumed == b[i].consumed);
  }
  // Frequency is log10(sum), including cross-source contributions. Dump the
  // public text format rather than depending on libime's internal trie bits.
  auto compiled = cowtype::loadPinyin(s);
  std::ostringstream text;
  compiled.dictionary->save(2, text, libime::PinyinDictFormat::Text);
  std::ostringstream systemText;
  compiled.dictionary->save(0, systemText, libime::PinyinDictFormat::Text);
  auto score = [](const std::string &dump, const std::string &word) {
    std::istringstream rows(dump);
    std::string text, reading;
    double value;
    while (rows >> text >> reading >> value)
      if (text == word)
        return value;
    assert(false);
    return 0.;
  };
  assert(std::abs((score(systemText.str(), "林") - score(text.str(), "临")) -
                  std::log10(111.1 / 110.)) < 1e-5);
  // Canonical collisions (ve/ue) also combine before logarithmic scoring.
  cowtype::PinyinCompiler canonical;
  assert(canonical.add("lve", "略", std::log10(100.), 2));
  assert(canonical.add("lue", "略", std::log10(10.), 1));
  assert(canonical.add("lue", "掠", std::log10(1.), 2));
  auto combined = canonical.finish();
  std::ostringstream merged;
  combined->save(1, merged, libime::PinyinDictFormat::Text);
  assert(std::abs(score(merged.str(), "略") - std::log10(110. / 111.)) < 1e-5);
  const auto oldKey = cowtype::sourceKey(s);
  put(wan, "lu\t露\t1\n");
  assert(cowtype::sourceKey(s) != oldKey);
  LibimeDecoder changed(s, false);
  assert(!changed.cacheHit && query(changed, "lu")[1].text == "露");
  put(wan, "lu\t露\t1\nbroken\n");
  LibimeDecoder invalid(s, false);
  assert(invalid.warnings & cowtype::OptionalInvalid);
  const auto withoutWan = query(invalid, "lu");
  assert(withoutWan[1].text == "路");
  auto key = cowtype::sourceKey(s);
  auto cache = root / "cache" / (key + ".ctdict");
  put(cache, "COWTYPE-PINYIN-1\n" + key + "\n0\n536870912 " +
                 std::string(64, 'a') + "\n");
  cowtype::CompiledPinyin damaged;
  assert(!cowtype::readCache(cache, key, damaged));
  LibimeDecoder repaired(s, false);
  assert(!repaired.cacheHit);
  LibimeDecoder repairedWarm(s, false);
  assert(repairedWarm.cacheHit);
  s.cache = (root / "not-a-directory").string();
  put(s.cache, "file");
  s.precompiled = "";
  s.wanxiang = "";
  LibimeDecoder unwritable(s, false);
  assert(query(unwritable, "lin")[0].text == "林");
  setenv("COWTYPE_STATISTICAL_MODEL_FILE", (root / "missing.lm").c_str(), 1);
  LibimeDecoder missingModel(s, true);
  assert(!missingModel.modelLoaded &&
         (missingModel.warnings & LibimeDecoder::ModelUnavailable));
  assert(query(missingModel, "lin")[0].text == "林");
  setenv("COWTYPE_STATISTICAL_MODEL_FILE", "", 1);
  LibimeDecoder emptyModel(s, true);
  assert(!emptyModel.modelLoaded &&
         (emptyModel.warnings & LibimeDecoder::ModelUnavailable));
  auto brokenSystem = s;
  brokenSystem.system = (root / "broken.dict").string();
  put(brokenSystem.system, "corrupt");
  LibimeDecoder badSystem(brokenSystem, false);
  assert((badSystem.warnings & cowtype::SystemUnavailable) &&
         query(badSystem, "lin")[0].text == "林");
  libime::PinyinDictionary invalidScore;
  invalidScore.addWord(0, "lin", "林", std::numeric_limits<float>::quiet_NaN());
  invalidScore.save(0, brokenSystem.system.c_str(),
                    libime::PinyinDictFormat::Binary);
  LibimeDecoder badScore(brokenSystem, false);
  assert((badScore.warnings & cowtype::SystemUnavailable) &&
         query(badScore, "lin")[0].text == "林");
  // A real installed local model is optional; no test downloads or user data.
  if (std::filesystem::exists("/usr/lib/libime/zh_CN.lm")) {
    setenv("COWTYPE_STATISTICAL_MODEL_FILE", "/usr/lib/libime/zh_CN.lm", 1);
    LibimeDecoder model(s, true);
    assert(model.modelLoaded && query(model, "lu")[0].text == "陆");
  }
  std::cout << "dictionary: summed duplicates, source tiers, full matches, "
               "cache parity/invalidation/repair and optional model verified\n";
}
