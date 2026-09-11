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

std::string stripOptionalQuotes(std::string text) {
    text = trim(text);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
    }
    return text;
}

std::vector<std::string> splitPipeSeparated(const std::string& text) {
    std::vector<std::string> parts;
    std::stringstream stream(stripOptionalQuotes(text));
    std::string part;
    while (std::getline(stream, part, '|')) {
        part = toLowerAscii(trim(part));
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
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
    // One deterministic base mapping. Ambiguous real-world spellings belong
    // in dictionary/candidate data rather than duplicate equal-length rules.
    static const std::vector<BaseConsonantRule> rules = {
        // Lexicon Pack 2: productive hn-/nh- base for Burmese "နှ-".
        {"hn", "နှ"},
        {"nh", "နှ"},
        {"ng", "င"},
        {"ny", "ည"},
        {"kh", "ခ"},
        {"ph", "ဖ"},
        {"th", "သ"},
        {"ht", "ထ"},
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
        {"l", "လ"},
        {"w", "ဝ"},
        {"z", "ဇ"},
        {"h", "ဟ"},
    };

    return rules;
}

bool usesTallAa(const std::string& baseLatin) {
    // Myanmar U+102B TALL AA is used with these base shapes. This keeps
    // dar -> ဒါ while kar -> ကာ, and also handles ခါ/ဂါ/ငါ/ပါ/ဝါ.
    return baseLatin == "kh"
        || baseLatin == "g"
        || baseLatin == "ng"
        || baseLatin == "d"
        || baseLatin == "p"
        || baseLatin == "w";
}

const std::vector<MedialRule>& medialRules() {
    // alpha-0.9.4 user rule: -ya prefers medial ra (ြ) as candidate #1,
    // while medial ya (ျ) remains candidate #2. The single-path parser and
    // expanded master-rule path use the same preference.
    static const std::vector<MedialRule> rules = {
        {"y", "ြ"},
        {"y", "ျ"},
        {"r", "ြ"},
        {"w", "ွ"},
        {"h", "ှ"},
    };

    return rules;
}

struct MedialExpansion {
    std::size_t index = 0;
    std::string output;
    int bonus = 0;
};

std::vector<MedialExpansion> expandMedials(
    const std::string& text,
    std::size_t startIndex,
    const std::string& baseOutput
) {
    std::vector<MedialExpansion> paths{{startIndex, baseOutput, 0}};

    // Myanmar medial codes used by this IME are single ASCII letters. Expand
    // ambiguous y into two candidate paths while keeping deterministic rules
    // for r/w/h. A small hard limit avoids malformed input growing forever.
    for (int depth = 0; depth < 4; ++depth) {
        bool advancedAny = false;
        std::vector<MedialExpansion> next;

        for (const auto& path : paths) {
            if (path.index >= text.size()) {
                next.push_back(path);
                continue;
            }

            const char code = text[path.index];
            if (code == 'y') {
                next.push_back(MedialExpansion{path.index + 1, path.output + "ြ", path.bonus + 20000});
                next.push_back(MedialExpansion{path.index + 1, path.output + "ျ", path.bonus + 10000});
                advancedAny = true;
            } else if (code == 'r') {
                next.push_back(MedialExpansion{path.index + 1, path.output + "ြ", path.bonus + 15000});
                advancedAny = true;
            } else if (code == 'w') {
                next.push_back(MedialExpansion{path.index + 1, path.output + "ွ", path.bonus + 15000});
                advancedAny = true;
            } else if (code == 'h') {
                next.push_back(MedialExpansion{path.index + 1, path.output + "ှ", path.bonus + 15000});
                advancedAny = true;
            } else {
                next.push_back(path);
            }
        }

        // Deduplicate identical paths while keeping the better preference bonus.
        std::vector<MedialExpansion> deduped;
        for (const auto& candidate : next) {
            auto found = std::find_if(deduped.begin(), deduped.end(), [&](const MedialExpansion& existing) {
                return existing.index == candidate.index && existing.output == candidate.output;
            });
            if (found == deduped.end()) {
                deduped.push_back(candidate);
            } else if (candidate.bonus > found->bonus) {
                found->bonus = candidate.bonus;
            }
        }
        paths = std::move(deduped);

        if (!advancedAny) {
            break;
        }
    }

    return paths;
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

bool isSingleTypoAway(const std::string& input, const std::string& target) {
    if (input == target) {
        return false;
    }

    const std::size_t inputLength = input.size();
    const std::size_t targetLength = target.size();

    if (inputLength + 1 < targetLength || targetLength + 1 < inputLength) {
        return false;
    }

    // One substituted character.
    if (inputLength == targetLength) {
        std::size_t differences = 0;
        for (std::size_t i = 0; i < inputLength; ++i) {
            if (input[i] != target[i]) {
                ++differences;
            }
        }

        if (differences == 1) {
            return true;
        }

        // One adjacent transposition.
        for (std::size_t i = 0; i + 1 < inputLength; ++i) {
            if (input[i] == target[i + 1]
                && input[i + 1] == target[i]) {
                std::string swapped = input;
                std::swap(swapped[i], swapped[i + 1]);
                if (swapped == target) {
                    return true;
                }
            }
        }

        return false;
    }

    // One missing or extra character.
    const std::string& shorter =
        inputLength < targetLength ? input : target;
    const std::string& longer =
        inputLength < targetLength ? target : input;

    std::size_t shortIndex = 0;
    std::size_t longIndex = 0;
    bool skipped = false;

    while (shortIndex < shorter.size() && longIndex < longer.size()) {
        if (shorter[shortIndex] == longer[longIndex]) {
            ++shortIndex;
            ++longIndex;
            continue;
        }

        if (skipped) {
            return false;
        }

        skipped = true;
        ++longIndex;
    }

    return true;
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

    auto addMasterRule = [&](const std::string& code, const std::string& burmese, int score) {
        if (code.empty() || burmese.empty()) {
            return;
        }

        auto& rules = masterRhymeRulesByCode_[toLowerAscii(code)];
        for (auto& existing : rules) {
            if (existing.burmese == burmese) {
                existing.score = std::max(existing.score, score);
                return;
            }
        }
        rules.push_back(MasterRhymeRule{burmese, score});
    };

    const auto masterPath = dataRoot_ / "data" / "myanglish_rules" / "rhymes_master_v1.csv";
    std::ifstream masterFile(masterPath, std::ios::binary);
    if (masterFile.is_open()) {
        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(masterFile, line)) {
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

            if (lineNumber == 1
                && toLowerAscii(trim(row.first)) == "burmese"
                && toLowerAscii(trim(row.second)) == "main") {
                continue;
            }

            const std::string burmese = stripOptionalQuotes(row.first);
            const std::string mainCode = toLowerAscii(stripOptionalQuotes(row.second));
            if (burmese.empty() || mainCode.empty()) {
                continue;
            }

            // User review for alpha-0.7: this family is intentionally unused.
            if (burmese == "ိုင့်") {
                continue;
            }

            addMasterRule(mainCode, burmese, 300000);
            for (const auto& variant : splitPipeSeparated(row.third)) {
                addMasterRule(variant, burmese, 200000);
            }
        }
    }

    // Alpha 0.8: user-reviewed ranked suffix candidates.  This table is
    // additive: it does not remove the stable alpha-0.3 master/legacy rules.
    const auto rankedSuffixPath = dataRoot_ / "data" / "myanglish_rules" / "ranked_suffixes_alpha08.csv";
    std::ifstream rankedSuffixFile(rankedSuffixPath, std::ios::binary);
    if (rankedSuffixFile.is_open()) {
        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(rankedSuffixFile, line)) {
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

            if (lineNumber == 1
                && toLowerAscii(trim(row.first)) == "code"
                && toLowerAscii(trim(row.second)) == "burmese") {
                continue;
            }

            const std::string code = toLowerAscii(stripOptionalQuotes(row.first));
            const std::string burmese = stripOptionalQuotes(row.second);
            int score = 0;
            try {
                score = std::stoi(stripOptionalQuotes(row.third));
            } catch (...) {
                continue;
            }

            addMasterRule(code, burmese, score);
        }
    }

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

    loadBurmeseLexicon();

    auto loadExtraCandidateFile = [&](
        const std::filesystem::path& path,
        auto& destination
    ) {
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
            if (lineNumber == 1 && toLowerAscii(row.first) == "myanglish") {
                continue;
            }

            const std::string input = toLowerAscii(stripOptionalQuotes(row.first));
            const std::string burmese = stripOptionalQuotes(row.second);
            int frequency = 0;
            try {
                frequency = std::stoi(stripOptionalQuotes(row.third));
            } catch (...) {
                frequency = 0;
            }
            if (!input.empty() && !burmese.empty()) {
                destination[input].push_back(Candidate{burmese, frequency});
            }
        }
    };

    // Historical hand-entered mappings and loanwords are candidate sources,
    // NOT overrides of alpha-0.3 stable conversions. This prevents old rows
    // such as be->ဘီ or in->အိမ် from replacing stable phonetic results while
    // still keeping them available after the primary candidate.
    loadExtraCandidateFile(
        dataRoot_ / "data" / "historical_candidates.csv",
        historicalCandidatesByInput_
    );
    loadExtraCandidateFile(
        dataRoot_ / "data" / "loanwords.csv",
        loanwordCandidatesByInput_
    );

    // Companion metadata from the user's reviewed 36-family table. It is used
    // only for deciding whether an extra letter is still extending the SAME
    // word (in -> inn, o -> oe, etc.), not for candidate ranking.
    {
        std::ifstream file(dataRoot_ / "data" / "myanglish_rules" / "suffix_families_alpha08.csv", std::ios::binary);
        std::string line;
        std::size_t lineNumber = 0;
        while (file.is_open() && std::getline(file, line)) {
            ++lineNumber;
            if (lineNumber == 1) {
                line = stripUtf8Bom(std::move(line));
            }
            line = trim(line);
            if (line.empty()) {
                continue;
            }
            const auto comma = line.find(',');
            if (comma == std::string::npos) {
                continue;
            }
            const std::string code = toLowerAscii(trim(line.substr(0, comma)));
            if (lineNumber == 1 && code == "code") {
                continue;
            }
            try {
                const int familyId = std::stoi(trim(line.substr(comma + 1)));
                if (!code.empty()) {
                    suffixFamiliesByCode_[code].insert(familyId);
                }
            } catch (...) {
                // Ignore malformed metadata rows; input conversion must remain usable.
            }
        }
    }
}

