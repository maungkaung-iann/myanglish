#include "MyanglishConverter.h"
#include "Dictionary.h"
#include "UnicodeUtils.h"

#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
#include <conio.h>
#endif

namespace {

std::string trim(const std::string& input) {
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0) {
        ++start;
    }

    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }

    return input.substr(start, end - start);
}

std::string toLowerAscii(const std::string& input) {
    std::string lowered = input;
    for (char& ch : lowered) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return lowered;
}

bool isBlank(const std::string& input) {
    for (char ch : input) {
        if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> splitWords(const std::string& input) {
    std::vector<std::string> words;
    std::size_t index = 0;

    while (index < input.size()) {
        while (index < input.size() && std::isspace(static_cast<unsigned char>(input[index])) != 0) {
            ++index;
        }
        if (index >= input.size()) {
            break;
        }

        const std::size_t start = index;
        while (index < input.size() && std::isspace(static_cast<unsigned char>(input[index])) == 0) {
            ++index;
        }

        words.emplace_back(input.substr(start, index - start));
    }

    return words;
}

std::filesystem::path dictionaryPath() {
#ifdef MYANGLISHIME_SOURCE_DIR
    return std::filesystem::path(MYANGLISHIME_SOURCE_DIR) / "data" / "dictionary.csv";
#else
    return std::filesystem::current_path() / "data" / "dictionary.csv";
#endif
}

void configureUtf8Console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

int readKey() {
#ifdef _WIN32
    return _getch();
#else
    return std::cin.get();
#endif
}

int chooseCandidate(const std::vector<myanglish::Candidate>& candidates) {
    if (candidates.empty()) {
        return -1;
    }

    std::size_t selectedIndex = 0;
    std::cout << "Use 1-" << candidates.size() << ", Up/Down, Space, Enter, or Esc.\n";
    std::cout << "Selected: 1. " << candidates[0].burmese << "\r" << std::flush;

    while (true) {
        const int key = readKey();
        if (key >= '1' && key <= '9') {
            const std::size_t choice = static_cast<std::size_t>(key - '1');
            if (choice < candidates.size()) {
                std::cout << static_cast<char>(key) << '\n';
                return static_cast<int>(choice);
            }
        }
#ifdef _WIN32
        else if (key == 0 || key == 224) {
            const int arrowKey = readKey();
            if (arrowKey == 72 && selectedIndex > 0) {
                --selectedIndex;
            } else if (arrowKey == 80 && selectedIndex + 1 < candidates.size()) {
                ++selectedIndex;
            }
            std::cout << "\rSelected: " << (selectedIndex + 1) << ". " << candidates[selectedIndex].burmese << "      " << std::flush;
        }
#endif
        else if (key == '\r' || key == ' ') {
            std::cout << '\n';
            return static_cast<int>(selectedIndex);
        } else if (key == 27) {
            std::cout << '\n';
            return -1;
        }
    }
}

bool isExitCommand(const std::string& text) {
    return toLowerAscii(trim(text)) == "/exit";
}

} // namespace

int main() {
    configureUtf8Console();
    std::ios::sync_with_stdio(false);

    // Get and display the exact dictionary file being used.
    const std::filesystem::path dictionaryFile = dictionaryPath();

    std::cout << "Dictionary path: "
              << dictionaryFile.string()
              << '\n';

    std::cout << "Dictionary exists: "
              << (std::filesystem::exists(dictionaryFile) ? "YES" : "NO")
              << "\n\n";

    myanglish::Dictionary dictionary;
    std::string errorMessage;

    if (!dictionary.loadFromCsv(dictionaryFile, &errorMessage)) {
        std::cerr << "Failed to load dictionary: "
                  << errorMessage
                  << '\n';
        return 1;
    }

    myanglish::MyanglishConverter converter(std::move(dictionary));

    std::cout << "Myanglish IME Prototype\n";
    std::cout << "Type Myanglish or type /exit to close.\n\n";

    std::string inputLine;

    while (true) {
        std::cout << "Input: " << std::flush;

        if (!std::getline(std::cin, inputLine)) {
            break;
        }

        if (isExitCommand(inputLine)) {
            break;
        }

        if (isBlank(inputLine)) {
            continue;
        }

        const auto words = splitWords(inputLine);

        if (words.size() == 1) {
            const auto candidates = converter.getCandidates(words[0]);

            if (candidates.empty()) {
                std::cout << "Output: " << words[0] << "\n\n";
                continue;
            }

            if (candidates.size() == 1) {
                std::cout << "Output: "
                          << candidates[0].burmese
                          << "\n\n";
                continue;
            }

            std::cout << "\nCandidates:\n";

            for (std::size_t index = 0;
                 index < candidates.size();
                 ++index) {
                std::cout << (index + 1)
                          << ". "
                          << candidates[index].burmese
                          << '\n';
            }

            const int selectedIndex = chooseCandidate(candidates);

            if (selectedIndex >= 0 &&
                static_cast<std::size_t>(selectedIndex) <
                    candidates.size()) {
                std::cout
                    << "Output: "
                    << candidates[
                           static_cast<std::size_t>(selectedIndex)
                       ].burmese
                    << "\n\n";
            } else {
                std::cout << "Output: "
                          << words[0]
                          << "\n\n";
            }

            continue;
        }

        std::cout << "Output: "
                  << converter.convertSentence(inputLine)
                  << "\n\n";
    }

    return 0;
}