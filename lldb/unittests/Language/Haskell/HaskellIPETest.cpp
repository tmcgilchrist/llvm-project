//===-- HaskellIPETest.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/Language/Haskell/HaskellIPE.h"

#include "llvm/ADT/StringRef.h"
#include "gtest/gtest.h"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace ghc::ipe;

namespace {

constexpr uint64_t kMagicWord = 0x4950450049504500ULL;

/// Little-endian memory, byte by byte. Reads of bytes that were never
/// written fail, as reads of unmapped memory do.
class FakeMemory : public MemoryReader {
public:
  void Write(uint64_t addr, uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i)
      m_bytes[addr + i] = static_cast<uint8_t>(value >> (8 * i));
  }

  void WriteBytes(uint64_t addr, llvm::StringRef bytes) {
    for (size_t i = 0; i < bytes.size(); ++i)
      m_bytes[addr + i] = static_cast<uint8_t>(bytes[i]);
  }

  std::optional<uint64_t> ReadUnsigned(uint64_t addr, unsigned size) override {
    uint64_t value = 0;
    for (unsigned i = 0; i < size; ++i) {
      auto it = m_bytes.find(addr + i);
      if (it == m_bytes.end())
        return std::nullopt;
      value |= static_cast<uint64_t>(it->second) << (8 * i);
    }
    return value;
  }

  std::optional<std::string> ReadCString(uint64_t addr) override {
    std::string result;
    for (;; ++addr) {
      auto it = m_bytes.find(addr);
      if (it == m_bytes.end())
        return std::nullopt;
      if (it->second == 0)
        return result;
      result.push_back(static_cast<char>(it->second));
    }
  }

private:
  std::map<uint64_t, uint8_t> m_bytes;
};

/// A string table, built up as GHC lays it out: NUL-terminated strings
/// addressed by byte offset.
class StringTable {
public:
  uint32_t Add(llvm::StringRef str) {
    uint32_t idx = m_data.size();
    m_data += str;
    m_data.push_back('\0');
    return idx;
  }
  const std::string &GetData() const { return m_data; }

private:
  std::string m_data;
};

struct TestEntry {
  uint64_t info_ptr;
  uint32_t closure_desc;
  uint32_t table_name, ty_desc, label, src_file, src_span;
};

struct TestNode {
  uint64_t next = 0;
  uint64_t compressed = 0;
  uint32_t unit_id = 0, module_name = 0;
  StringTable strings;
  std::vector<TestEntry> entries;
};

/// Writes `node` at `addr` in `layout`, with its tables, entries and string
/// table in the 4 KiB after it.
void WriteNode(FakeMemory &memory, NodeLayout layout, uint64_t addr,
               const TestNode &node) {
  const bool magic = layout == NodeLayout::MagicWords;
  const uint64_t tables = addr + 0x100;
  const uint64_t entries = addr + 0x400;
  const uint64_t string_table = addr + 0x800;
  const uint64_t header = magic ? 8 : 0;
  const uint64_t count = node.entries.size();

  // The fields after `next`, which the MagicWords layout moves down by 8.
  const uint64_t base = addr + (magic ? 16 : 8);
  memory.Write(addr, node.next, 8);
  if (magic)
    memory.Write(addr + 8, /*node_id=*/1, 8);
  memory.Write(base + 0, node.compressed, 8);
  memory.Write(base + 8, count, 8);
  memory.Write(base + 16, tables, 8);
  memory.Write(base + 24, entries, 8);
  memory.Write(base + 32, count * 24, 8);
  memory.Write(base + 40, string_table, 8);
  memory.Write(base + 48, node.strings.GetData().size(), 8);
  memory.Write(base + 56, node.unit_id, 4);
  memory.Write(base + 60, node.module_name, 4);

  if (magic) {
    memory.Write(entries, kMagicWord, 8);
    memory.Write(string_table, kMagicWord, 8);
  }
  for (uint64_t i = 0; i < count; ++i) {
    const TestEntry &entry = node.entries[i];
    memory.Write(tables + i * 8, entry.info_ptr, 8);
    uint64_t entry_addr = entries + header + i * 24;
    memory.Write(entry_addr + 0, entry.table_name, 4);
    memory.Write(entry_addr + 4, entry.closure_desc, 4);
    memory.Write(entry_addr + 8, entry.ty_desc, 4);
    memory.Write(entry_addr + 12, entry.label, 4);
    memory.Write(entry_addr + 16, entry.src_file, 4);
    memory.Write(entry_addr + 20, entry.src_span, 4);
  }
  memory.WriteBytes(string_table + header, node.strings.GetData());
}

