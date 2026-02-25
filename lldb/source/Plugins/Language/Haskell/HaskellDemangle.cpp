//===-- HaskellDemangle.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaskellDemangle.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"

using namespace lldb_private;
using namespace lldb_private::haskell;

// Decodes the escape that follows 'Z' at the start of `s`, consuming it.
static bool DecodeUpper(llvm::StringRef &s, std::string &out) {
  if (s.empty())
    return false;

  // Tuples: Z<n>T is the boxed n-tuple, Z<n>H the unboxed one. GHC encodes
  // "()" as Z0T, "(##)" as Z0H, and "(# #)" as Z1H. See decode_tuple in
  // GHC.Utils.Encoding.
  if (llvm::isDigit(s.front())) {
    size_t digits = s.find_if_not(llvm::isDigit);
    if (digits == llvm::StringRef::npos)
      return false;
    unsigned arity;
    if (s.take_front(digits).getAsInteger(10, arity) || arity > 64)
      return false;
    char close = s[digits];
    if (close != 'T' && close != 'H')
      return false;
    s = s.drop_front(digits + 1);
    bool unboxed = close == 'H';
    if (arity == 0) {
      out += unboxed ? "(##)" : "()";
    } else if (unboxed && arity == 1) {
      out += "(# #)";
    } else {
      out += unboxed ? "(#" : "(";
      out.append(arity - 1, ',');
      out += unboxed ? "#)" : ")";
    }
    return true;
  }

  // See decode_upper in GHC.Utils.Encoding.
  static constexpr llvm::StringLiteral codes = "LRMNCZ";
  static constexpr llvm::StringLiteral chars = "()[]:Z";
  static_assert(codes.size() == chars.size());
  size_t index = codes.find(s.front());
  if (index == llvm::StringRef::npos)
    return false;
  out += chars[index];
  s = s.drop_front();
  return true;
}

// Decodes the escape that follows 'z' at the start of `s`, consuming it.
static bool DecodeLower(llvm::StringRef &s, std::string &out) {
  if (s.empty())
    return false;

  // Any other character: z<hex>U, where the hex digits always start with a
  // decimal digit (GHC adds a leading 0 otherwise).
  if (llvm::isDigit(s.front())) {
    size_t digits = s.find_if_not(llvm::isHexDigit);
    if (digits == llvm::StringRef::npos || s[digits] != 'U')
      return false;
    unsigned code_point;
    if (s.take_front(digits).getAsInteger(16, code_point) ||
        code_point > 0x10FFFF)
      return false;
    char utf8[UNI_MAX_UTF8_BYTES_PER_CODE_POINT];
    char *end = utf8;
    if (!llvm::ConvertCodePointToUTF8(code_point, end))
      return false;
    out.append(utf8, end);
    s = s.drop_front(digits + 1);
    return true;
  }

  // See decode_lower in GHC.Utils.Encoding.
  static constexpr llvm::StringLiteral codes = "abcdeghilmnpqrstuvz";
  static constexpr llvm::StringLiteral chars = "&|^$=>#.<-!+'\\/*_%z";
  static_assert(codes.size() == chars.size());
  size_t index = codes.find(s.front());
  if (index == llvm::StringRef::npos)
    return false;
  out += chars[index];
  s = s.drop_front();
  return true;
}

std::optional<std::string> haskell::ZDecode(llvm::StringRef encoded) {
  std::string result;
  result.reserve(encoded.size());
  while (!encoded.empty()) {
    char c = encoded.front();
    encoded = encoded.drop_front();
    if (c == 'Z') {
      if (!DecodeUpper(encoded, result))
        return std::nullopt;
    } else if (c == 'z') {
      if (!DecodeLower(encoded, result))
        return std::nullopt;
    } else if (llvm::isAlnum(c)) {
      result += c;
    } else {
      return std::nullopt;
    }
  }
  return result;
}