void MyanglishConverter::loadBurmeseLexicon() {
    burmeseLexicon_.clear();

    const auto path = dataRoot_ / "data" / "lexicon" / "burmese_lexicon.txt";
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        std::string word = trim(line);
        if (lineNumber == 1) {
            word = stripUtf8Bom(std::move(word));
        }
        if (!word.empty()) {
            burmeseLexicon_.insert(std::move(word));
        }
    }
}

bool MyanglishConverter::isKnownBurmeseWord(const std::string& text) const noexcept {
    return !burmeseLexicon_.empty() && burmeseLexicon_.find(text) != burmeseLexicon_.end();
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
        if (rhymeCode == "ar" && usesTallAa(baseLatin)) {
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
        // Lexicon Pack 2: starting y- should expose ရ- before ယ-.
        // The normal parser still produces ယ-. Add a parallel ရ- candidate
        // without disturbing explicit dictionary entries.
        if (startsWith(normalized, "y")) {
            std::vector<Candidate> raAlternatives;
            for (const auto& candidate : outputCandidates) {
                static const std::string ya = "ယ";
                static const std::string ra = "ရ";
                if (startsWith(candidate.burmese, ya)) {
                    Candidate alternate = candidate;
                    alternate.burmese =
                        ra + candidate.burmese.substr(ya.size());
                    alternate.frequency += 25000;
                    raAlternatives.push_back(std::move(alternate));
                }
            }
            outputCandidates.insert(
                outputCandidates.end(),
                raAlternatives.begin(),
                raAlternatives.end()
            );
        }

        // Keep only the highest-frequency entry for each Burmese output.
        // std::unique after a frequency-first sort is not sufficient because
        // duplicate Burmese strings with different frequencies may not be
        // adjacent.
        std::unordered_map<std::string, int> bestFrequencyByOutput;
        for (const auto& candidate : outputCandidates) {
            auto it = bestFrequencyByOutput.find(candidate.burmese);
            if (it == bestFrequencyByOutput.end() || candidate.frequency > it->second) {
                bestFrequencyByOutput[candidate.burmese] = candidate.frequency;
            }
        }

        outputCandidates.clear();
        outputCandidates.reserve(bestFrequencyByOutput.size());
        for (const auto& pair : bestFrequencyByOutput) {
            outputCandidates.push_back(Candidate{pair.first, pair.second});
        }

        std::sort(outputCandidates.begin(), outputCandidates.end(), [](const Candidate& left, const Candidate& right) {
            if (left.frequency != right.frequency) {
                return left.frequency > right.frequency;
            }
            return left.burmese < right.burmese;
        });
    };

    auto appendMasterRules = [&](const std::string& code, const std::string& prefixOutput, const std::string& baseLatin, int parseBonus) {
        const auto it = masterRhymeRulesByCode_.find(toLowerAscii(code));
        if (it == masterRhymeRulesByCode_.end()) {
            return;
        }
        for (const auto& rule : it->second) {
            std::string burmeseRhyme = rule.burmese;
            if (usesTallAa(baseLatin)) {
                // These base shapes require TALL AA (ါ), including inside
                // ော-family rhymes: p + ောင် must be ပေါင်, never ပောင်.
                const std::string normalAa = "ာ";
                const std::string tallAa = "ါ";
                std::size_t pos = 0;
                while ((pos = burmeseRhyme.find(normalAa, pos)) != std::string::npos) {
                    burmeseRhyme.replace(pos, normalAa.size(), tallAa);
                    pos += tallAa.size();
                }
            }

            const std::string candidateText = prefixOutput + burmeseRhyme;
            int score = rule.score;

            // A lexicon hit is strong evidence that the generated spelling is
            // a real Burmese word.  Absence never deletes a candidate because
            // compounds, derived forms and names can be missing from a finite
            // word list.
            if (isKnownBurmeseWord(candidateText)) {
                score += 100000;
            }

            candidates.push_back(Candidate{candidateText, score + parseBonus});
        }
    };

    // Try the new master table before the legacy rules.
    std::size_t masterBaseLength = 0;
    const BaseConsonantRule* masterBaseRule = findBaseConsonant(normalized, masterBaseLength);
    if (masterBaseRule != nullptr && masterBaseLength > 0) {
        // Direct suffix: kwat -> က + ွက်
        appendMasterRules(normalized.substr(masterBaseLength), masterBaseRule->burmese, masterBaseRule->latin, 15000);

        // Medial split. alpha-0.8 expands y as ြ first and ျ second.
        // Example: p + y + i can generate both ပြီ-like and ပျီ-like paths;
        // explicit word mappings still outrank generated alternatives.
        for (const auto& medial : expandMedials(normalized, masterBaseLength, masterBaseRule->burmese)) {
            if (medial.index <= masterBaseLength || medial.index >= normalized.size()) {
                continue;
            }
            appendMasterRules(
                normalized.substr(medial.index),
                medial.output,
                masterBaseRule->latin,
                medial.bonus
            );
        }
    }

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

    if (!candidates.empty()) {
        appendAndFinalize(candidates);
        return candidates;
    }

    return {};
}

