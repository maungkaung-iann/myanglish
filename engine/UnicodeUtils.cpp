#include "UnicodeUtils.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace myanglish {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string::npos) {
        return {};
    }

    const auto last = text.find_last_not_of(" \t\r\n\f\v");
    return text.substr(first, last - first + 1);
}

std::string toLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::vector<std::string> splitWords(const std::string& text) {
    std::istringstream input(text);
    std::vector<std::string> words;
    std::string word;

    while (input >> word) {
        words.push_back(word);
    }

    return words;
}

std::string joinWords(const std::vector<std::string>& words, std::size_t start, std::size_t count) {
    std::string result;

    for (std::size_t index = 0; index < count; ++index) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result += words[start + index];
    }

    return result;
}

bool isBlank(const std::string& text) {
    return trim(text).empty();
}

std::size_t countWords(const std::string& text) {
    return splitWords(text).size();
}

} // namespace myanglish
