#include "MyanglishConverter.h"

#include "UnicodeUtils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace myanglish {

namespace {

struct BaseConsonantRule {
    const char* latin;
    const char* burmese;
};

struct MedialRule {
    const char* latin;
    const char* burmese;
};

struct CsvRow {
    std::string first;
    std::string second;
    std::string third;
};

std::filesystem::path sourceDataRoot() {
    return std::filesystem::path(MYANGLISHIME_SOURCE_DIR);
}

bool parseCsvLine(const std::string& line, CsvRow& row) {
    std::stringstream stream(line);

    if (!std::getline(stream, row.first, ',')) {
        return false;
    }
    if (!std::getline(stream, row.second, ',')) {
        return false;
    }
    if (!std::getline(stream, row.third)) {
        return false;
    }

    row.first = trim(row.first);
    row.second = trim(row.second);
    row.third = trim(row.third);
    return true;
}

std::string stripUtf8Bom(std::string text) {
    if (text.size() >= 3
        && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }

    return text;
}

bool isHeaderRow(const CsvRow& row) {
    return toLowerAscii(trim(row.first)) == "code"
        && toLowerAscii(trim(row.second)) == "standalone_output"
        && toLowerAscii(trim(row.third)) == "dependent_output";
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

const std::vector<BaseConsonantRule>& baseConsonantRules() {
    static const std::vector<BaseConsonantRule> rules = {
        {"ng", "င"},
        {"ny", "ည"},
        {"kh", "ခ"},
        {"g", "ဂ"},
        {"ph", "ဖ"},
        {"b", "ဘ"},
        {"t", "သ"},
        {"h", "ထ"},
        {"ch", "ချ"},
        {"sh", "ရှ"},
        {"k", "က"},
        {"g", "ဂ"},
        {"s", "စ"},
        {"j", "ဇ"},
        {"t", "တ"},
        {"d", "ဒ"},
        {"n", "န"},
        {"p", "ပ"},
        {"b", "ဘ"},
        {"m", "မ"},
        {"y", "ယ"},
        {"y", "ရ"},
        {"l", "လ"},
        {"w", "ဝ"},
        {"s", "ဆ"},
        {"z", "ဇ"},
        {"h", "ဟ"},
    };

    return rules;
}

const std::vector<MedialRule>& medialRules() {
    static const std::vector<MedialRule> rules = {
        {"y", "ျ"},
        {"r", "ြ"},
        {"w", "ွ"},
        {"h", "ှ"},
    };

    return rules;
}

const BaseConsonantRule* findBaseConsonant(const std::string& text, std::size_t& matchedLength) {
    matchedLength = 0;
    const BaseConsonantRule* match = nullptr;

    for (const auto& rule : baseConsonantRules()) {
        const std::size_t length = std::char_traits<char>::length(rule.latin);
        if (length <= matchedLength) {
            continue;
        }
        if (startsWith(text, rule.latin)) {
            matchedLength = length;
            match = &rule;
        }
    }

    return match;
}

bool consumeMedials(const std::string& text, std::size_t& index, std::string& output) {
    bool consumedAny = false;

    while (index < text.size()) {
        bool matched = false;
        for (const auto& rule : medialRules()) {
            const std::size_t length = std::char_traits<char>::length(rule.latin);
            if (startsWith(text.substr(index), rule.latin)) {
                output += rule.burmese;
                index += length;
                matched = true;
                consumedAny = true;
                break;
            }
        }

        if (!matched) {
            break;
        }
    }

    return consumedAny;
}

} // namespace

MyanglishConverter::MyanglishConverter(Dictionary dictionary, std::filesystem::path dataRoot)
        : dictionary_(std::move(dictionary)),
      dataRoot_(dataRoot.empty() ? sourceDataRoot() : std::move(dataRoot)) {
    auto loadRulesFromCsv = [&](const std::filesystem::path& path, auto& rulesByCode, auto& codesByLength, std::size_t& priority) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return;
        }

        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(file, line)) {
            ++lineNumber;
            std::string trimmedLine = trim(line);
            if (lineNumber == 1) {
                trimmedLine = stripUtf8Bom(std::move(trimmedLine));
            }
            if (trimmedLine.empty()) {
                continue;
            }

            CsvRow row;
            if (!parseCsvLine(trimmedLine, row)) {
                continue;
            }

            if (lineNumber == 1 && isHeaderRow(row)) {
                continue;
            }

            if (row.first.empty() || row.second.empty()) {
                continue;
            }

            const std::string code = toLowerAscii(row.first);
            RhymeRule rule{row.second, row.third, static_cast<int>(100000 - priority)};

            auto& rules = rulesByCode[code];
            rules.push_back(rule);

            if (std::find(codesByLength.begin(), codesByLength.end(), code) == codesByLength.end()) {
                codesByLength.push_back(code);
            }

            ++priority;
        }
    };

    std::size_t priority = 0;
    loadRulesFromCsv(dataRoot_ / "data" / "rules" / "rhymes.csv", rhymeRulesByCode_, rhymeCodesByLength_, priority);
    loadRulesFromCsv(dataRoot_ / "data" / "rules" / "tone_marks.csv", toneMarkRulesByCode_, toneMarkCodesByLength_, priority);

    std::sort(rhymeCodesByLength_.begin(), rhymeCodesByLength_.end(), [](const std::string& left, const std::string& right) {
        if (left.size() != right.size()) {
            return left.size() > right.size();
        }
        return left < right;
    });

    std::sort(toneMarkCodesByLength_.begin(), toneMarkCodesByLength_.end(), [](const std::string& left, const std::string& right) {
        if (left.size() != right.size()) {
            return left.size() > right.size();
        }
        return left < right;
    });
}