std::vector<Candidate> MyanglishConverter::buildCoreCandidates(const std::string& text) const {
    const std::string normalized = toLowerAscii(trim(text));
    if (normalized.empty()) {
        return {};
    }

    const auto entries = dictionary_.findEntries(normalized);
    std::vector<Candidate> candidates;

    // Keep exact dictionary/user mappings first, but no longer hide useful
    // generated alternatives.  This is the alpha-0.7 candidate expansion: an
    // exact word such as sin -> ဆင် stays first while rule candidates can still
    // appear after it in the candidate window.
    for (const auto& entry : entries) {
        candidates.push_back(Candidate{entry.burmese, entry.frequency});
    }

    auto ruleCandidates = buildRuleCandidates(normalized);

    // alpha-0.8 strict generated-word validation: a rule may describe a valid
    // Myanmar spelling shape without describing a word people actually use.
    // When the shipped Burmese lexicon is available, generated alternatives
    // must exist in it. Exact dictionary/user/loanword rows above are trusted
    // and are never removed by this filter.
    if (!burmeseLexicon_.empty()) {
        ruleCandidates.erase(
            std::remove_if(
                ruleCandidates.begin(),
                ruleCandidates.end(),
                [&](const Candidate& candidate) {
                    return !isKnownBurmeseWord(candidate.burmese);
                }
            ),
            ruleCandidates.end()
        );
    }

    if (!entries.empty()) {
        // Preserve the relative order of generated alternatives while placing
        // all exact dictionary entries above them. Candidate::frequency is an
        // internal ranking score in this path; negative values are harmless.
        for (auto& candidate : ruleCandidates) {
            candidate.frequency -= 5000000;
        }
    }
    candidates.insert(candidates.end(), ruleCandidates.begin(), ruleCandidates.end());

    if (candidates.empty()) {
        return {};
    }

    std::unordered_map<std::string, int> bestFrequencies;
    for (const auto& candidate : candidates) {
        auto it = bestFrequencies.find(candidate.burmese);
        if (it == bestFrequencies.end() || candidate.frequency > it->second) {
            bestFrequencies[candidate.burmese] = candidate.frequency;
        }
    }

    candidates.clear();
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

    return candidates;
}

