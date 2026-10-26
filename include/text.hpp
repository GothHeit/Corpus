#ifndef TEXT_HPP
#define TEXT_HPP

#include <string>

/// @brief Folds UTF-8 text to lowercase so two spellings that differ only in case compare equal: "Ação" and
/// "AÇÃO" both become "ação". Covers every letter Unicode gives a simple lowercase form: Latin (accented,
/// Vietnamese and fullwidth letters included), Greek (polytonic too), Cyrillic, Armenian, Georgian, Cherokee
/// and the rest. Accents are kept: this is how tag names are told apart, so "acao" and "ação" are two tags.
/// Bytes that aren't valid UTF-8 are kept as they are.
std::string fold_case(const std::string &utf8);

/// @brief Folds UTF-8 text for the search box's plain text: lowercase as fold_case, and without accents, so
/// "Ação", "acao" and "ACAO" all become "acao". Latin, Greek and Cyrillic letters lose their accents (Vietnamese
/// and polytonic Greek included), ß becomes "ss", ø, đ, ł, ı, ħ and ŧ their plain letter, and combining marks
/// (accents written as separate code points, as macOS writes file names) are dropped.
std::string fold_search(const std::string &utf8);

#endif
