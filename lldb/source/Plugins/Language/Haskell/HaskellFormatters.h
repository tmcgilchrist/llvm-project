//===-- HaskellFormatters.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// GHC closure formatters for LLDB.
//
// GHC's runtime represents all Haskell values as closures on the heap.
// Each closure starts with a pointer to its info table, which contains
// the closure type and layout information. This module reads the info
// table from process memory and produces human-readable descriptions.
//
// Closure layout (x86_64, TABLES_NEXT_TO_CODE):
//
//   [con_desc offset] [-24]   (constructors only, OFFSET_FIELD)
//   [layout]          [-16]   (bitmap or payload size)
//   [type]            [-8]    (closure type, a half-word)
//   [srt]             [-4]    (constructor tag, for a constructor)
//   [entry code]      [0]     <-- info pointer points here
//
// From StgInfoTable and StgConInfoTable in rts/include/rts/storage/
// InfoTables.h. con_desc is relative to the info pointer (GET_CON_DESC).
//
//   Closure on heap:
//   [info_ptr | payload_0 | payload_1 | ...]
//
// Pointer tagging: low 3 bits encode constructor tag or evaluation state.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_HASKELL_HASKELLFORMATTERS_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_HASKELL_HASKELLFORMATTERS_H

#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"

namespace lldb_private {
namespace formatters {

/// Summary provider for GHC closures.
///
/// Reads the info table from process memory, determines the closure type,
/// and produces a one-line summary:
///   - CONSTR:    "Just" or "(:) 42 ..."
///   - THUNK:     "<thunk>"
///   - BLACKHOLE: "<blackhole>"
///   - FUN:       "<function>"
///   - PAP:       "<partial application>"
///   - IND:       follows indirection chain
bool HaskellClosureSummaryProvider(ValueObject &valobj, Stream &s,
                                   const TypeSummaryOptions &options);

} // namespace formatters
} // namespace lldb_private

// GHC closure type constants (from rts/include/rts/storage/ClosureTypes.h)
namespace ghc {

enum ClosureType : uint32_t {
  INVALID_OBJECT = 0,
  CONSTR = 1,
  CONSTR_1_0 = 2,
  CONSTR_0_1 = 3,
  CONSTR_2_0 = 4,
  CONSTR_1_1 = 5,
  CONSTR_0_2 = 6,
  CONSTR_NOCAF = 7,
  FUN = 8,
  FUN_1_0 = 9,
  FUN_0_1 = 10,
  FUN_2_0 = 11,
  FUN_1_1 = 12,
  FUN_0_2 = 13,
  FUN_STATIC = 14,
  THUNK = 15,
  THUNK_1_0 = 16,
  THUNK_0_1 = 17,
  THUNK_2_0 = 18,
  THUNK_1_1 = 19,
  THUNK_0_2 = 20,
  THUNK_STATIC = 21,
  THUNK_SELECTOR = 22,
  BCO = 23,
  AP = 24,
  PAP = 25,
  AP_STACK = 26,
  IND = 27,
  IND_STATIC = 28,
  BLACKHOLE = 38,
};

inline bool IsConstr(uint32_t t) { return t >= CONSTR && t <= CONSTR_NOCAF; }
inline bool IsFun(uint32_t t) { return t >= FUN && t <= FUN_STATIC; }
inline bool IsThunk(uint32_t t) { return t >= THUNK && t <= THUNK_SELECTOR; }
inline bool IsInd(uint32_t t) { return t == IND || t == IND_STATIC; }

const char *ClosureTypeName(uint32_t type);

} // namespace ghc

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_HASKELL_HASKELLFORMATTERS_H