std::vector<Candidate> MyanglishConverter::buildPrefixCandidates(const std::string& text) const {
    const std::string normalized = toLowerAscii(trim(text));
    if (normalized.empty()) {
        return {};
    }

    struct PrefixRule {
        const char* latin;
        const char* burmese;
        int baseScore;
    };

    // Productive no-space prefixes requested for alpha-0.3.
    // Example: alote -> အလုပ်, asaw -> အစော, malote -> မလုပ်.
    static const PrefixRule prefixRules[] = {
        {"ma", "မ", 180000},
        {"a", "အ", 170000},
    };

    std::vector<Candidate> candidates;

    for (const auto& prefix : prefixRules) {
        const std::string latinPrefix(prefix.latin);
        if (!startsWith(normalized, latinPrefix) || normalized.size() <= latinPrefix.size()) {
            continue;
        }

        const std::string remainder = normalized.substr(latinPrefix.size());
        auto remainderCandidates = buildCoreCandidates(remainder);
        for (const auto& remainderCandidate : remainderCandidates) {
            if (remainderCandidate.burmese.empty()) {
                continue;
            }

            const std::string combined = std::string(prefix.burmese) + remainderCandidate.burmese;
            int score = prefix.baseScore + (std::max)(0, remainderCandidate.frequency);

            // A word present in the supplied Burmese lexicon receives a modest
            // ranking bonus, but lexicon incompleteness never blocks a valid
            // productive prefix such as မ + လုပ်.
            if (isKnownBurmeseWord(combined)) {
                score += 50000;
            }

            candidates.push_back(Candidate{combined, score});
        }
    }

    std::unordered_map<std::string, int> bestFrequencyByOutput;
    for (const auto& candidate : candidates) {
        auto it = bestFrequencyByOutput.find(candidate.burmese);
        if (it == bestFrequencyByOutput.end() || candidate.frequency > it->second) {
            bestFrequencyByOutput[candidate.burmese] = candidate.frequency;
        }
    }

    candidates.clear();
    candidates.reserve(bestFrequencyByOutput.size());
    for (const auto& pair : bestFrequencyByOutput) {
        candidates.push_back(Candidate{pair.first, pair.second});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        if (left.frequency != right.frequency) {
            return left.frequency > right.frequency;
        }
        return left.burmese < right.burmese;
    });

    return candidates;
}

