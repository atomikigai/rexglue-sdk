/**
 * @file        codegen/codegen_language.h
 * @brief       Output language selection for the codegen backend (C++ or C11)
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <string_view>

namespace rex::codegen {

/// Output language for generated recompiler code. Cpp is the long-standing
/// default and preserves the existing observable output byte-for-byte; C
/// emits C11 (gnu11) with no C++-only constructs.
enum class Language { Cpp, C };

/// Parses "cpp" or "c" (case-sensitive, matches the CLI/manifest spelling).
/// Returns false and leaves `out` untouched for anything else.
bool ParseLanguage(std::string_view text, Language& out);

/// Process-wide codegen output language for the current `rexglue` invocation.
/// Backed by the `codegen_language` cvar (see codegen_flags.h) so it can be
/// set generically via `--codegen_language=c`, or explicitly via the `--lang`
/// flag on `rexglue codegen`, before any CodegenWriter::write() call reads it.
Language GetCodegenLanguage();

/// Sets the process-wide codegen output language (writes the backing cvar).
void SetCodegenLanguage(Language lang);

/// Source-file extension for `lang`, without a leading dot ("cpp" or "c").
const char* LanguageSourceExt(Language lang);

}  // namespace rex::codegen