bool MyanglishConverter::isReady() const noexcept {
    return !dictionary_.entries().empty();
}

Candidate MyanglishConverter::makeBestCandidate(const std::vector<DictionaryEntry>& entries) {
    Candidate bestCandidate;
    bool hasCandidate = false;

    for (const auto& entry : entries) {
        if (!hasCandidate || entry.frequency > bestCandidate.frequency) {
            bestCandidate.burmese = entry.burmese;
            bestCandidate.frequency = entry.frequency;
            hasCandidate = true;
        }
    }

    return bestCandidate;
}

std::vector<Candidate> MyanglishConverter::buildRuleCandidates(const std::string& text) const {
    const std::string normalized = toLowerAscii(trim(text));
    if (normalized.empty()) {
        return {};
    }

    std::vector<Candidate> candidates;

    auto dependentOutputFor = [](const std::string& baseLatin, const std::string& rhymeCode, const RhymeRule& rule) {
        if (rhymeCode == "ar" && (baseLatin == "kh" || baseLatin == "ng")) {
            return std::string("ါ");
        }

        return rule.dependentOutput;
    };

    struct RuleMatch {
        std::string code;
        const std::vector<RhymeRule>* rules = nullptr;
    };

    auto findLongestMatch = [&](const std::string& text, std::size_t index, const auto& rulesByCode, const auto& codesByLength) {
        RuleMatch match;

        for (const auto& code : codesByLength) {
            if (!startsWith(text.substr(index), code)) {
                continue;
            }

            const auto it = rulesByCode.find(code);
            if (it == rulesByCode.end()) {
                continue;
            }

            match.code = code;
            match.rules = &it->second;
            break;
        }

        return match;
    };

    auto appendAndFinalize = [&](std::vector<Candidate>& outputCandidates) {
        std::sort(outputCandidates.begin(), outputCandidates.end(), [](const Candidate& left, const Candidate& right) {
            if (left.frequency != right.frequency) {
                return left.frequency > right.frequency;
            }
            return left.burmese < right.burmese;
        });

        outputCandidates.erase(std::unique(outputCandidates.begin(), outputCandidates.end(), [](const Candidate& left, const Candidate& right) {
            return left.burmese == right.burmese;
        }), outputCandidates.end());
    };

    auto tryParse = [&](std::size_t consumedLength, const std::string& prefixOutput, const std::string& baseLatin, bool standalone) {
        std::size_t index = consumedLength;
        std::string syllable = prefixOutput;

        if (!standalone && consumedLength > 0) {
            consumeMedials(normalized, index, syllable);
        }

        const auto rhymeMatch = findLongestMatch(normalized, index, rhymeRulesByCode_, rhymeCodesByLength_);
        if (rhymeMatch.rules != nullptr) {
            const std::size_t rhymeEnd = index + rhymeMatch.code.size();
            const auto toneMarkMatch = findLongestMatch(normalized, rhymeEnd, toneMarkRulesByCode_, toneMarkCodesByLength_);

            if (rhymeEnd == normalized.size() || (toneMarkMatch.rules != nullptr && rhymeEnd + toneMarkMatch.code.size() == normalized.size())) {
                for (const auto& rule : *rhymeMatch.rules) {
                    const std::string rhymeOutput = standalone ? rule.standaloneOutput : dependentOutputFor(baseLatin, rhymeMatch.code, rule);
                    if (rhymeEnd == normalized.size()) {
                        candidates.push_back(Candidate{syllable + rhymeOutput, rule.frequency});
                        continue;
                    }

                    const RhymeRule& toneMarkRule = toneMarkMatch.rules->front();
                    const std::string toneMarkOutput = toneMarkRule.dependentOutput.empty() ? toneMarkRule.standaloneOutput : toneMarkRule.dependentOutput;
                    candidates.push_back(Candidate{syllable + rhymeOutput + toneMarkOutput, std::min(rule.frequency, toneMarkRule.frequency)});
                }
                return true;
            }
        }

        if (index < normalized.size() && normalized.substr(index) == "a") {
            candidates.push_back(Candidate{syllable, 0});
            return true;
        }

        return false;
    };

    if (tryParse(0, "", "", true)) {
        appendAndFinalize(candidates);
        return candidates;
    }

    std::size_t baseLength = 0;
    const BaseConsonantRule* baseRule = findBaseConsonant(normalized, baseLength);
    if (baseRule != nullptr && baseLength > 0 && tryParse(baseLength, baseRule->burmese, baseRule->latin, false)) {
        appendAndFinalize(candidates);
        return candidates;
    }

    return {};
}

