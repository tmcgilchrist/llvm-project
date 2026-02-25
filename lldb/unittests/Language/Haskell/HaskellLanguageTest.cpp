//===-- HaskellLanguageTest.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/Language/Haskell/HaskellLanguage.h"
#include "TestingSupport/SubsystemRAII.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Utility/StreamString.h"
#include "gtest/gtest.h"

using namespace lldb;
using namespace lldb_private;

namespace {
class HaskellLanguageTest : public ::testing::Test {
protected:
  SubsystemRAII<HaskellLanguage> subsystems;

  Language *GetHaskell() {
    return Language::FindPlugin(lldb::eLanguageTypeHaskell);
  }
};
} // namespace

TEST_F(HaskellLanguageTest, PluginIsRegistered) {
  Language *haskell = GetHaskell();
  ASSERT_NE(haskell, nullptr);
  EXPECT_EQ(haskell->GetPluginName(), "haskell");
  EXPECT_EQ(haskell->GetLanguageType(), eLanguageTypeHaskell);
}

TEST_F(HaskellLanguageTest, SourceFiles) {
  Language *haskell = GetHaskell();
  ASSERT_NE(haskell, nullptr);
  EXPECT_TRUE(haskell->IsSourceFile("Main.hs"));
  EXPECT_TRUE(haskell->IsSourceFile("src/Data/Map.lhs"));
  EXPECT_FALSE(haskell->IsSourceFile("rts/Schedule.c"));
  EXPECT_FALSE(haskell->IsSourceFile("rts/Updates.cmm"));
}

TEST_F(HaskellLanguageTest, SymbolNameFitsToLanguage) {
  Language *haskell = GetHaskell();
  ASSERT_NE(haskell, nullptr);

  // LLDB doesn't recognise GHC symbols as mangled, so they are stored as
  // demangled names.
  Mangled sum_tree("Main_sumTree_info");
  ASSERT_FALSE(sum_tree.GetMangledName());
  EXPECT_TRUE(haskell->SymbolNameFitsToLanguage(sum_tree));
  EXPECT_TRUE(haskell->SymbolNameFitsToLanguage(
      Mangled("ghczminternal_GHCziInternalziBase_zpzp_info")));
  EXPECT_TRUE(
      haskell->SymbolNameFitsToLanguage(Mangled("_Main_sat_s1ZZ_entry")));

  EXPECT_FALSE(haskell->SymbolNameFitsToLanguage(Mangled("main")));
  EXPECT_FALSE(haskell->SymbolNameFitsToLanguage(Mangled("mark_closure")));
  EXPECT_FALSE(
      haskell->SymbolNameFitsToLanguage(Mangled("stg_upd_frame_info")));
  EXPECT_FALSE(haskell->SymbolNameFitsToLanguage(Mangled("_ZN4llvm3fooEv")));
  EXPECT_FALSE(haskell->SymbolNameFitsToLanguage(Mangled()));
}

TEST_F(HaskellLanguageTest, GuessLanguage) {
  EXPECT_EQ(Mangled("Main_sumTree_info").GuessLanguage(), eLanguageTypeHaskell);
  EXPECT_EQ(Mangled("mark_closure").GuessLanguage(), eLanguageTypeUnknown);
}

TEST_F(HaskellLanguageTest, SymbolStoredAsMangled) {
  // An internal symbol in a module whose name starts with Z starts with _Z,
  // so Mangled takes it for an Itanium name.
  Mangled mangled("_ZZoo_sat_s1_entry");
  ASSERT_TRUE(mangled.GetMangledName());
  EXPECT_EQ(mangled.GuessLanguage(), eLanguageTypeHaskell);
  EXPECT_EQ(mangled.GetDisplayDemangledName(), "Zoo.sat_s1");
}

TEST_F(HaskellLanguageTest, Names) {
  Mangled info("Main_sumTree_info");
  EXPECT_EQ(info.GetDisplayDemangledName(), "Main.sumTree");
  EXPECT_EQ(info.GetName(Mangled::ePreferDemangledWithoutArguments),
            "Main.sumTree");
  // The symbol table keeps the raw name.
  EXPECT_EQ(info.GetName(), "Main_sumTree_info");

  Mangled closure("Main_sumTree_closure");
  EXPECT_EQ(closure.GetDisplayDemangledName(), "Main.sumTree (closure)");
  EXPECT_EQ(closure.GetName(Mangled::ePreferDemangledWithoutArguments),
            "Main.sumTree");

  Mangled con("Main_Leaf_con_info");
  EXPECT_EQ(con.GetDisplayDemangledName(), "Main.Leaf (con_info)");

  // Names the plugin doesn't claim are left alone.
  Language *haskell = GetHaskell();
  ASSERT_NE(haskell, nullptr);
  EXPECT_EQ(haskell->GetDisplayDemangledName(Mangled("stg_ap_v_info")),
            "stg_ap_v_info");
  EXPECT_EQ(haskell->GetDemangledFunctionNameWithoutArguments(
                Mangled("stg_ap_v_info")),
            "stg_ap_v_info");
}

TEST_F(HaskellLanguageTest, FunctionDisplayName) {
  Language *haskell = GetHaskell();
  ASSERT_NE(haskell, nullptr);

  auto display = [&](llvm::StringRef name) -> std::optional<std::string> {
    Symbol symbol(/*symID=*/0, name, eSymbolTypeCode, /*external=*/true,
                  /*is_debug=*/false, /*is_trampoline=*/false,
                  /*is_artificial=*/false, /*section_sp=*/nullptr,
                  /*value=*/0, /*size=*/0, /*size_is_valid=*/false,
                  /*contains_linker_annotations=*/false, /*flags=*/0);
    SymbolContext sc;
    sc.symbol = &symbol;
    StreamString s;
    if (!haskell->GetFunctionDisplayName(
            sc, nullptr, Language::FunctionNameRepresentation::eName, s))
      return std::nullopt;
    return s.GetString().str();
  };

  EXPECT_EQ(display("Main_sumTree_info"), "Main.sumTree");
  EXPECT_EQ(display("Main_zdwmkTree_info"), "Main.$wmkTree");
  EXPECT_EQ(display("_Main_sat_s1ZZ_entry"), "Main.sat_s1ZZ");
  EXPECT_EQ(display("stg_upd_frame_info"), std::nullopt);
  EXPECT_EQ(display("main"), std::nullopt);

  SymbolContext empty;
  StreamString s;
  EXPECT_FALSE(haskell->GetFunctionDisplayName(
      empty, nullptr, Language::FunctionNameRepresentation::eName, s));
}