/// A node for module Main of unit main, with entries for a constructor and
/// a thunk in tree.hs. The values are illustrative: closure_desc 2 is
/// CONSTR_1_0 and 15 is THUNK.
TestNode MakeMainNode() {
  TestNode node;
  node.module_name = node.strings.Add("Main");
  node.unit_id = node.strings.Add("main");
  uint32_t empty = node.strings.Add("");
  uint32_t src_file = node.strings.Add("tree.hs");
  node.entries.push_back({/*info_ptr=*/0x408e38, /*closure_desc=*/2,
                          node.strings.Add("Leaf_Main_0_con_info"),
                          node.strings.Add("Tree"), empty, src_file,
                          node.strings.Add("6:12-18")});
  node.entries.push_back({/*info_ptr=*/0x408e58, /*closure_desc=*/15,
                          node.strings.Add("sat_s1_info"),
                          node.strings.Add("Int"), node.strings.Add("sumTree"),
                          src_file, node.strings.Add("11:22-42")});
  return node;
}

class HaskellIPELayoutTest : public testing::TestWithParam<NodeLayout> {};

} // namespace

TEST_P(HaskellIPELayoutTest, DetectsLayout) {
  FakeMemory memory;
  WriteNode(memory, GetParam(), 0x1000, MakeMainNode());
  EXPECT_EQ(DetectNodeLayout(memory, 0x1000), GetParam());
}

TEST_P(HaskellIPELayoutTest, FindsEntry) {
  FakeMemory memory;
  WriteNode(memory, GetParam(), 0x1000, MakeMainNode());

  InfoProv prov;
  ASSERT_TRUE(LookupIPEInList(memory, 0x1000, 0x408e58, prov));
  EXPECT_EQ(prov.table_name, "sat_s1_info");
  EXPECT_EQ(prov.closure_desc, 15u);
  EXPECT_EQ(prov.ty_desc, "Int");
  EXPECT_EQ(prov.label, "sumTree");
  EXPECT_EQ(prov.unit_id, "main");
  EXPECT_EQ(prov.module_name, "Main");
  EXPECT_EQ(prov.src_file, "tree.hs");
  EXPECT_EQ(prov.src_span, "11:22-42");

  ASSERT_TRUE(LookupIPEInList(memory, 0x1000, 0x408e38, prov));
  EXPECT_EQ(prov.table_name, "Leaf_Main_0_con_info");
  EXPECT_EQ(prov.ty_desc, "Tree");
  EXPECT_EQ(prov.label, "");
}

TEST_P(HaskellIPELayoutTest, MissingInfoTable) {
  FakeMemory memory;
  WriteNode(memory, GetParam(), 0x1000, MakeMainNode());
  InfoProv prov;
  EXPECT_FALSE(LookupIPEInList(memory, 0x1000, 0x408e40, prov));
}

TEST_P(HaskellIPELayoutTest, WalksList) {
  FakeMemory memory;
  TestNode first = MakeMainNode();
  first.entries.resize(1);
  first.next = 0x8000;
  TestNode second;
  second.module_name = second.strings.Add("Lib");
  second.unit_id = second.strings.Add("main");
  uint32_t str = second.strings.Add("x");
  second.entries.push_back({0x409000, 3, str, str, str, str, str});
  WriteNode(memory, GetParam(), 0x1000, first);
  WriteNode(memory, GetParam(), 0x8000, second);

  InfoProv prov;
  ASSERT_TRUE(LookupIPEInList(memory, 0x1000, 0x409000, prov));
  EXPECT_EQ(prov.module_name, "Lib");
  ASSERT_TRUE(LookupIPEInList(memory, 0x1000, 0x408e38, prov));
  EXPECT_EQ(prov.module_name, "Main");
}

TEST_P(HaskellIPELayoutTest, SkipsCompressedNodes) {
  FakeMemory memory;
  TestNode node = MakeMainNode();
  node.compressed = 1;
  WriteNode(memory, GetParam(), 0x1000, node);
  EXPECT_EQ(DetectNodeLayout(memory, 0x1000), GetParam());
  InfoProv prov;
  EXPECT_FALSE(LookupIPEInList(memory, 0x1000, 0x408e58, prov));
}

