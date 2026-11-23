// fuzzy.hpp - forgiving station-name matching ("imam khomeini" finds "Emam Khomeini").
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace metro {

// Lower-case ASCII letters and digits only; spaces, '-', '\'' and other ASCII
// punctuation are dropped. UTF-8 (Persian) bytes are kept, with Arabic yeh/kaf
// mapped to the Persian letters so both spellings match.
std::string normalize(std::string_view text);

// Edit distance (insert, delete, substitute = 1) between two byte strings.
std::size_t levenshtein(std::string_view a, std::string_view b);

// 0..1 similarity between a normalized query and a normalized candidate.
double similarity(std::string_view query, std::string_view candidate);

}  // namespace metro
