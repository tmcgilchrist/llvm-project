//===-- HaskellIPE.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaskellIPE.h"

#include "lldb/Core/Module.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Status.h"

using namespace lldb;
using namespace lldb_private;
using namespace ghc::ipe;

namespace {

/// Field offsets in an IpeBufferListNode (64-bit targets). `next` is at
/// offset 0 in both layouts.
struct NodeOffsets {
  uint64_t compressed;
  uint64_t count;
  uint64_t tables;
  uint64_t entries;
  uint64_t entries_size;
  uint64_t string_table;
  uint64_t string_table_size;
  uint64_t unit_id;
  uint64_t module_name;
  /// Bytes in front of the entries and the string table (the magic word).
  uint64_t block_header;
};

constexpr uint64_t kNodeNextOffset = 0;

constexpr NodeOffsets kPlainOffsets = {
    /*compressed=*/8,
    /*count=*/16,
    /*tables=*/24,
    /*entries=*/32,
    /*entries_size=*/40,
    /*string_table=*/48,
    /*string_table_size=*/56,
    /*unit_id=*/64,
    /*module_name=*/68,
    /*block_header=*/0,
};

constexpr NodeOffsets kMagicWordsOffsets = {
    /*compressed=*/16,
    /*count=*/24,
    /*tables=*/32,
    /*entries=*/40,
    /*entries_size=*/48,
    /*string_table=*/56,
    /*string_table_size=*/64,
    /*unit_id=*/72,
    /*module_name=*/76,
    /*block_header=*/8,
};

const NodeOffsets &GetOffsets(NodeLayout layout) {
  return layout == NodeLayout::MagicWords ? kMagicWordsOffsets : kPlainOffsets;
}

// IpeBufferEntry: 24 bytes each
constexpr uint64_t kEntrySize = 24;
constexpr uint64_t kEntryTableNameOffset = 0;
constexpr uint64_t kEntryClosureDescOffset = 4;
constexpr uint64_t kEntryTyDescOffset = 8;
constexpr uint64_t kEntryLabelOffset = 12;
constexpr uint64_t kEntrySrcFileOffset = 16;
constexpr uint64_t kEntrySrcSpanOffset = 20;

// IPE_MAGIC_WORD, "IPE\0IPE\0"
constexpr uint64_t kIPEMagicWord = 0x4950450049504500ULL;

/// Reads the memory of a live process.
class ProcessMemoryReader : public MemoryReader {
public:
  explicit ProcessMemoryReader(Process &process) : m_process(process) {}

  std::optional<uint64_t> ReadUnsigned(uint64_t addr, unsigned size) override {
    Status error;
    uint64_t value =
        m_process.ReadUnsignedIntegerFromMemory(addr, size, 0, error);
    if (error.Fail())
      return std::nullopt;
    return value;
  }