TEST_P(HaskellIPELayoutTest, RejectsStringIndexOutsideTable) {
  FakeMemory memory;
  TestNode node = MakeMainNode();
  node.entries[1].ty_desc = node.strings.GetData().size();
  WriteNode(memory, GetParam(), 0x1000, node);
  InfoProv prov;
  EXPECT_FALSE(LookupIPEInList(memory, 0x1000, 0x408e58, prov));
}

TEST_P(HaskellIPELayoutTest, StopsOnCycle) {
  FakeMemory memory;
  TestNode node = MakeMainNode();
  node.next = 0x1000;
  WriteNode(memory, GetParam(), 0x1000, node);
  InfoProv prov;
  EXPECT_FALSE(LookupIPEInList(memory, 0x1000, 0x409000, prov));
}

INSTANTIATE_TEST_SUITE_P(Layouts, HaskellIPELayoutTest,
                         testing::Values(NodeLayout::Plain,
                                         NodeLayout::MagicWords),
                         [](const testing::TestParamInfo<NodeLayout> &info) {
                           return info.param == NodeLayout::Plain
                                      ? "Plain"
                                      : "MagicWords";
                         });

// The node headers of Main in tree.hs, read from the running program
// (`-g -O1 -finfo-table-map -fdistinct-constructor-tables`).
TEST(HaskellIPETest, MeasuredGHC9122Node) {
  FakeMemory memory;
  const uint64_t node = 0x4c80f0;
  for (auto [offset, value] : std::vector<std::pair<uint64_t, uint64_t>>{
           {0, 0},         // next
           {8, 0},         // compressed
           {16, 0x29},     // count
           {24, 0x4c7748}, // tables
           {32, 0x4c7d10}, // entries
           {40, 0x3d8},    // entries_size
           {48, 0x4c7890}, // string_table
           {56, 0x478},    // string_table_size
           {64, 0x5},      // unit_id, module_name
       })
    memory.Write(node + offset, value, 8);
  // The first entry and the start of the string table.
  memory.Write(0x4c7d10, 0x000000030000000a, 8);
  memory.WriteBytes(0x4c7890, llvm::StringRef("Main\0main\0", 10));
  EXPECT_EQ(DetectNodeLayout(memory, node), NodeLayout::Plain);
}

TEST(HaskellIPETest, MeasuredGHCHeadNode) {
  FakeMemory memory;
  const uint64_t node = 0x8cdc18;
  for (auto [offset, value] : std::vector<std::pair<uint64_t, uint64_t>>{
           {0, 0},         // next
           {8, 1},         // node_id
           {16, 0},        // compressed
           {24, 0x29},     // count
           {32, 0x8cdad0}, // tables
           {40, 0x8bd858}, // entries_block
           {48, 0x3d8},    // entries_size
           {56, 0x8bd3b8}, // string_table_block
           {64, 0x492},    // string_table_size
           {72, 0x5},      // unit_id, module_name
       })
    memory.Write(node + offset, value, 8);
  memory.Write(0x8bd858, kMagicWord, 8);
  memory.Write(0x8bd3b8, kMagicWord, 8);
  EXPECT_EQ(DetectNodeLayout(memory, node), NodeLayout::MagicWords);
}

TEST(HaskellIPETest, RejectsCorruptMagicWord) {
  FakeMemory memory;
  WriteNode(memory, NodeLayout::MagicWords, 0x1000, MakeMainNode());
  memory.Write(0x1000 + 0x800, 0, 8); // the string table's magic word
  EXPECT_EQ(DetectNodeLayout(memory, 0x1000), std::nullopt);
  InfoProv prov;
  EXPECT_FALSE(LookupIPEInList(memory, 0x1000, 0x408e58, prov));
}

TEST(HaskellIPETest, RejectsInconsistentPlainNode) {
  FakeMemory memory;
  WriteNode(memory, NodeLayout::Plain, 0x1000, MakeMainNode());
  memory.Write(0x1000 + 40, 2 * 24 + 1, 8); // entries_size
  EXPECT_EQ(DetectNodeLayout(memory, 0x1000), std::nullopt);
}

TEST(HaskellIPETest, UnreadableMemory) {
  FakeMemory memory;
  EXPECT_EQ(DetectNodeLayout(memory, 0x1000), std::nullopt);
  InfoProv prov;
  EXPECT_FALSE(LookupIPEInList(memory, 0x1000, 0x408e58, prov));
  EXPECT_FALSE(LookupIPEInList(memory, 0, 0x408e58, prov));
}
