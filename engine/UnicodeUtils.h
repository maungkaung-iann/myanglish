#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace myanglish {

std::string trim(const std::string& text);
std::string toLowerAscii(std::string text);
std::vector<std::string> splitWords(const std::string& text);
std::string joinWords(const std::vector<std::string>& words, std::size_t start, std::size_t count);
bool isBlank(const std::string& text);
std::size_t countWords(const std::string& text);

} // namespace myanglish