  std::optional<std::string> ReadCString(uint64_t addr) override {
    Status error;
    std::string result;
    m_process.ReadCStringFromMemory(addr, result, error);
    if (error.Fail())
      return std::nullopt;
    return result;
  }

private:
  Process &m_process;
};

/// Find the address of the global `ipeBufferList` symbol in the target.
uint64_t FindIPEBufferList(Process *process) {
  Target &target = process->GetTarget();
  ConstString symbol_name("ipeBufferList");

  for (const ModuleSP &module_sp : target.GetImages().Modules()) {
    const Symbol *sym = module_sp->FindFirstSymbolWithNameAndType(
        symbol_name, eSymbolTypeData);
    if (sym) {
      Address addr = sym->GetAddress();
      return addr.GetLoadAddress(&target);
    }
  }
  return LLDB_INVALID_ADDRESS;
}

/// Returns true if the pointer at \p field_addr points at the magic word.
bool PointsAtMagicWord(MemoryReader &memory, uint64_t field_addr) {
  std::optional<uint64_t> block = memory.ReadUnsigned(field_addr, 8);
  if (!block || *block == 0)
    return false;
  std::optional<uint64_t> magic = memory.ReadUnsigned(*block, 8);
  return magic && *magic == kIPEMagicWord;
}

/// Search a single IpeBufferListNode for the given info_ptr.
/// Returns true if found, filling in `out`.
bool SearchNode(MemoryReader &memory, uint64_t node_addr, NodeLayout layout,
                uint64_t info_ptr, InfoProv &out) {
  const NodeOffsets &offsets = GetOffsets(layout);
  auto read_word = [&](uint64_t offset) {
    return memory.ReadUnsigned(node_addr + offset, 8);
  };

  // Check if data is compressed — we can't decompress in the debugger
  std::optional<uint64_t> compressed = read_word(offsets.compressed);
  if (!compressed || *compressed != 0)
    return false;

  std::optional<uint64_t> count = read_word(offsets.count);
  std::optional<uint64_t> tables = read_word(offsets.tables);
  if (!count || !tables || *count == 0)
    return false;

  // Scan the tables array for our info_ptr.
  std::optional<uint64_t> index;
  for (uint64_t i = 0; i < *count; ++i) {
    std::optional<uint64_t> table = memory.ReadUnsigned(*tables + i * 8, 8);
    if (!table)
      return false;
    if (*table == info_ptr) {
      index = i;
      break;
    }
  }
  if (!index)
    return false;

  std::optional<uint64_t> entries = read_word(offsets.entries);
  std::optional<uint64_t> string_table = read_word(offsets.string_table);
  std::optional<uint64_t> string_table_size =
      read_word(offsets.string_table_size);
  if (!entries || !string_table || !string_table_size)
    return false;

  uint64_t entry_addr = *entries + offsets.block_header + *index * kEntrySize;
  uint64_t string_table_base = *string_table + offsets.block_header;

  auto read_u32 = [&](uint64_t addr) -> std::optional<uint32_t> {
    if (std::optional<uint64_t> value = memory.ReadUnsigned(addr, 4))
      return static_cast<uint32_t>(*value);
    return std::nullopt;
  };
  // A StringIdx outside the string table means we are reading something
  // that isn't an IPE node.
  auto read_string = [&](std::optional<uint32_t> idx,
                         std::string &result) -> bool {
    if (!idx || *idx >= *string_table_size)
      return false;
    std::optional<std::string> str =
        memory.ReadCString(string_table_base + *idx);
    if (!str)
      return false;
    result = std::move(*str);
    return true;
  };

  std::optional<uint32_t> closure_desc =
      read_u32(entry_addr + kEntryClosureDescOffset);
  if (!closure_desc)
    return false;

  InfoProv prov;
  prov.closure_desc = *closure_desc;
  if (!read_string(read_u32(entry_addr + kEntryTableNameOffset),
                   prov.table_name) ||
      !read_string(read_u32(entry_addr + kEntryTyDescOffset), prov.ty_desc) ||
      !read_string(read_u32(entry_addr + kEntryLabelOffset), prov.label) ||
      !read_string(read_u32(entry_addr + kEntrySrcFileOffset), prov.src_file) ||
      !read_string(read_u32(entry_addr + kEntrySrcSpanOffset), prov.src_span) ||
      // unit_id and module_name are shared by every entry in the node.
      !read_string(read_u32(node_addr + offsets.unit_id), prov.unit_id) ||
      !read_string(read_u32(node_addr + offsets.module_name), prov.module_name))
    return false;

  out = std::move(prov);
  return true;
}

} // anonymous namespace

namespace ghc {
namespace ipe {

std::optional<NodeLayout> DetectNodeLayout(MemoryReader &memory,
                                           uint64_t node_addr) {
  if (PointsAtMagicWord(memory, node_addr + kMagicWordsOffsets.entries) &&
      PointsAtMagicWord(memory, node_addr + kMagicWordsOffsets.string_table))
    return NodeLayout::MagicWords;

  // The Plain layout has no magic word. entries_size is the decompressed
  // size, so it matches count even for a compressed node.
  std::optional<uint64_t> compressed =
      memory.ReadUnsigned(node_addr + kPlainOffsets.compressed, 8);
  std::optional<uint64_t> count =
      memory.ReadUnsigned(node_addr + kPlainOffsets.count, 8);
  std::optional<uint64_t> entries_size =
      memory.ReadUnsigned(node_addr + kPlainOffsets.entries_size, 8);
  if (compressed && *compressed <= 1 && count && entries_size &&
      *entries_size % kEntrySize == 0 && *entries_size / kEntrySize == *count)
    return NodeLayout::Plain;

  return std::nullopt;
}

bool LookupIPEInList(MemoryReader &memory, uint64_t node_addr,
                     uint64_t info_ptr, InfoProv &out) {
  // Walk the linked list
  constexpr int kMaxNodes = 10000; // safety limit
  for (int visited = 0; node_addr != 0 && visited < kMaxNodes; ++visited) {
    std::optional<NodeLayout> layout = DetectNodeLayout(memory, node_addr);
    if (layout && SearchNode(memory, node_addr, *layout, info_ptr, out))
      return true;

    // Follow next pointer
    std::optional<uint64_t> next =
        memory.ReadUnsigned(node_addr + kNodeNextOffset, 8);
    if (!next)
      return false;
    node_addr = *next;
  }
  return false;
}

bool LookupIPE(Process *process, uint64_t info_ptr, InfoProv &out) {
  // Both layouts are 64-bit.
  if (!process || info_ptr == 0 || process->GetAddressByteSize() != 8)
    return false;

  // Find the ipeBufferList global
  uint64_t list_sym_addr = FindIPEBufferList(process);
  if (list_sym_addr == LLDB_INVALID_ADDRESS)
    return false;

  ProcessMemoryReader memory(*process);
  // Read the head pointer
  std::optional<uint64_t> head = memory.ReadUnsigned(list_sym_addr, 8);
  if (!head)
    return false;
  return LookupIPEInList(memory, *head, info_ptr, out);
}

} // namespace ipe
} // namespace ghc
