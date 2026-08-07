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
};

myanglish::MyanglishConverter loadConverterOrThrow() {
    myanglish::Dictionary dictionary;
    std::string errorMessage;
    if (!dictionary.loadFromCsv(testDictionaryPath(), &errorMessage)) {
        throw std::runtime_error(errorMessage);
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
        tests.expectEqualSize(candidates.size(), 3, "Multiple candidates");
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

        const auto kanCandidates = converter.getCandidates("kan");
        tests.expectEqualSize(kanCandidates.size(), 5, "Candidate support kan");
        tests.expectContains(kanCandidates, u8"ကန်", "Candidate support kan contains ကန်");
        tests.expectContains(kanCandidates, u8"ကန်း", "Candidate support kan contains ကန်း");
        tests.expectContains(kanCandidates, u8"ကမ်", "Candidate support kan contains ကမ်");
        tests.expectContains(kanCandidates, u8"ကမ်း", "Candidate support kan contains ကမ်း");
        tests.expectContains(kanCandidates, u8"ကံ", "Candidate support kan contains ကံ");

        tests.expectEqual(converter.convertSentence("ker"), u8"ကား", "Master main er -> ား");
        tests.expectEqual(converter.convertSentence("kee"), u8"ကီး", "Master main ee -> ီး");
        tests.expectEqual(converter.convertSentence("kinn"), u8"ကင်း", "Master main inn -> င်း");
        tests.expectEqual(converter.convertSentence("kis"), u8"ကစ်", "Master variant is -> စ်");
        tests.expectEqual(converter.convertSentence("kwat"), u8"ကွက်", "Master direct wat -> ွက်");

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
