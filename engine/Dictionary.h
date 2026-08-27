#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace myanglish {

struct DictionaryEntry {
    std::string myanglish;
    std::string burmese;
    int frequency = 0;
};

class Dictionary {
public:
    bool loadFromCsv(const std::filesystem::path& path, std::string* errorMessage = nullptr);
    bool appendFromCsv(
        const std::filesystem::path& path,
        std::string* errorMessage = nullptr,
        bool missingIsOk = true
    );
    void addEntry(DictionaryEntry entry);

    const std::vector<DictionaryEntry>& entries() const noexcept;
    std::vector<DictionaryEntry> findEntries(const std::string& myanglish) const;
    std::size_t longestPhraseLength() const noexcept;

private:
    bool readCsv(
        const std::filesystem::path& path,
        bool clearFirst,
        bool missingIsOk,
        std::string* errorMessage
    );
    static std::string makeLookupKey(const std::string& text);

    std::vector<DictionaryEntry> entries_;
    std::unordered_map<std::string, std::vector<DictionaryEntry>> byKey_;
    std::size_t longestPhraseLength_ = 1;
};

} // namespace myanglish
