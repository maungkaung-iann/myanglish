#include "Dictionary.h"
#include "MyanglishConverter.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"'); ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            out.push_back(field); field.clear();
        } else {
            field.push_back(ch);
        }
    }
    out.push_back(field);
    return out;
}

std::string csvQuote(const std::string& s) {
    std::string out = """;
    for (char ch : s) {
        if (ch == '"') out += """";
        else out.push_back(ch);
    }
    out += '"';
    return out;
}

std::filesystem::path sourceRoot() {
#ifdef MYANGLISHIME_SOURCE_DIR
    return std::filesystem::path(MYANGLISHIME_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

void appendIfExists(myanglish::Dictionary& dictionary, const std::filesystem::path& p) {
    std::string error;
    dictionary.appendFromCsv(p, &error, true);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr
            << "Usage:\n"
            << "  MyanglishCoverageAudit corpus.csv [output.csv]\n\n"
            << "corpus.csv columns: myanmar,myanglish\n"
            << "Multiple rows may use the same Myanmar word with alternate Myanglish spellings.\n";
        return 2;
    }

    const auto root = sourceRoot();
    myanglish::Dictionary dictionary;
    std::string error;
    if (!dictionary.loadFromCsv(root / "data" / "dictionary.csv", &error)) {
        std::cerr << "Dictionary load failed: " << error << "\n";
        return 1;
    }

    // Match the currently shipped non-personal IME candidate sources.
    appendIfExists(dictionary, root / "data" / "merged_candidates.csv");
    appendIfExists(dictionary, root / "data" / "alpha08_candidates.csv");

    myanglish::MyanglishConverter converter(std::move(dictionary), root);

    std::ifstream in(argv[1], std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Cannot open corpus: " << argv[1] << "\n";
        return 1;
    }

    const std::filesystem::path output =
        argc >= 3 ? std::filesystem::path(argv[2]) : std::filesystem::path("coverage.csv");
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "Cannot create output: " << output.string() << "\n";
        return 1;
    }

    out << "myanmar,myanglish,status,candidate_rank,candidates\n";

    std::string line;
    std::size_t lineNo = 0, tested = 0, passed = 0, failed = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (lineNo == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        if (trim(line).empty()) continue;

        const auto cols = splitCsv(line);
        if (cols.size() < 2) continue;

        const std::string myanmar = trim(cols[0]);
        const std::string roman = trim(cols[1]);
        if (lineNo == 1 && (myanmar == "myanmar" || myanmar == "burmese")) continue;
        if (myanmar.empty() || roman.empty()) continue;

        const auto candidates = converter.getCandidates(roman, 64);
        std::size_t rank = 0;
        std::ostringstream joined;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (i) joined << " | ";
            joined << candidates[i].burmese;
            if (rank == 0 && candidates[i].burmese == myanmar) rank = i + 1;
        }

        ++tested;
        if (rank != 0) ++passed; else ++failed;

        out << csvQuote(myanmar) << ','
            << csvQuote(roman) << ','
            << (rank ? "PASS" : "FAIL") << ','
            << (rank ? std::to_string(rank) : "") << ','
            << csvQuote(joined.str()) << "\n";
    }

    const double coverage = tested == 0 ? 0.0 : (100.0 * static_cast<double>(passed) / tested);
    std::cout << "=== MYANGLISH COVERAGE AUDIT ===\n"
              << "Tested: " << tested << "\n"
              << "PASS:   " << passed << "\n"
              << "FAIL:   " << failed << "\n"
              << "Coverage: " << coverage << "%\n"
              << "Output: " << output.string() << "\n";
    return failed == 0 ? 0 : 3;
}
