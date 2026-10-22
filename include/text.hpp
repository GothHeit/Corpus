#ifndef TEXT_HPP
#define TEXT_HPP

#include <string>

/// @brief Folds UTF-8 text to lowercase so two spellings that differ only in case compare equal: "Ação" and
/// "AÇÃO" both become "ação". Covers every letter Unicode gives a simple lowercase form: Latin (accented,
/// Vietnamese and fullwidth letters included), Greek (polytonic too), Cyrillic, Armenian, Georgian, Cherokee
/// and the rest. Accents are kept, so "acao" still doesn't match "ação". Bytes that aren't valid UTF-8 are
/// kept as they are.
std::string fold_case(const std::string &utf8);

#endif
