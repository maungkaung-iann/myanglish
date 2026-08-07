#include "Dictionary.h"
#include "UnicodeUtils.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace myanglish {

namespace {

bool parseCsvLine(
    const std::string& line,
    std::string& first,
    std::string& second,
    std::string& third
) {
    std::stringstream stream(line);

    if (!std::getline(stream, first, ',')) {
        return false;
    }

    if (!std::getline(stream, second, ',')) {
        return false;
    }

    if (!std::getline(stream, third)) {
        return false;
    }

    first = trim(first);
    second = trim(second);
    third = trim(third);

    return true;
}

std::string stripUtf8Bom(std::string text) {
    if (
        text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF
    ) {
        text.erase(0, 3);
    }

    return text;
}

bool isHeaderRow(
    const std::string& first,
    const std::string& second,
    const std::string& third
) {
    return toLowerAscii(trim(first)) == "myanglish" &&
           toLowerAscii(trim(second)) == "burmese" &&
           toLowerAscii(trim(third)) == "frequency";
}

} // namespace

bool Dictionary::loadFromCsv(
    const std::filesystem::path& path,
    std::string* errorMessage
) {
    entries_.clear();
    byKey_.clear();
    longestPhraseLength_ = 1;

    std::ifstream file(path, std::ios::binary);

    if (!file.is_open()) {
        if (errorMessage != nullptr) {
            *errorMessage =
                "Could not open dictionary file: " + path.u8string();
        }

        return false;
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

        std::string myanglish;
        std::string burmese;
        std::string frequencyText;

        if (!parseCsvLine(
                trimmedLine,
                myanglish,
                burmese,
                frequencyText
            )) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "Invalid CSV format on line " +
                    std::to_string(lineNumber);
            }

            return false;
        }

        if (isHeaderRow(myanglish, burmese, frequencyText)) {
            continue;
        }

        myanglish = toLowerAscii(trim(myanglish));
        burmese = trim(burmese);
        frequencyText = trim(frequencyText);

        if (myanglish.empty() || burmese.empty()) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "Empty dictionary value on line " +
                    std::to_string(lineNumber);
            }

            return false;
        }

        int frequency = 0;

        try {
            frequency = std::stoi(frequencyText);
        } catch (...) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "Invalid frequency on line " +
                    std::to_string(lineNumber) +
                    ": " +
                    frequencyText;
            }

            return false;
        }

        DictionaryEntry entry{
            myanglish,
            burmese,
            frequency
        };

        entries_.push_back(entry);

        const std::string lookupKey =
            makeLookupKey(myanglish);

        byKey_[lookupKey].push_back(entry);

        const std::size_t phraseLength =
            countWords(myanglish);

        if (phraseLength > longestPhraseLength_) {
            longestPhraseLength_ = phraseLength;
        }
    }

    if (entries_.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage =
                "Dictionary file is empty: " +
                path.u8string();
        }

        return false;
    }

    return true;
}

const std::vector<DictionaryEntry>&
Dictionary::entries() const noexcept {
    return entries_;
}

std::vector<DictionaryEntry>
Dictionary::findEntries(
    const std::string& myanglish
) const {
    const std::string lookupKey =
        makeLookupKey(myanglish);

    const auto it = byKey_.find(lookupKey);

    if (it == byKey_.end()) {
        return {};
    }

    return it->second;
}

std::size_t
Dictionary::longestPhraseLength() const noexcept {
    return longestPhraseLength_;
}

std::string Dictionary::makeLookupKey(
    const std::string& text
) {
    return toLowerAscii(trim(text));
}

}