std::vector<Candidate> MyanglishConverter::getCandidates(const std::string& myanglish, std::size_t limit) const {
    if (limit == 0) {
        return {};
    }

    const std::string originalInput = myanglish;
    const std::string trimmedInput = trim(myanglish);
    const std::string normalizedInput = toLowerAscii(trimmedInput);
    if (trimmedInput.empty()) {
        return {};
    }

    // Lexicon Pack 2: uppercase T is a deliberate shortcut.
    // TextService preserves uppercase T only when Shift+T starts a fresh word.
    if (trimmedInput == "T") {
        return {
            Candidate{"သည်", 1000000},
            Candidate{"တယ်", 999000}
        };
    }
    if (trimmedInput == "Ta") {
        return {Candidate{"တစ်", 1000000}};
    }
    if (trimmedInput == "S") {
        return {
            Candidate{"ဆောက်", 1400000},
            Candidate{"စောက်", 1390000},
            Candidate{"ဆောင့်", 1380000},
            Candidate{"စောင့်", 1370000}
        };
    }

    const bool hasExactCoreMapping = !dictionary_.findEntries(normalizedInput).empty();

    // Alpha 0.10.1: user-confirmed exact-only key.
    // var must be ဗာ and must not expose generated rule candidates such as ဒုံ.
    if (normalizedInput == "var") {
        const auto entries = dictionary_.findEntries(normalizedInput);
        std::vector<Candidate> exactOnly;
        for (const auto& entry : entries) {
            if (entry.burmese.empty()) continue;
            const bool duplicate = std::any_of(
                exactOnly.begin(), exactOnly.end(),
                [&](const Candidate& existing) {
                    return existing.burmese == entry.burmese;
                }
            );
            if (!duplicate) {
                exactOnly.push_back(Candidate{entry.burmese, entry.frequency});
            }
        }
        if (exactOnly.empty()) {
            exactOnly.push_back(Candidate{"ဗာ", 200});
        }
        std::sort(exactOnly.begin(), exactOnly.end(),
            [](const Candidate& left, const Candidate& right) {
                return left.frequency > right.frequency;
            });
        if (exactOnly.size() > limit) exactOnly.resize(limit);
        return exactOnly;
    }

    // Stable alpha-0.3 dictionary and validated rule candidates form the core.
    auto candidates = buildCoreCandidates(trimmedInput);

    // Productive prefixes are useful only when no explicit full-word mapping
    // already owns the input.
    if (!hasExactCoreMapping) {
        auto prefixCandidates = buildPrefixCandidates(trimmedInput);

        const bool coreHasKnownWord = std::any_of(
            candidates.begin(),
            candidates.end(),
            [&](const Candidate& candidate) { return isKnownBurmeseWord(candidate.burmese); }
        );
        const bool prefixHasKnownWord = std::any_of(
            prefixCandidates.begin(),
            prefixCandidates.end(),
            [&](const Candidate& candidate) { return isKnownBurmeseWord(candidate.burmese); }
        );

        if (candidates.empty() || (!coreHasKnownWord && prefixHasKnownWord)) {
            candidates.insert(candidates.end(), prefixCandidates.begin(), prefixCandidates.end());
        }
    }

    // Deduplicate and rank the core before adding optional historical/loanword
    // candidates. Those extra sources must never silently replace stable core
    // behavior.
    if (!candidates.empty()) {
        std::unordered_map<std::string, int> bestFrequencies;
        for (const auto& candidate : candidates) {
            auto it = bestFrequencies.find(candidate.burmese);
            if (it == bestFrequencies.end() || candidate.frequency > it->second) {
                bestFrequencies[candidate.burmese] = candidate.frequency;
            }
        }

        candidates.clear();
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
    }

    const auto historicalIt = historicalCandidatesByInput_.find(normalizedInput);
    const auto loanwordIt = loanwordCandidatesByInput_.find(normalizedInput);

    std::vector<Candidate> extras;
    auto appendExtraSource = [&](const auto& it, const auto& end) {
        if (it == end) {
            return;
        }
        for (const auto& candidate : it->second) {
            if (candidate.burmese.empty()) {
                continue;
            }
            const bool duplicate = std::any_of(extras.begin(), extras.end(), [&](const Candidate& existing) {
                return existing.burmese == candidate.burmese;
            });
            if (!duplicate) {
                extras.push_back(candidate);
            }
        }
    };
    appendExtraSource(historicalIt, historicalCandidatesByInput_.end());
    appendExtraSource(loanwordIt, loanwordCandidatesByInput_.end());

    auto appendUnique = [](std::vector<Candidate>& target, const std::vector<Candidate>& source) {
        for (const auto& candidate : source) {
            const bool duplicate = std::any_of(target.begin(), target.end(), [&](const Candidate& existing) {
                return existing.burmese == candidate.burmese;
            });
            if (!duplicate) {
                target.push_back(candidate);
            }
        }
    };

    std::vector<Candidate> ordered;

    // For inputs of three or more letters without an explicit stable mapping,
    // a hand-entered historical word or exact English loanword is usually more
    // intentional than a generic phonetic rule (bank -> ဘဏ်, computer -> ...).
    // Very short codes such as ai/in/o remain phonetic-first to protect alpha-0.3.
    const bool extrasFirst = !hasExactCoreMapping
        && normalizedInput.size() >= 3
        && !extras.empty();

    if (extrasFirst) {
        appendUnique(ordered, extras);
        appendUnique(ordered, candidates);
    } else {
        appendUnique(ordered, candidates);
        appendUnique(ordered, extras);
    }

    if (ordered.empty()) {
        return {Candidate{originalInput, 0}};
    }

    if (ordered.size() > limit) {
        ordered.resize(limit);
    }
    return ordered;
}