std::vector<Candidate> MyanglishConverter::getCandidates(const std::string& myanglish, std::size_t limit) const {
    if (limit == 0) {
        return {};
    }

    const std::string originalInput = myanglish;
    const std::string trimmedInput = trim(myanglish);
    if (trimmedInput.empty()) {
        return {};
    }

    const auto entries = dictionary_.findEntries(trimmedInput);
    if (entries.empty()) {
        auto ruleCandidates = buildRuleCandidates(trimmedInput);
        if (ruleCandidates.empty()) {
            return {Candidate{originalInput, 0}};
        }

        if (ruleCandidates.size() > limit) {
            ruleCandidates.resize(limit);
        }

        return ruleCandidates;
    }

    std::unordered_map<std::string, int> bestFrequencies;
    for (const auto& entry : entries) {
        auto it = bestFrequencies.find(entry.burmese);
        if (it == bestFrequencies.end() || entry.frequency > it->second) {
            bestFrequencies[entry.burmese] = entry.frequency;
        }
    }

    std::vector<Candidate> candidates;
    candidates.reserve(bestFrequencies.size());
    for (const auto& pair : bestFrequencies) {
        candidates.push_back(Candidate{pair.first, pair.second});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        if (left.frequency != right.frequency) {
            return left.frequency > right.frequency;
        }
        return left.burmese < right.burmese;
    });

    if (candidates.size() > limit) {
        candidates.resize(limit);
    }

    return candidates;
}

std::string MyanglishConverter::convertSentence(const std::string& input) const {
    const auto words = splitWords(input);
    if (words.empty()) {
        return {};
    }

    std::vector<std::string> convertedWords;
    convertedWords.reserve(words.size());

    const std::size_t maxPhraseLength = dictionary_.longestPhraseLength();

    for (std::size_t index = 0; index < words.size();) {
        bool matched = false;
        const std::size_t remainingWords = words.size() - index;
        const std::size_t bestLength = std::min(maxPhraseLength, remainingWords);

        for (std::size_t length = bestLength; length > 0; --length) {
            const std::string lookup = joinWords(words, index, length);
            const auto entries = dictionary_.findEntries(lookup);
            if (!entries.empty()) {
                convertedWords.push_back(makeBestCandidate(entries).burmese);
                index += length;
                matched = true;
                break;
            }
        }

        if (!matched) {
            auto ruleCandidates = buildRuleCandidates(words[index]);
            if (!ruleCandidates.empty()) {
                convertedWords.push_back(ruleCandidates.front().burmese);
            } else {
                convertedWords.push_back(words[index]);
            }
            ++index;
        }
    }

    std::string output;
    for (const auto& word : convertedWords) {
        if (!output.empty()) {
            output.push_back(' ');
        }
        output += word;
    }

    return output;
}

} // namespace myanglish
