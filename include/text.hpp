#ifndef TEXT_HPP
#define TEXT_HPP

#include <string>

/// Folds UTF-8 text to lowercase so spellings differing only in case compare equal ("Ação" and "AÇÃO"). Covers
/// every letter with a simple Unicode lowercase form. Accents are kept ("acao" and "ação" are two tags). Invalid
/// UTF-8 bytes are kept as they are.
std::string fold_case(const std::string &utf8);

/// Folds UTF-8 text for the search box: lowercase as fold_case, and without accents ("Ação" and "ACAO" both give
/// "acao"). Covers Latin, Greek and Cyrillic, ß becomes "ss", ø đ ł ı ħ ŧ their plain letter, and combining
/// marks (macOS writes file names decomposed) are dropped.
std::string fold_search(const std::string &utf8);

#endif