std::vector<Candidate> MyanglishConverter::getContinuousCandidates(
    const std::string& myanglish,
    std::size_t limit
) const {
    // Preserve all existing exact/rule/prefix/historical behavior first.
    auto candidates = getCandidates(myanglish, limit);

    const std::string normalized = toLowerAscii(trim(myanglish));
    if (normalized.size() < 3) {
        return candidates;
    }

    // getCandidates() returns the raw Roman input when nothing validated exists.
    // Typo correction is allowed only in that final-fallback case.
    const bool hasValidatedCandidate = std::any_of(
        candidates.begin(),
        candidates.end(),
        [&](const Candidate& candidate) {
            return !candidate.burmese.empty()
                && candidate.burmese != myanglish;
        }
    );

    if (hasValidatedCandidate) {
        return candidates;
    }

    std::unordered_map<std::string, int> bestFrequencyByOutput;

    for (const auto& entry : dictionary_.entries()) {
        if (!isSingleTypoAway(normalized, entry.myanglish)) {
            continue;
        }

        auto it = bestFrequencyByOutput.find(entry.burmese);
        if (it == bestFrequencyByOutput.end() || entry.frequency > it->second) {
            bestFrequencyByOutput[entry.burmese] = entry.frequency;
        }
    }

    if (bestFrequencyByOutput.empty()) {
        return candidates;
    }

    std::vector<Candidate> typoCandidates;
    typoCandidates.reserve(bestFrequencyByOutput.size());

    for (const auto& pair : bestFrequencyByOutput) {
        typoCandidates.push_back(Candidate{pair.first, pair.second});
    }

    std::sort(
        typoCandidates.begin(),
        typoCandidates.end(),
        [](const Candidate& left, const Candidate& right) {
            if (left.frequency != right.frequency) {
                return left.frequency > right.frequency;
            }
            return left.burmese < right.burmese;
        }
    );

    if (typoCandidates.size() > limit) {
        typoCandidates.resize(limit);
    }

    return typoCandidates;
}

