#include "MyanglishConverter.h"
#include "Dictionary.h"
#include "UnicodeUtils.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace {

std::filesystem::path testDictionaryPath() {
    return std::filesystem::path(MYANGLISHIME_SOURCE_DIR) / "tests" / "test_data.csv";
}

struct TestSuite {
    int passed = 0;
    int failed = 0;

    void expectTrue(bool condition, const std::string& name) {
        if (condition) {
            ++passed;
            std::cout << "PASS: " << name << '\n';
        } else {
            ++failed;
            std::cout << "FAIL: " << name << '\n';
        }
    }

    void expectEqual(const std::string& actual, const std::string& expected, const std::string& name) {
        expectTrue(actual == expected, name + "\n  expected: " + expected + "\n  actual:   " + actual);
    }

    void expectEqualSize(std::size_t actual, std::size_t expected, const std::string& name) {
        expectTrue(actual == expected, name + "\n  expected: " + std::to_string(expected) + "\n  actual:   " + std::to_string(actual));
    }

    void expectContains(const std::vector<myanglish::Candidate>& candidates, const std::string& expected, const std::string& name) {
        const bool found = std::any_of(candidates.begin(), candidates.end(), [&](const myanglish::Candidate& candidate) {
            return candidate.burmese == expected;
        });
        expectTrue(found, name + "\n  expected candidate: " + expected);
    }

    void expectCandidate(
        const std::vector<myanglish::Candidate>& candidates,
        std::size_t index,
        const std::string& burmese,
        int frequency,
        const std::string& name
    ) {
        if (index >= candidates.size()) {
            expectTrue(false, name + "\n  candidate index missing");
            return;
        }
        expectTrue(
            candidates[index].burmese == burmese && candidates[index].frequency == frequency,
            name + "\n  expected: " + burmese + " / " + std::to_string(frequency) +
            "\n  actual:   " + candidates[index].burmese + " / " + std::to_string(candidates[index].frequency)
        );
    }
};

myanglish::MyanglishConverter loadConverterOrThrow() {
    myanglish::Dictionary dictionary;
    std::string errorMessage;
    if (!dictionary.loadFromCsv(testDictionaryPath(), &errorMessage)) {
        throw std::runtime_error(errorMessage);
    }
    const auto alpha08 = std::filesystem::path(MYANGLISHIME_SOURCE_DIR) / "data" / "alpha08_candidates.csv";
    std::string extraError;
    if (!dictionary.appendFromCsv(alpha08, &extraError, true)) {
        throw std::runtime_error(extraError);
    }
    return myanglish::MyanglishConverter(std::move(dictionary));
}

} // namespace

