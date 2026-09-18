#pragma once

#include "Dictionary.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace myanglish {

struct Candidate {
    std::string burmese;
    int frequency = 0;
};

class MyanglishConverter {
public:
    explicit MyanglishConverter(Dictionary dictionary, std::filesystem::path dataRoot = {});

    std::string convertSentence(const std::string& input) const;
    std::vector<Candidate> getCandidates(const std::string& myanglish, std::size_t limit = 5) const;
    // Legacy engine helper retained for tests/tools. The Windows IME no longer
    // uses automatic whole-buffer segmentation in alpha-0.6.1.
    std::vector<Candidate> getContinuousCandidates(const std::string& myanglish, std::size_t limit = 9) const;

    // Word-boundary helpers for Japanese-style live *word* conversion.
    // A previous word is auto-locked only when it is an explicit known input
    // and adding the next letter can no longer continue any known input.
    bool hasExactInput(const std::string& myanglish) const;
    bool hasInputPrefix(const std::string& prefix) const;

    // Alpha-0.8.1 rolling-buffer helper. Returns 0 while the entire raw input
    // can still be the same word. Otherwise returns a split index where the
    // left side is a complete Myanglish word and the right side is a plausible
    // beginning/current word. This decision is made AFTER appending the new
    // character, so a valid prefix such as ba/ban is never committed early
    // while the user is still reaching bank.
    std::size_t findRollingSplit(const std::string& combinedInput) const;

    bool isReady() const noexcept;

private:
    struct RhymeRule {
        std::string standaloneOutput;
        std::string dependentOutput;
        int frequency = 0;
    };

    struct MasterRhymeRule {
        std::string burmese;
        int score = 0;
    };

    static Candidate makeBestCandidate(const std::vector<DictionaryEntry>& entries);
    std::vector<Candidate> buildRuleCandidates(const std::string& text) const;
    std::vector<Candidate> buildPrefixCandidates(const std::string& text) const;
    std::vector<Candidate> buildCoreCandidates(const std::string& text) const;
    bool isKnownBurmeseWord(const std::string& text) const noexcept;
    void loadBurmeseLexicon();

    Dictionary dictionary_;
    std::filesystem::path dataRoot_;
    std::unordered_map<std::string, std::vector<RhymeRule>> rhymeRulesByCode_;
    std::vector<std::string> rhymeCodesByLength_;
    std::unordered_map<std::string, std::vector<RhymeRule>> toneMarkRulesByCode_;
    std::vector<std::string> toneMarkCodesByLength_;
    std::unordered_map<std::string, std::vector<MasterRhymeRule>> masterRhymeRulesByCode_;
    std::unordered_map<std::string, std::vector<Candidate>> historicalCandidatesByInput_;
    std::unordered_map<std::string, std::unordered_set<int>> suffixFamiliesByCode_;
    std::unordered_set<std::string> burmeseLexicon_;
};

} // namespace myanglish