bool MyanglishConverter::hasExactInput(const std::string& myanglish) const {
    const std::string normalized = toLowerAscii(trim(myanglish));
    if (normalized.empty()) {
        return false;
    }
    const auto candidates = getCandidates(normalized, 1);
    return !candidates.empty()
        && !candidates.front().burmese.empty()
        && candidates.front().burmese != normalized;
}

bool MyanglishConverter::hasInputPrefix(const std::string& prefix) const {
    const std::string normalized = toLowerAscii(trim(prefix));
    if (normalized.empty()) {
        return false;
    }

    auto startsWithInput = [&](const std::string& input) {
        return input.size() >= normalized.size()
            && input.compare(0, normalized.size(), normalized) == 0;
    };

    // Exact/full dictionary rows and their unfinished prefixes.
    for (const auto& entry : dictionary_.entries()) {
        if (startsWithInput(entry.myanglish)) {
            return true;
        }
    }

    // Historical and loanword candidate sources are not stored in Dictionary,
    // but they are real reviewed inputs and therefore must also keep a rolling
    // buffer alive while the user is still typing them.
    for (const auto& [input, ignored] : historicalCandidatesByInput_) {
        (void)ignored;
        if (startsWithInput(input)) {
            return true;
        }
    }
    for (const auto& [input, ignored] : loanwordCandidatesByInput_) {
        (void)ignored;
        if (startsWithInput(input)) {
            return true;
        }
    }

    // A complete rule-generated word is also a usable current input.
    if (hasExactInput(normalized)) {
        return true;
    }

    // One newly typed ASCII letter is always a plausible beginning of the next
    // word. This lets the rolling preview hold "ဘဏ်p" instead of incorrectly
    // committing/splitting the word as soon as p arrives.
    return normalized.size() == 1
        && normalized.front() >= 'a'
        && normalized.front() <= 'z';
}

