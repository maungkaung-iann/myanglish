#pragma once

#include "Dictionary.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
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

    bool isReady() const noexcept;

private:
    struct RhymeRule {
        std::string standaloneOutput;
        std::string dependentOutput;
        int frequency = 0;
    };

    static Candidate makeBestCandidate(const std::vector<DictionaryEntry>& entries);
    std::vector<Candidate> buildRuleCandidates(const std::string& text) const;

    Dictionary dictionary_;
    std::filesystem::path dataRoot_;
    std::unordered_map<std::string, std::vector<RhymeRule>> rhymeRulesByCode_;
    std::vector<std::string> rhymeCodesByLength_;
    std::unordered_map<std::string, std::vector<RhymeRule>> toneMarkRulesByCode_;
    std::vector<std::string> toneMarkCodesByLength_;
};

} // namespace myanglish
