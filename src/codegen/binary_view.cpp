/**
 * @file        codegen/binary_view.cpp
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <limits>

#include <rex/codegen/binary_view.h>
#include <rex/logging.h>

#include "codegen_logging.h"
#include <rex/memory/utils.h>
#include <rex/system/binary_types.h>
#include <rex/system/module.h>

using rex::memory::load_and_swap;

namespace rex::codegen {

namespace {

// xex2_export_table layout (see rex/system/util/xex2_info.h and
// XexModule::GetProcAddress(ordinal), which parses the same structure from
// live guest memory): magic[3], modulenumber[2], version[3], imagebaseaddr,
// count, base, then `count` ordOffset words. All fields are big-endian.
constexpr uint32_t kExportTableHeaderSize = 0x2C;

// Parses this binary's own XEX2 export table (ordinal -> guest address) from
// already-copied section data. Self-contained: no live guest memory access,
// so it works the same way at codegen analysis time as it does for a loaded
// runtime::Module. Returns an empty vector if there is no export table, or
// it fails to fit within its containing section.
std::vector<ExportSymbol> parseExportTable(uint32_t exportTableAddr,
                                            std::span<const SectionView> sections) {
  std::vector<ExportSymbol> result;
  if (exportTableAddr == 0) {
    return result;
  }

  const SectionView* section = nullptr;
  for (const auto& candidate : sections) {
    if (candidate.contains(exportTableAddr)) {
      section = &candidate;
      break;
    }
  }
  if (!section) {
    return result;
  }

  uint32_t offset = exportTableAddr - section->baseAddress;
  if (offset + kExportTableHeaderSize > section->size) {
    return result;
  }
  const uint8_t* header = section->data + offset;

  uint32_t imagebaseaddr = load_and_swap<uint32_t>(header + 0x20);
  uint32_t count = load_and_swap<uint32_t>(header + 0x24);
  uint32_t base = load_and_swap<uint32_t>(header + 0x28);

  // Reject implausible counts rather than reading past the section: a
  // corrupt/foreign table should degrade to "no exports", not a crash.
  if (count == 0 || count > 65535 ||
      offset + kExportTableHeaderSize + static_cast<uint64_t>(count) * 4 > section->size) {
    return result;
  }

  result.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t ordOffset = load_and_swap<uint32_t>(header + kExportTableHeaderSize + i * 4);
    if (ordOffset == 0) {
      continue;  // Unused ordinal slot
    }
    uint32_t address = ordOffset + (imagebaseaddr << 16);
    result.push_back(ExportSymbol{.ordinal = static_cast<uint16_t>(base + i), .address = address});
  }
  return result;
}

}  // namespace

BinaryView BinaryView::fromModule(const runtime::Module& module) {
  BinaryView view;

  // Copy metadata
  view.baseAddress_ = module.base_address();
  view.imageSize_ = module.image_size();
  view.entryPoint_ = module.entry_point();
  view.exceptionDirectoryAddr_ = module.exception_directory_address();
  view.exceptionDirectorySize_ = module.exception_directory_size();
  view.exportTableAddr_ = module.export_table_address();

  // Copy import symbols and calculate import thunk table start
  // The import thunk table (and export table after it) extends to end of .text
  uint32_t minImportAddr = std::numeric_limits<uint32_t>::max();
  for (const auto& sym : module.binary_symbols()) {
    if (sym.type == runtime::BinarySymbolType::Import) {
      // Copy symbol for Register phase to use
      view.importSymbols_.push_back(ImportSymbol{.address = sym.address, .name = sym.name});

      // Track minimum address for thunk table range
      if (sym.address != 0 && sym.address < minImportAddr) {
        minImportAddr = sym.address;
      }
    }
  }
  if (minImportAddr != std::numeric_limits<uint32_t>::max()) {
    view.importThunkTableStart_ = minImportAddr;
    REXCODEGEN_DEBUG("BinaryView: import thunk table starts at 0x{:08X}", minImportAddr);

    // Find section containing the import thunk table to determine end of import/export range
    for (const auto& section : module.binary_sections()) {
      uint32_t sectionEnd = section.virtual_address + section.virtual_size;
      if (minImportAddr >= section.virtual_address && minImportAddr < sectionEnd) {
        view.importExportRangeEnd_ = sectionEnd;
        REXCODEGEN_DEBUG("BinaryView: import/export range ends at 0x{:08X} (end of {} section)",
                         view.importExportRangeEnd_, section.name);
        break;
      }
    }
  }
  REXCODEGEN_DEBUG("BinaryView: copied {} import symbols", view.importSymbols_.size());

  // Copy section data
  const auto& binarySections = module.binary_sections();
  view.sectionNames_.reserve(binarySections.size());
  view.sectionData_.reserve(binarySections.size());
  view.sections_.reserve(binarySections.size());

  uint32_t imageEnd = view.baseAddress_ + view.imageSize_;
  for (const auto& section : binarySections) {
    // Skip sections not mapped into memory
    if (!section.host_data || section.virtual_size == 0) {
      REXCODEGEN_DEBUG("BinaryView: skipping unmapped section '{}'", section.name);
      continue;
    }
    // Skip sections that extend beyond the loaded image
    if (section.virtual_address + section.virtual_size > imageEnd) {
      REXCODEGEN_DEBUG("BinaryView: skipping section '{}' (extends past image end 0x{:08X})",
                       section.name, imageEnd);
      continue;
    }

    // Copy name
    view.sectionNames_.push_back(std::string(section.name));

    // Copy bytes
    view.sectionData_.emplace_back(section.host_data, section.host_data + section.virtual_size);

    // Create view pointing into our owned data
    view.sections_.push_back(SectionView{.name = view.sectionNames_.back(),
                                         .baseAddress = section.virtual_address,
                                         .size = section.virtual_size,
                                         .data = view.sectionData_.back().data(),
                                         .executable = section.executable});

    REXCODEGEN_DEBUG("BinaryView: section '{}' at 0x{:08X} size 0x{:X} exec={}", section.name,
                     section.virtual_address, section.virtual_size, section.executable);
  }

  REXCODEGEN_DEBUG("BinaryView: loaded {} sections, base=0x{:08X}, size=0x{:X}",
                   view.sections_.size(), view.baseAddress_, view.imageSize_);

  view.exportSymbols_ = parseExportTable(view.exportTableAddr_, view.sections_);
  if (!view.exportSymbols_.empty()) {
    REXCODEGEN_DEBUG("BinaryView: parsed {} export(s) from table at 0x{:08X}",
                     view.exportSymbols_.size(), view.exportTableAddr_);
  }

  return view;
}

const uint8_t* BinaryView::translate(uint32_t addr) const {
  for (const auto& section : sections_) {
    if (auto* ptr = section.translate(addr)) {
      return ptr;
    }
  }
  return nullptr;
}

bool BinaryView::isExecutable(uint32_t addr) const {
  if (auto* section = findSection(addr)) {
    return section->executable;
  }
  return false;
}

const SectionView* BinaryView::findSection(uint32_t addr) const {
  for (const auto& section : sections_) {
    if (section.contains(addr)) {
      return &section;
    }
  }
  return nullptr;
}

const SectionView* BinaryView::findSectionByName(std::string_view name) const {
  for (const auto& section : sections_) {
    if (section.name == name) {
      return &section;
    }
  }
  return nullptr;
}

}  // namespace rex::codegen
