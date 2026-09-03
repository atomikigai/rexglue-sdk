/**
 * @file        codegen/codegen_language.cpp
 * @brief       Output language selection for the codegen backend (C++ or C11)
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include "codegen_language.h"
#include "codegen_flags.h"

namespace rex::codegen {

bool ParseLanguage(std::string_view text, Language& out) {
  if (text == "cpp") {
    out = Language::Cpp;
    return true;
  }
  if (text == "c") {
    out = Language::C;
    return true;
  }
  return false;
}

Language GetCodegenLanguage() {
  Language lang = Language::Cpp;
  // Unknown/empty values (should not happen: the cvar is allowlisted to
  // {"cpp", "c"}) fall back to the long-standing Cpp default.
  ParseLanguage(REXCVAR_GET(codegen_language), lang);
  return lang;
}

void SetCodegenLanguage(Language lang) {
  REXCVAR_GET(codegen_language) = (lang == Language::C) ? "c" : "cpp";
}

const char* LanguageSourceExt(Language lang) {
  return lang == Language::C ? "c" : "cpp";
}

}  // namespace rex::codegen
