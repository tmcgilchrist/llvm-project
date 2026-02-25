//===-- HaskellDemangle.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Decoder for the symbol names that GHC gives to Haskell code and data.
//
// GHC prints a label for a top-level Haskell binding (an "IdLabel") as
//
//   [<unit>_]<module>_<name>_<flavour>
//
// where the unit is omitted for the main unit, and each of <unit>, <module>
// and <name> is Z-encoded (GHC.Utils.Encoding.zEncodeString). Z-encoding
// turns every character other than [A-Za-y0-9] into an escape, including
// '_' (zu), so every literal '_' in a symbol is a separator. Examples:
//
//   Main_sumTree_info                     Main.sumTree
//   ghczminternal_GHCziInternalziBase_zpzp_info
//                                         GHC.Internal.Base.++
//   Main_Leaf_con_info                    Main.Leaf (con_info)
//   ZCMain_main_closure                   :Main.main (closure)
//
// With -fexpose-internal-symbols, GHC also emits local symbols for closures
// that are internal to a module:
//
//   _<module>_<name>_<unique>_<flavour>   _Main_sat_s1ZZ_entry
//
// With -fdistinct-constructor-tables, constructor info tables carry the
// module that allocates them and an index:
//
//   <unit>_<module>_<name>_<module'>_<n>_con_info
//
// See GHC.Cmm.CLabel (pprCLabelStyle, ppIdFlavor, ppInternalProcLabel) and
// GHC.Types.Name (pprName).
//
// RTS labels (stg_upd_frame_info), C symbols, and labels that have no module
// (SRTs, string literals, cost centres) are rejected.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_HASKELL_HASKELLDEMANGLE_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_HASKELL_HASKELLDEMANGLE_H

#include "llvm/ADT/StringRef.h"

#include <optional>
#include <string>

namespace lldb_private {
namespace haskell {

/// Decodes a Z-encoded string.
///
/// Returns std::nullopt if \p encoded is not something GHC's encoder could
/// have produced: an unknown or truncated escape, or a character outside
/// [A-Za-z0-9].
std::optional<std::string> ZDecode(llvm::StringRef encoded);

/// What a GHC symbol labels, from the flavour suffix of its name.
enum class GHCSymbolKind {
  /// `_info`: the info table. With tables-next-to-code this is also the
  /// address of the entry code, so it names the function.
  Info,
  /// `_entry`: entry code, when it has its own label.
  Entry,
  /// `_slow`: slow entry point, used for unknown calls.
  Slow,
  /// `_closure`: the static closure (data).
  Closure,
  /// `_con_info`: the info table of a data constructor.
  ConInfo,
  /// `_con_entry`: the entry code of a data constructor.
  ConEntry,
  /// `_closure_tbl`: the table of nullary-constructor closures of a type.
  ClosureTable,
  /// `_bytes`: the bytes of a string literal.
  Bytes,
  /// `_ct`: a ticky-ticky counter.
  TickyCounter,
};

/// A decoded GHC symbol name.
struct GHCSymbol {
  /// The decoded unit id (`ghc-internal`, `base-4.21.0.0-ae91`), or empty
  /// for the main unit.
  std::string unit;
  /// The decoded module name (`GHC.Internal.Base`, `:Main`).
  std::string module;
  /// The decoded occurrence name (`++`, `$wmkTree`, `:`).
  std::string name;
  GHCSymbolKind kind = GHCSymbolKind::Info;
  /// For -fexpose-internal-symbols labels, the unique that tells apart
  /// local closures with the same name (`s1ZZ`). Empty otherwise.
  std::string unique;
  /// For -fdistinct-constructor-tables, the module that allocates the
  /// constructor (`Main`), and the index of the table within it.
  std::string usage_module;
  unsigned usage_index = 0;

  bool IsInternal() const { return !unique.empty(); }
  bool HasUsageSite() const { return !usage_module.empty(); }

  /// True for kinds that label code rather than data.
  bool IsCode() const;

  /// The qualified Haskell name: `Main.sumTree`, `GHC.Internal.Base.++`,
  /// or `Main.sat_s1ZZ` for an internal symbol. This is the name to match
  /// lookups against.
  std::string GetQualifiedName() const;

  /// The qualified name, followed by the kind when the symbol does not
  /// label a function's code: `Main.sumTree`, `Main.sumTree (closure)`,
  /// `Main.Leaf (con_info)`. This is the name to show to users.
  std::string GetDisplayName() const;
};

/// Returns the short description of \p kind used by GetDisplayName, or an
/// empty string for the kinds that name a function (Info, Entry).
llvm::StringRef GetKindDescription(GHCSymbolKind kind);

/// Parses a symbol name produced by GHC. Returns std::nullopt for anything
/// else, including RTS and C symbols.
std::optional<GHCSymbol> ParseGHCSymbol(llvm::StringRef symbol);

} // namespace haskell
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_HASKELL_HASKELLDEMANGLE_H
