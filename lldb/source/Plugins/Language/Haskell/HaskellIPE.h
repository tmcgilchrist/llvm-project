//===-- HaskellIPE.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Reader for GHC's Info Table Provenance Entries (IPE).
//
// When a Haskell program is compiled with -finfo-table-map, GHC emits
// an IpeBufferListNode per module containing provenance data for each
// info table: type description, source file, source span, etc.
//
// At program startup, each module's node is registered into a global
// linked list (ipeBufferList). This module walks that list in the
// target process's memory to look up provenance for a given info table
// pointer.
//
// IpeBufferListNode has two layouts (x86_64 offsets, from rts/IPE.h):
//
//   Plain (GHC 9.12 and 9.14)       MagicWords (GHC HEAD since July 2025)
//   +0   next                       +0   next
//                                   +8   node_id (4 bytes, then padding)
//   +8   compressed                 +16  compressed
//   +16  count                      +24  count
//   +24  tables                     +32  tables
//   +32  entries                    +40  entries_block
//   +40  entries_size               +48  entries_size
//   +48  string_table               +56  string_table_block
//   +56  string_table_size          +64  string_table_size
//   +64  unit_id (StringIdx)        +72  unit_id (StringIdx)
//   +68  module_name (StringIdx)    +76  module_name (StringIdx)
//
// In the Plain layout `entries` and `string_table` point straight at the
// data. In the MagicWords layout (GHC commits c80dd91c0b and cab4266619) the
// blocks they point to start with an 8-byte IPE_MAGIC_WORD.
//
// tables[i] is the info table that entries[i] describes. With
// TABLES_NEXT_TO_CODE it is the entry code address, which is also the
// closure's info pointer.
//
// IpeBufferEntry layout (24 bytes, the same in both):
//   +0   table_name    (4 bytes, StringIdx)
//   +4   closure_desc  (4 bytes, uint32_t)
//   +8   ty_desc       (4 bytes, StringIdx)
//   +12  label         (4 bytes, StringIdx)
//   +16  src_file      (4 bytes, StringIdx)
//   +20  src_span      (4 bytes, StringIdx)
//
// A StringIdx is a byte offset into the module's string table.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_HASKELL_HASKELLIPE_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_HASKELL_HASKELLIPE_H

#include <cstdint>
#include <optional>
#include <string>

namespace lldb_private {
class Process;
}

namespace ghc {
namespace ipe {

/// Provenance information for a single info table.
struct InfoProv {
  std::string table_name;
  uint32_t closure_desc = 0;
  std::string ty_desc;
  std::string label;
  std::string unit_id;
  std::string module_name;
  std::string src_file;
  std::string src_span;
};

/// The IpeBufferListNode layouts described above.
enum class NodeLayout {
  /// GHC 9.12 and 9.14: no node_id, and no magic words.
  Plain,
  /// GHC HEAD: a node_id after next, and magic words in front of the
  /// entries and the string table.
  MagicWords,
};

/// Reads target memory for the IPE reader, so that it can be tested
/// without a process. Values are in target byte order.
class MemoryReader {
public:
  virtual ~MemoryReader() = default;

  /// Reads an unsigned integer of \p size bytes (4 or 8) at \p addr.
  virtual std::optional<uint64_t> ReadUnsigned(uint64_t addr,
                                               unsigned size) = 0;

  /// Reads a NUL-terminated string at \p addr.
  virtual std::optional<std::string> ReadCString(uint64_t addr) = 0;
};

/// Works out which layout the node at \p node_addr uses. Returns nullopt if
/// it is consistent with neither: a MagicWords node must have the magic word
/// at the start of both blocks, and a Plain node must have an entries_size
/// of count * sizeof(IpeBufferEntry).
std::optional<NodeLayout> DetectNodeLayout(MemoryReader &memory,
                                           uint64_t node_addr);

/// Looks up provenance for \p info_ptr in the list of nodes that starts at
/// \p node_addr. Nodes in neither layout, and compressed nodes, are skipped.
///
/// Returns true if provenance was found, filling in `out`.
bool LookupIPEInList(MemoryReader &memory, uint64_t node_addr,
                     uint64_t info_ptr, InfoProv &out);

/// Look up IPE data for an info table pointer by walking the
/// ipeBufferList in the target process.
///
/// Returns true if provenance was found, filling in `out`.
bool LookupIPE(lldb_private::Process *process, uint64_t info_ptr,
               InfoProv &out);

} // namespace ipe
} // namespace ghc

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_HASKELL_HASKELLIPE_H