std::size_t MyanglishConverter::findRollingSplit(
    const std::string& combinedInput
) const {
    const std::string normalized = toLowerAscii(trim(combinedInput));
    if (normalized.size() < 2) {
        return 0;
    }

    // Most important rule: if the WHOLE buffer is already a word or still a
    // prefix of a reviewed longer spelling, never split it. Examples:
    // ba -> ban -> bank, kin -> kinn, pau -> paung.
    if (hasInputPrefix(normalized)) {
        return 0;
    }

    std::size_t bestExactRightSplit = 0;
    std::size_t bestExactRightLength = 0;
    std::size_t bestPrefixOnlySplit = 0;

    for (std::size_t split = 1; split < normalized.size(); ++split) {
        const std::string left = normalized.substr(0, split);
        const std::string right = normalized.substr(split);

        if (!hasExactInput(left) || !hasInputPrefix(right)) {
            continue;
        }

        const bool rightIsExact = hasExactInput(right);
        if (rightIsExact) {
            // If multiple splits are possible, prefer the one that gives the
            // LONGEST complete right-hand word. This resolves yapy as ya|py
            // rather than yap|y, and yapyi as ya|pyi.
            if (right.size() > bestExactRightLength) {
                bestExactRightLength = right.size();
                bestExactRightSplit = split;
            }
            continue;
        }

        // With only a one-letter/incomplete look-ahead, prefer the longest
        // finished left word. Example bankp -> bank|p. The TSF layer previews
        // this split but delays the actual commit until the right side becomes
        // a complete word, avoiding premature word boundaries.
        if (split > bestPrefixOnlySplit) {
            bestPrefixOnlySplit = split;
        }
    }

    return bestExactRightSplit != 0
        ? bestExactRightSplit
        : bestPrefixOnlySplit;
}

bool MyanglishConverter::shouldAutoLockBeforeAppend(
    const std::string& current,
    char nextCharacter
) const {
    // Deprecated compatibility behavior: callers that still use this method
    // receive the post-append rolling decision. The alpha-0.8.1 TSF path does
    // not call it before appending anymore.
    std::string combined = toLowerAscii(trim(current));
    char next = nextCharacter;
    if (next >= 'A' && next <= 'Z') {
        next = static_cast<char>(next - 'A' + 'a');
    }
    if (next < 'a' || next > 'z') {
        return false;
    }
    combined.push_back(next);
    return findRollingSplit(combined) != 0;
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
            const auto candidates = getCandidates(words[index], 1);
            if (!candidates.empty()) {
                convertedWords.push_back(candidates.front().burmese);
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