int main() {
    try {
        const auto converter = loadConverterOrThrow();
        TestSuite tests;

        tests.expectEqual(converter.convertSentence("mingalar par"), u8"မင်္ဂလာပါ", "Exact word conversion");
        tests.expectEqual(converter.convertSentence("nay kaung lar"), u8"နေကောင်းလား", "Complete phrase conversion");
        tests.expectEqual(converter.convertSentence("kyay zu tin par tal"), u8"ကျေးဇူးတင်ပါတယ်", "Longest phrase conversion");

        const auto candidates = converter.getCandidates("sa");
        tests.expectTrue(candidates.size() >= 3, "Multiple candidates include stable top three");
        if (candidates.size() == 3) {
            tests.expectEqual(candidates[0].burmese, u8"စာ", "Candidate ranking first");
            tests.expectEqual(candidates[1].burmese, u8"စား", "Candidate ranking second");
            tests.expectEqual(candidates[2].burmese, u8"ဆာ", "Candidate ranking third");
        }

        tests.expectEqual(converter.convertSentence("MINGALAR PAR"), u8"မင်္ဂလာပါ", "Uppercase input");
        tests.expectEqual(converter.convertSentence("  mingalar   par  "), u8"မင်္ဂလာပါ", "Extra spaces");
        tests.expectEqual(converter.convertSentence("hello mingalar"), u8"hello မင်္ဂလာ", "Unknown words");
        tests.expectEqual(converter.convertSentence("kyezu"), u8"ကျေးဇူး", "Alternative spellings");
        tests.expectEqual(converter.convertSentence(""), "", "Empty input");
        tests.expectEqual(converter.convertSentence("kyayzu"), u8"ကျေးဇူး", "Burmese Unicode output");
        tests.expectEqual(converter.convertSentence("ar"), u8"အာ", "Standalone ar");
        tests.expectEqual(converter.convertSentence("i"), u8"အိ", "Standalone i");
        tests.expectEqual(converter.convertSentence("ii"), u8"အီ", "Standalone ii");
        tests.expectEqual(converter.convertSentence("u"), u8"အု", "Standalone u");
        tests.expectEqual(converter.convertSentence("uu"), u8"အူ", "Standalone uu");
        tests.expectEqual(converter.convertSentence("e"), u8"အေ", "Standalone e");
        tests.expectEqual(converter.convertSentence("ai"), u8"အဲ", "Standalone ai");
        tests.expectEqual(converter.convertSentence("o"), u8"အို", "Standalone o");
        tests.expectEqual(converter.convertSentence("aw"), u8"အော", "Standalone aw");
        tests.expectEqual(converter.convertSentence("kar"), u8"ကာ", "Base consonant kar");
        tests.expectEqual(converter.convertSentence("karr"), u8"ကား", "Tone mark karr");
        tests.expectEqual(converter.convertSentence("nar"), u8"နာ", "Base consonant nar");
        tests.expectEqual(converter.convertSentence("narr"), u8"နား", "Tone mark narr");
        tests.expectEqual(converter.convertSentence("khar"), u8"ခါ", "Base consonant khar");
        tests.expectEqual(converter.convertSentence("ngar"), u8"ငါ", "Base consonant ngar");
        tests.expectEqual(converter.convertSentence("mhar"), u8"မှာ", "Medial mhar");
        tests.expectEqual(converter.convertSentence("mharr"), u8"မှား", "Tone mark mharr");
        tests.expectEqual(converter.convertSentence("kra"), u8"ကြ", "Medial kra");
        tests.expectEqual(converter.convertSentence("in"), u8"အင်", "Rhyme in");
        tests.expectEqual(converter.convertSentence("kin"), u8"ကင်", "Base consonant kin");
        tests.expectEqual(converter.convertSentence("min"), u8"မင်", "Base consonant min");
        tests.expectEqual(converter.convertSentence("an"), u8"အန်", "Rhyme an");
        tests.expectEqual(converter.convertSentence("kan"), u8"ကန်", "Candidate default kan");

        const auto kanCandidates = converter.getCandidates("kan", 9);
        tests.expectTrue(kanCandidates.size() >= 5, "Candidate support kan expanded");
        tests.expectContains(kanCandidates, u8"ကန်", "Candidate support kan contains ကန်");
        tests.expectContains(kanCandidates, u8"ကန်း", "Candidate support kan contains ကန်း");
        tests.expectContains(kanCandidates, u8"ကမ်", "Candidate support kan contains ကမ်");
        tests.expectContains(kanCandidates, u8"ကမ်း", "Candidate support kan contains ကမ်း");
        tests.expectContains(kanCandidates, u8"ကံ", "Candidate support kan contains ကံ");

        // Alpha 0.7 ranked family behavior requested by the user.
        const auto kinCandidates = converter.getCandidates("kin", 9);
        tests.expectTrue(kinCandidates.size() >= 3, "kin has at least three family candidates");
        if (kinCandidates.size() >= 3) {
            tests.expectEqual(kinCandidates[0].burmese, u8"ကင်", "kin candidate 1 -> ကင်");
            tests.expectEqual(kinCandidates[1].burmese, u8"ကင်း", "kin candidate 2 -> ကင်း");
            tests.expectEqual(kinCandidates[2].burmese, u8"ကင့်", "kin candidate 3 -> ကင့်");
        }

        const auto kinnCandidates = converter.getCandidates("kinn", 9);
        tests.expectTrue(kinnCandidates.size() >= 3, "kinn has at least three family candidates");
        if (kinnCandidates.size() >= 3) {
            tests.expectEqual(kinnCandidates[0].burmese, u8"ကင်း", "kinn candidate 1 -> ကင်း");
            tests.expectEqual(kinnCandidates[1].burmese, u8"ကင်", "kinn candidate 2 -> ကင်");
            tests.expectEqual(kinnCandidates[2].burmese, u8"ကင့်", "kinn candidate 3 -> ကင့်");
        }

        const auto kintCandidates = converter.getCandidates("kint", 9);
        tests.expectTrue(kintCandidates.size() >= 3, "kint has at least three family candidates");
        if (kintCandidates.size() >= 3) {
            tests.expectEqual(kintCandidates[0].burmese, u8"ကင့်", "kint candidate 1 -> ကင့်");
            tests.expectEqual(kintCandidates[1].burmese, u8"ကင်", "kint candidate 2 -> ကင်");
            tests.expectEqual(kintCandidates[2].burmese, u8"ကင်း", "kint candidate 3 -> ကင်း");
        }

        // Alpha 0.8 live-word and user-approved exact mappings.
        tests.expectEqual(converter.convertSentence("ya"), u8"ရ", "Alpha 0.8 ya -> ရ");

        const auto pyiCandidates = converter.getCandidates("pyi", 9);
        tests.expectTrue(pyiCandidates.size() >= 2, "pyi has live alternatives");
        if (pyiCandidates.size() >= 2) {
            tests.expectEqual(pyiCandidates[0].burmese, u8"ပြီ", "pyi candidate 1 -> ပြီ");
            tests.expectEqual(pyiCandidates[1].burmese, u8"ပျီ", "pyi candidate 2 -> ပျီ");
        }

        const auto paungCandidates = converter.getCandidates("paung", 9);
        tests.expectTrue(paungCandidates.size() >= 3, "paung has three approved candidates");
        if (paungCandidates.size() >= 3) {
            tests.expectEqual(paungCandidates[0].burmese, u8"ပေါင်", "paung candidate 1 -> ပေါင်");
            tests.expectEqual(paungCandidates[1].burmese, u8"ပေါင်း", "paung candidate 2 -> ပေါင်း");
            tests.expectEqual(paungCandidates[2].burmese, u8"ပေါင့်", "paung candidate 3 -> ပေါင့်");
        }

        tests.expectEqual(converter.convertSentence("aung"), u8"အောင်", "Standalone aung -> အောင်");
        tests.expectEqual(converter.convertSentence("ag"), u8"အောင်", "Ag/ag -> အောင်");
        tests.expectEqual(converter.convertSentence("mg"), u8"မောင်", "Mg/mg -> မောင်");

        const auto pgCandidates = converter.getCandidates("pg", 9);
        tests.expectTrue(!pgCandidates.empty(), "-g generates a candidate");
        if (!pgCandidates.empty()) {
            tests.expectEqual(pgCandidates[0].burmese, u8"ပေါင်", "-g ranks ောင် first");
        }
        const auto pggCandidates = converter.getCandidates("pgg", 9);
        tests.expectTrue(!pggCandidates.empty(), "-gg generates a candidate");
        if (!pggCandidates.empty()) {
            tests.expectEqual(pggCandidates[0].burmese, u8"ပေါင်း", "-gg ranks ောင်း first");
        }

        tests.expectTrue(converter.hasExactInput("ya"), "ya is a validated current word");

        // Alpha-0.8.1 rolling-buffer behavior: never decide the boundary BEFORE
        // seeing the new character. The whole current input wins whenever it is
        // still a valid word/prefix.
        tests.expectEqualSize(converter.findRollingSplit("bank"), 0, "bank remains one complete current word");
        tests.expectEqualSize(converter.findRollingSplit("bankp"), 4, "bankp detects pending bank|p boundary without splitting bank early");
        tests.expectEqualSize(converter.findRollingSplit("bankpy"), 4, "bankpy confirms bank|py once the next word is valid");
        tests.expectEqualSize(converter.findRollingSplit("yap"), 0, "yap stays whole while yap itself is valid");
        tests.expectEqualSize(converter.findRollingSplit("yapy"), 2, "yapy prefers ya|py once the combined whole input is invalid");
        tests.expectEqualSize(converter.findRollingSplit("yapyi"), 2, "yapyi keeps longest valid right word pyi");
        tests.expectEqualSize(converter.findRollingSplit("kin"), 0, "kin remains one word");
        tests.expectEqualSize(converter.findRollingSplit("kinn"), 0, "kinn remains one word");
        tests.expectEqualSize(converter.findRollingSplit("kint"), 0, "kint remains one word");
        tests.expectEqualSize(converter.findRollingSplit("sinn"), 0, "sinn extends sin instead of committing sin early");
        tests.expectEqualSize(converter.findRollingSplit("sinp"), 3, "sinp detects pending sin|p boundary");
        tests.expectEqualSize(converter.findRollingSplit("sinpy"), 3, "sinpy confirms sin|py boundary");
        tests.expectEqualSize(converter.findRollingSplit("paung"), 0, "paung is never split into shorter valid pieces");

        const auto bankCandidates = converter.getCandidates("bank", 9);
        tests.expectTrue(!bankCandidates.empty(), "bank loanword candidate exists");
        if (!bankCandidates.empty()) {
            tests.expectEqual(bankCandidates[0].burmese, u8"ဘဏ်", "bank loanword is first when no stable core mapping exists");
        }

        const auto aiCandidates = converter.getCandidates("ai", 9);
        tests.expectTrue(!aiCandidates.empty(), "ai candidates exist");
        if (!aiCandidates.empty()) {
            tests.expectEqual(aiCandidates[0].burmese, u8"အဲ", "short ai keeps stable phonetic candidate first");
        }
        tests.expectContains(aiCandidates, u8"အေအိုင်", "AI loanword remains available after phonetic candidate");

        const auto beCandidates08 = converter.getCandidates("be", 9);
        tests.expectTrue(!beCandidates08.empty(), "be candidates exist");
        if (!beCandidates08.empty()) {
            tests.expectEqual(beCandidates08[0].burmese, u8"ပဲ", "stable be -> ပဲ is not replaced by historical candidates");
        }
        tests.expectContains(beCandidates08, u8"ဘီ", "historical be -> ဘီ remains available as a later candidate");

        const auto inCandidates08 = converter.getCandidates("in", 9);
        tests.expectTrue(!inCandidates08.empty(), "in candidates exist");
        if (!inCandidates08.empty()) {
            tests.expectEqual(inCandidates08[0].burmese, u8"အင်", "short in keeps phonetic candidate first");
        }
        tests.expectContains(inCandidates08, u8"အိမ်", "historical in -> အိမ် remains available later");

        tests.expectEqual(converter.convertSentence("ker"), u8"ကား", "Master main er -> ား");
        tests.expectEqual(converter.convertSentence("kee"), u8"ကီး", "Master main ee -> ီး");
        tests.expectEqual(converter.convertSentence("kinn"), u8"ကင်း", "Master main inn -> င်း");
        tests.expectEqual(converter.convertSentence("kis"), u8"ကစ်", "Master variant is -> စ်");
        tests.expectEqual(converter.convertSentence("kwat"), u8"ကွက်", "Master direct wat -> ွက်");
        const auto kineCandidates = converter.getCandidates("kine", 9);
        tests.expectTrue(
            std::none_of(kineCandidates.begin(), kineCandidates.end(), [](const myanglish::Candidate& candidate) {
                return candidate.burmese == u8"ကိုင့်";
            }),
            "Alpha 0.7 does not generate unused ိုင့် family"
        );
        tests.expectEqual(converter.convertSentence("ta"), u8"တစ်", "Requested mapping ta -> တစ်");
        tests.expectEqual(converter.convertSentence("tha"), u8"သ", "Base mapping tha -> သ");
        tests.expectEqual(converter.convertSentence("ha"), u8"ဟ", "Base mapping ha -> ဟ");
        tests.expectEqual(converter.convertSentence("da"), u8"ဒ", "Base mapping da -> ဒ");
        tests.expectEqual(converter.convertSentence("dar"), u8"ဒါ", "Tall AA dar -> ဒါ");
        tests.expectEqual(converter.convertSentence("thy"), u8"သေး", "Lexical spelling thy -> သေး");
        tests.expectEqual(converter.convertSentence("thay"), u8"သေး", "Lexical spelling thay -> သေး");

        tests.expectEqual(converter.convertSentence("pyaw"), u8"ပြော", "Explicit pyaw -> ပြော");
        tests.expectEqual(converter.convertSentence("thone"), u8"သုံး", "Explicit thone -> သုံး");
        tests.expectEqual(converter.convertSentence("sin"), u8"ဆင်", "Explicit sin -> ဆင်");
        tests.expectEqual(converter.convertSentence("pl"), u8"ပဲ", "Explicit pl -> ပဲ");
        tests.expectEqual(converter.convertSentence("pal"), u8"ပဲ", "Explicit pal -> ပဲ");
        tests.expectEqual(converter.convertSentence("pe"), u8"ပဲ", "Explicit pe -> ပဲ");
        tests.expectEqual(converter.convertSentence("bl"), u8"ပဲ", "Explicit bl ranks ပဲ first");
        tests.expectEqual(converter.convertSentence("bal"), u8"ပဲ", "Explicit bal -> ပဲ");
        tests.expectEqual(converter.convertSentence("be"), u8"ပဲ", "Explicit be -> ပဲ");

        const auto soCandidates = converter.getCandidates("so");
        tests.expectTrue(soCandidates.size() >= 2, "so keeps lexical candidates and may add rule alternatives");
        tests.expectCandidate(soCandidates, 0, u8"ဆို", 100, "so first candidate ဆို");
        tests.expectCandidate(soCandidates, 1, u8"စို", 90, "so second candidate စို");

        const auto thyCandidates = converter.getCandidates("thy");
        tests.expectContains(thyCandidates, u8"သေး", "thy contains သေး");
        tests.expectContains(thyCandidates, u8"သေ", "thy contains သေ");
        tests.expectCandidate(thyCandidates, 0, u8"သေး", 100, "thy keeps သေး first");

        const auto thayCandidates = converter.getCandidates("thay");
        tests.expectContains(thayCandidates, u8"သေး", "thay contains သေး");
        tests.expectContains(thayCandidates, u8"သေ", "thay contains သေ");

        tests.expectEqual(converter.convertSentence("alote"), u8"အလုပ်", "No-space a prefix alote -> အလုပ်");
        tests.expectEqual(converter.convertSentence("asaw"), u8"အစော", "No-space a prefix asaw -> အစော");
        tests.expectEqual(converter.convertSentence("amyan"), u8"အမြန်", "No-space a prefix amyan -> အမြန်");
        tests.expectEqual(converter.convertSentence("malote"), u8"မလုပ်", "No-space ma prefix malote -> မလုပ်");

        // alpha-0.8.2 user-added exact candidates.
        tests.expectEqual(converter.convertSentence("lwl"), u8"လွဲ", "lwl -> လွဲ");
        tests.expectEqual(converter.convertSentence("lwal"), u8"လွဲ", "lwal -> လွဲ");
        tests.expectEqual(converter.convertSentence("lwel"), u8"လွဲ", "lwel -> လွဲ");
        tests.expectEqual(converter.convertSentence("kel"), u8"ကဲ", "kel -> ကဲ");
        tests.expectEqual(converter.convertSentence("kal"), u8"ကဲ", "kal -> ကဲ");
        tests.expectEqual(converter.convertSentence("kl"), u8"ကဲ", "kl -> ကဲ");

        const auto yinCandidates082 = converter.getCandidates("yin", 9);
        tests.expectTrue(yinCandidates082.size() >= 3, "yin has three reviewed candidates");
        if (yinCandidates082.size() >= 3) {
            tests.expectEqual(yinCandidates082[0].burmese, u8"ရင်", "yin first -> ရင်");
            tests.expectEqual(yinCandidates082[1].burmese, u8"ရင်း", "yin second -> ရင်း");
            tests.expectEqual(yinCandidates082[2].burmese, u8"ယင်", "yin third -> ယင်");
        }

        for (const auto& input : {std::string("sai"), std::string("sine")}) {
            const auto c = converter.getCandidates(input, 9);
            tests.expectTrue(c.size() >= 4, input + " has four reviewed candidates");
            if (c.size() >= 4) {
                tests.expectEqual(c[0].burmese, u8"ဆိုင်", input + " first -> ဆိုင်");
                tests.expectEqual(c[1].burmese, u8"စိုင်", input + " second -> စိုင်");
                tests.expectEqual(c[2].burmese, u8"စိုင်း", input + " third -> စိုင်း");
                tests.expectEqual(c[3].burmese, u8"ဆိုင်း", input + " fourth -> ဆိုင်း");
            }
        }

        const auto unknownCandidates = converter.getCandidates("unknownword");
        tests.expectEqualSize(unknownCandidates.size(), 1, "Unknown candidate fallback size");
        if (unknownCandidates.size() == 1) {
            tests.expectEqual(unknownCandidates[0].burmese, "unknownword", "Unknown candidate fallback text");
        }

        std::cout << '\n' << "Summary: " << tests.passed << " passed, " << tests.failed << " failed." << '\n';
        return tests.failed == 0 ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "Test setup failed: " << ex.what() << '\n';
        return 1;
    }
}
