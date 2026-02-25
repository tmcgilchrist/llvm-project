//===-- HaskellFormatters.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaskellFormatters.h"

#include "lldb/ValueObject/ValueObject.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

namespace ghc {

const char *ClosureTypeName(uint32_t type) {
  switch (type) {
  case CONSTR:         return "CONSTR";
  case CONSTR_1_0:     return "CONSTR_1_0";
  case CONSTR_0_1:     return "CONSTR_0_1";
  case CONSTR_2_0:     return "CONSTR_2_0";
  case CONSTR_1_1:     return "CONSTR_1_1";
  case CONSTR_0_2:     return "CONSTR_0_2";
  case CONSTR_NOCAF:   return "CONSTR_NOCAF";
  case FUN:            return "FUN";
  case FUN_1_0:        return "FUN_1_0";
  case FUN_0_1:        return "FUN_0_1";
  case FUN_2_0:        return "FUN_2_0";
  case FUN_1_1:        return "FUN_1_1";
  case FUN_0_2:        return "FUN_0_2";
  case FUN_STATIC:     return "FUN_STATIC";
  case THUNK:          return "THUNK";
  case THUNK_1_0:      return "THUNK_1_0";
  case THUNK_0_1:      return "THUNK_0_1";
  case THUNK_2_0:      return "THUNK_2_0";
  case THUNK_1_1:      return "THUNK_1_1";
  case THUNK_0_2:      return "THUNK_0_2";
  case THUNK_STATIC:   return "THUNK_STATIC";
  case THUNK_SELECTOR: return "THUNK_SELECTOR";
  case BCO:            return "BCO";
  case AP:             return "AP";
  case PAP:            return "PAP";
  case AP_STACK:       return "AP_STACK";
  case IND:            return "IND";
  case IND_STATIC:     return "IND_STATIC";
  case BLACKHOLE:      return "BLACKHOLE";
  default:             return "UNKNOWN";
  }
}

} // namespace ghc

namespace {

/// Untag a GHC pointer (mask off low 3 bits used for pointer tagging).
uint64_t Untag(uint64_t ptr) { return ptr & ~0x7ULL; }

/// Extract the pointer tag (low 3 bits).
uint32_t GetTag(uint64_t ptr) { return static_cast<uint32_t>(ptr & 0x7); }

/// Read a word from the target process at the given address.
/// Returns true on success.
bool ReadWord(Process *process, uint64_t addr, uint64_t &out) {
  Status error;
  out = process->ReadUnsignedIntegerFromMemory(addr, 8, 0, error);
  return error.Success();
}

/// Read a half-word (4 bytes) from the target process.
bool ReadHalfWord(Process *process, uint64_t addr, uint32_t &out) {
  Status error;
  out = static_cast<uint32_t>(
      process->ReadUnsignedIntegerFromMemory(addr, 4, 0, error));
  return error.Success();
}

/// Read a null-terminated C string from the target process.
std::string ReadCString(Process *process, uint64_t addr,
                        size_t max_len = 256) {
  Status error;
  std::string result;
  process->ReadCStringFromMemory(addr, result, error);
  if (error.Fail())
    return "<unreadable>";
  return result;
}

/// Read the closure type from the info table.
///
/// With TABLES_NEXT_TO_CODE (x86_64), the info table is placed
/// immediately before the entry code. The closure's first word
/// (info_ptr) points to the entry code, and the type field is the
/// half-word at info_ptr - 8. The half-word after it, at info_ptr - 4, is
/// the SRT field (the constructor tag, for a constructor).
uint32_t ReadClosureType(Process *process, uint64_t info_ptr) {
  uint32_t val;
  if (!ReadHalfWord(process, info_ptr - 8, val))
    return ghc::INVALID_OBJECT;
  return val;
}

/// Read the constructor description string from a constructor info table.
///
/// StgConInfoTable layout (TABLES_NEXT_TO_CODE, x86_64):
///   info_ptr - 24 : con_desc (OFFSET_FIELD: 4-byte relative offset + padding)
///   info_ptr - 16 : layout
///   info_ptr - 8  : type
///   info_ptr - 4  : srt (the constructor tag)
///   info_ptr      : entry code
///
/// The con_desc field is an offset from the end of the StgConInfoTable,
/// which is info_ptr, to the constructor name string (GET_CON_DESC).
std::string ReadConDesc(Process *process, uint64_t info_ptr) {
  // Read the relative offset at info_ptr - 24
  uint32_t raw_offset;
  if (!ReadHalfWord(process, info_ptr - 24, raw_offset))
    return "<unknown>";

  // The offset is a signed 32-bit value relative to info_ptr
  int32_t offset = static_cast<int32_t>(raw_offset);
  uint64_t desc_addr = info_ptr + offset;

  return ReadCString(process, desc_addr);
}

/// Describe a closure, following indirections up to a depth limit.
void DescribeClosure(Process *process, uint64_t tagged_ptr, Stream &s,
                     int depth) {
  if (depth > 5) {
    s.PutCString("<deep indirection>");
    return;
  }

  uint64_t ptr = Untag(tagged_ptr);
  uint32_t tag = GetTag(tagged_ptr);

  if (ptr == 0) {
    s.PutCString("<null>");
    return;
  }

  // Read info pointer (first word of the closure)
  uint64_t info_ptr;
  if (!ReadWord(process, ptr, info_ptr)) {
    s.PutCString("<unreadable>");
    return;
  }

  uint32_t type = ReadClosureType(process, info_ptr);

  if (ghc::IsConstr(type)) {
    // Constructor: con_desc is the plain qualified name (`main:Main.Leaf`).
    // It isn't Z-encoded, so print it as it is.
    s.PutCString(ReadConDesc(process, info_ptr));
    if (tag > 0)
      s.Printf(" (tag=%u)", tag);
  } else if (ghc::IsThunk(type)) {
    s.PutCString("<thunk>");
  } else if (type == ghc::BLACKHOLE) {
    s.PutCString("<blackhole>");
  } else if (ghc::IsFun(type)) {
    s.PutCString("<function>");
  } else if (type == ghc::PAP) {
    s.PutCString("<partial application>");
  } else if (type == ghc::AP) {
    s.PutCString("<application>");
  } else if (ghc::IsInd(type)) {
    // Follow indirection: payload[0] is the target
    uint64_t target;
    if (ReadWord(process, ptr + 8, target)) {
      DescribeClosure(process, target, s, depth + 1);
    } else {
      s.PutCString("<indirection: unreadable>");
    }
  } else {
    s.Printf("<%s>", ghc::ClosureTypeName(type));
  }
}

} // anonymous namespace

namespace lldb_private {
namespace formatters {

bool HaskellClosureSummaryProvider(ValueObject &valobj, Stream &s,
                                   const TypeSummaryOptions &options) {
  // Get the pointer value from the ValueObject.
  // This works for pointer types (StgClosure *, etc.)
  uint64_t ptr_value = 0;
  if (valobj.IsPointerType()) {
    bool success = false;
    ptr_value = valobj.GetValueAsUnsigned(0, &success);
    if (!success || ptr_value == 0)
      return false;
  } else {
    // For non-pointer types, try to get the address
    auto addr_and_type = valobj.GetAddressOf();
    ptr_value = addr_and_type.address;
    if (ptr_value == 0 || ptr_value == LLDB_INVALID_ADDRESS)
      return false;
  }

  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  DescribeClosure(process_sp.get(), ptr_value, s, 0);
  return true;
}

} // namespace formatters
} // namespace lldb_private