// A module name is a dot-separated list of conids. Non-ASCII bytes are
// accepted anywhere, since GHC allows Unicode letters in module names.
static bool IsModuleName(llvm::StringRef name) {
  // The module of the program's real entry point, `:Main.main`.
  if (name == ":Main")
    return true;

  llvm::SmallVector<llvm::StringRef, 4> segments;
  name.split(segments, '.', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
  return llvm::all_of(segments, [](llvm::StringRef segment) {
    if (segment.empty())
      return false;
    unsigned char first = segment.front();
    if (!llvm::isUpper(first) && first < 0x80)
      return false;
    return llvm::all_of(segment.drop_front(), [](unsigned char c) {
      return llvm::isAlnum(c) || c == '_' || c == '\'' || c >= 0x80;
    });
  });
}

namespace {
struct Flavour {
  llvm::ArrayRef<llvm::StringRef> parts;
  GHCSymbolKind kind;
};
} // namespace

// The flavour suffixes from GHC.Cmm.CLabel.ppIdFlavor, longest first.
static const llvm::StringRef g_con_info[] = {"con", "info"};
static const llvm::StringRef g_con_entry[] = {"con", "entry"};
static const llvm::StringRef g_closure_tbl[] = {"closure", "tbl"};
static const llvm::StringRef g_info[] = {"info"};
static const llvm::StringRef g_entry[] = {"entry"};
static const llvm::StringRef g_slow[] = {"slow"};
static const llvm::StringRef g_closure[] = {"closure"};
static const llvm::StringRef g_bytes[] = {"bytes"};
static const llvm::StringRef g_ct[] = {"ct"};

static const Flavour g_flavours[] = {
    {g_con_info, GHCSymbolKind::ConInfo},
    {g_con_entry, GHCSymbolKind::ConEntry},
    {g_closure_tbl, GHCSymbolKind::ClosureTable},
    {g_info, GHCSymbolKind::Info},
    {g_entry, GHCSymbolKind::Entry},
    {g_slow, GHCSymbolKind::Slow},
    {g_closure, GHCSymbolKind::Closure},
    {g_bytes, GHCSymbolKind::Bytes},
    {g_ct, GHCSymbolKind::TickyCounter},
};

// Parses `[unit_]module_name` into `sym`.
static bool ParseQualifiedName(llvm::ArrayRef<llvm::StringRef> parts,
                               GHCSymbol &sym) {
  if (parts.size() != 2 && parts.size() != 3)
    return false;
  // zEncodeString escapes a leading digit, so no encoded unit, module or
  // name starts with one.
  if (llvm::any_of(parts, [](llvm::StringRef part) {
        return llvm::isDigit(part.front());
      }))
    return false;

  if (parts.size() == 3) {
    std::optional<std::string> unit = ZDecode(parts[0]);
    // "stg" is the RTS's namespace (stg_CAF_BLACKHOLE_info), not a unit.
    if (!unit || *unit == "stg")
      return false;
    sym.unit = std::move(*unit);
    parts = parts.drop_front();
  }

  std::optional<std::string> module = ZDecode(parts[0]);
  if (!module || !IsModuleName(*module))
    return false;
  std::optional<std::string> name = ZDecode(parts[1]);
  if (!name || name->empty())
    return false;
  sym.module = std::move(*module);
  sym.name = std::move(*name);
  return true;
}

// Parses the part of a symbol before its flavour suffix.
static bool ParseBody(llvm::ArrayRef<llvm::StringRef> parts, bool internal,
                      GHCSymbol &sym) {
  if (internal) {
    // _[unit_]module_name_unique: the unique is printed as is.
    if (parts.size() < 3)
      return false;
    sym.unique = parts.back().str();
    return ParseQualifiedName(parts.drop_back(), sym);
  }

  bool is_con =
      sym.kind == GHCSymbolKind::ConInfo || sym.kind == GHCSymbolKind::ConEntry;
  if (is_con && parts.size() >= 4 &&
      llvm::all_of(parts.back(), llvm::isDigit)) {
    // [unit_]module_Con_[unit'_]module'_n: a constructor table for one
    // allocation site. The constructor's unit is present unless it is the
    // main unit, and the allocating module's unit unless that is the main
    // unit. A main-unit constructor can only be used from the main unit.
    if (parts.back().getAsInteger(10, sym.usage_index))
      return false;
    parts = parts.drop_back();
    size_t con_parts;
    switch (parts.size()) {
    case 3: // Module_Con_Module'
    case 4: // unit_Module_Con_Module'
      con_parts = parts.size() - 1;
      break;
    case 5: // unit_Module_Con_unit'_Module'
      con_parts = 3;
      break;
    default:
      return false;
    }
    std::optional<std::string> usage_module = ZDecode(parts.back());
    if (!usage_module || !IsModuleName(*usage_module))
      return false;
    if (parts.size() == 5 && !ZDecode(parts[3]))
      return false;
    sym.usage_module = std::move(*usage_module);
    return ParseQualifiedName(parts.take_front(con_parts), sym);
  }

  return ParseQualifiedName(parts, sym);
}

std::optional<GHCSymbol> haskell::ParseGHCSymbol(llvm::StringRef symbol) {
  bool internal = symbol.consume_front("_");

  llvm::SmallVector<llvm::StringRef, 8> parts;
  symbol.split(parts, '_', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
  // Every part is Z-encoded or a number, so it is non-empty and
  // alphanumeric.
  for (llvm::StringRef part : parts)
    if (part.empty() || !llvm::all_of(part, llvm::isAlnum))
      return std::nullopt;

  // Try the longest matching flavour first, and fall back to a shorter one
  // if the rest doesn't parse: `pkg_M_con_info` is `pkg:M.con`'s info table.
  llvm::ArrayRef<llvm::StringRef> all(parts);
  for (const Flavour &flavour : g_flavours) {
    if (all.size() <= flavour.parts.size() ||
        all.take_back(flavour.parts.size()) != flavour.parts)
      continue;
    GHCSymbol sym;
    sym.kind = flavour.kind;
    if (ParseBody(all.drop_back(flavour.parts.size()), internal, sym))
      return sym;
  }
  return std::nullopt;
}

bool GHCSymbol::IsCode() const {
  switch (kind) {
  case GHCSymbolKind::Info:
  case GHCSymbolKind::Entry:
  case GHCSymbolKind::Slow:
  case GHCSymbolKind::ConInfo:
  case GHCSymbolKind::ConEntry:
    return true;
  case GHCSymbolKind::Closure:
  case GHCSymbolKind::ClosureTable:
  case GHCSymbolKind::Bytes:
  case GHCSymbolKind::TickyCounter:
    return false;
  }
  llvm_unreachable("unhandled GHCSymbolKind");
}

llvm::StringRef haskell::GetKindDescription(GHCSymbolKind kind) {
  switch (kind) {
  case GHCSymbolKind::Info:
  case GHCSymbolKind::Entry:
    return "";
  case GHCSymbolKind::Slow:
    return "slow entry";
  case GHCSymbolKind::Closure:
    return "closure";
  case GHCSymbolKind::ConInfo:
    return "con_info";
  case GHCSymbolKind::ConEntry:
    return "con_entry";
  case GHCSymbolKind::ClosureTable:
    return "closure_tbl";
  case GHCSymbolKind::Bytes:
    return "bytes";
  case GHCSymbolKind::TickyCounter:
    return "ticky counter";
  }
  llvm_unreachable("unhandled GHCSymbolKind");
}

std::string GHCSymbol::GetQualifiedName() const {
  std::string result = module + "." + name;
  if (IsInternal())
    result += "_" + unique;
  return result;
}

std::string GHCSymbol::GetDisplayName() const {
  std::string result = GetQualifiedName();
  llvm::StringRef description = GetKindDescription(kind);
  if (HasUsageSite()) {
    result += " (" + description.str() + ", " + usage_module + ":" +
              std::to_string(usage_index) + ")";
  } else if (!description.empty()) {
    result += " (" + description.str() + ")";
  }
  return result;
}
