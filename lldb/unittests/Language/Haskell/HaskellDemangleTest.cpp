//===-- HaskellDemangleTest.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/Language/Haskell/HaskellDemangle.h"
#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::haskell;

namespace {
struct ZDecodeCase {
  const char *encoded;
  const char *decoded;
};
} // namespace

TEST(HaskellDemangle, ZDecodeValid) {
  ZDecodeCase cases[] = {
      {"", ""},
      {"Main", "Main"},
      {"sumTree", "sumTree"},
      {"GHCziInternalziBase", "GHC.Internal.Base"},
      {"zpzp", "++"},
      {"zdwmkTree", "$wmkTree"},
      {"ZZoo", "Zoo"},
      {"zz", "z"},
      // Upper-case escapes.
      {"ZLZRZMZNZCZZ", "()[]:Z"},
      // Lower-case escapes.
      {"zazbzczdzezgzhzizlzmznzpzqzrzsztzuzvzz", "&|^$=>#.<-!+'\\/*_%z"},
      // Tuples, see decode_tuple in GHC.Utils.Encoding.
      {"Z0T", "()"},
      {"Z2T", "(,)"},
      {"Z3T", "(,,)"},
      {"Z10T", "(,,,,,,,,,)"},
      {"Z0H", "(##)"},
      {"Z1H", "(# #)"},
      {"Z2H", "(#,#)"},
      {"ZLzhz20UzhZR", "(# #)"},
      // Any other character is z<hex>U, with the hex starting with a digit.
      {"z3bbU", "\xce\xbb"},           // U+03BB GREEK SMALL LETTER LAMDA
      {"z0e9U", "\xc3\xa9"},           // U+00E9 LATIN SMALL LETTER E WITH ACUTE
      {"z1f600U", "\xf0\x9f\x98\x80"}, // U+1F600 GRINNING FACE
      // A leading digit is always escaped.
      {"z39UpH", "9pH"},
      // Unit ids.
      {"ghczminternal", "ghc-internal"},
      {"basezm4zi21zi0zi0zmae91", "base-4.21.0.0-ae91"},
      // Mangled record field names.
      {"zdfldZCFloatingDecimalZCdmantissa", "$fld:FloatingDecimal:dmantissa"},
  };
  for (const ZDecodeCase &c : cases) {
    SCOPED_TRACE(c.encoded);
    std::optional<std::string> decoded = ZDecode(c.encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, c.decoded);
  }
}

TEST(HaskellDemangle, ZDecodeInvalid) {
  const char *cases[] = {
      "z",        // Truncated escapes.
      "Z",        //
      "abcz",     //
      "zf",       // Unknown escapes.
      "zx",       //
      "Zx",       //
      "ZA",       //
      "z12",      // Unterminated code point.
      "z12X",     //
      "z110000U", // Beyond U+10FFFF.
      "Z2",       // Unterminated tuple.
      "Z2X",      //
      "a_b",      // Characters GHC always encodes.
      "a.b",      //
      "a$b",      //
      "a-b",      //
  };
  for (const char *c : cases) {
    SCOPED_TRACE(c);
    EXPECT_FALSE(ZDecode(c).has_value());
  }
}

namespace {
struct SymbolCase {
  const char *symbol;
  GHCSymbolKind kind;
  const char *unit;
  const char *module;
  const char *name;
  const char *display;
};
} // namespace

TEST(HaskellDemangle, ParseIdLabels) {
  using K = GHCSymbolKind;
  SymbolCase cases[] = {
      // The main unit has no unit prefix.
      {"Main_sumTree_info", K::Info, "", "Main", "sumTree", "Main.sumTree"},
      {"Main_sumTree_entry", K::Entry, "", "Main", "sumTree", "Main.sumTree"},
      {"Main_sumTree_closure", K::Closure, "", "Main", "sumTree",
       "Main.sumTree (closure)"},
      {"Main_zdwmkTree_info", K::Info, "", "Main", "$wmkTree", "Main.$wmkTree"},
      {"Main_zdtrModule2_bytes", K::Bytes, "", "Main", "$trModule2",
       "Main.$trModule2 (bytes)"},
      {"Main_foo_ct", K::TickyCounter, "", "Main", "foo",
       "Main.foo (ticky counter)"},
      // The constructor function and the constructor's info table.
      {"Main_Leaf_info", K::Info, "", "Main", "Leaf", "Main.Leaf"},
      {"Main_Leaf_con_info", K::ConInfo, "", "Main", "Leaf",
       "Main.Leaf (con_info)"},
      {"Main_Leaf_con_entry", K::ConEntry, "", "Main", "Leaf",
       "Main.Leaf (con_entry)"},
      // The real entry point, :Main.main.
      {"ZCMain_main_info", K::Info, "", ":Main", "main", ":Main.main"},
      {"ZCMain_main_closure", K::Closure, "", ":Main", "main",
       ":Main.main (closure)"},
      // Wired-in units have no version.
      {"ghczminternal_GHCziInternalziBase_zpzp_info", K::Info, "ghc-internal",
       "GHC.Internal.Base", "++", "GHC.Internal.Base.++"},
      {"ghczmprim_GHCziTypes_ZC_con_info", K::ConInfo, "ghc-prim", "GHC.Types",
       ":", "GHC.Types.: (con_info)"},
      {"ghczmprim_GHCziTypes_ZMZN_closure", K::Closure, "ghc-prim", "GHC.Types",
       "[]", "GHC.Types.[] (closure)"},
      {"ghczmprim_GHCziTuple_Z2T_con_info", K::ConInfo, "ghc-prim", "GHC.Tuple",
       "(,)", "GHC.Tuple.(,) (con_info)"},
      {"ghczmbignum_GHCziNumziPrimitives_shiftRWzh_info", K::Info, "ghc-bignum",
       "GHC.Num.Primitives", "shiftRW#", "GHC.Num.Primitives.shiftRW#"},
      // Other units carry a version and a hash.
      {"basezm4zi21zi0zi0zmae91_ControlziConcurrentziChan_Chan_con_info",
       K::ConInfo, "base-4.21.0.0-ae91", "Control.Concurrent.Chan", "Chan",
       "Control.Concurrent.Chan.Chan (con_info)"},
      {"textzm2zi1zi2zmecdb_DataziText_zdwzdmZCzg_slow", K::Slow,
       "text-2.1.2-ecdb", "Data.Text", "$w$m:>",
       "Data.Text.$w$m:> (slow entry)"},
      {"containerszm0zi7zm5a8f_DataziSetziInternal_takezuzdszdwpolyzugo_slow",
       K::Slow, "containers-0.7-5a8f", "Data.Set.Internal", "take_$s$wpoly_go",
       "Data.Set.Internal.take_$s$wpoly_go (slow entry)"},
      {"ghczminternal_GHCziInternalziIOziDevice_SeekMode_closure_tbl",
       K::ClosureTable, "ghc-internal", "GHC.Internal.IO.Device", "SeekMode",
       "GHC.Internal.IO.Device.SeekMode (closure_tbl)"},
      // Instance methods, defaults and record fields.
      {"basezm4zi21zi0zi0zmae91_DataziFunctorziClasses_"
       "zdfEq1ComplexzuzdcliftEq_closure",
       K::Closure, "base-4.21.0.0-ae91", "Data.Functor.Classes",
       "$fEq1Complex_$cliftEq",
       "Data.Functor.Classes.$fEq1Complex_$cliftEq (closure)"},
      {"ghczminternal_GHCziInternalziDataziTypeableziInternal_"
       "zdfldZCTrTypeZCtrFunMul_info",
       K::Info, "ghc-internal", "GHC.Internal.Data.Typeable.Internal",
       "$fld:TrType:trFunMul",
       "GHC.Internal.Data.Typeable.Internal.$fld:TrType:trFunMul"},
      // Unicode, and a unit name that starts with a digit.
      {"Main_z3bbU_info", K::Info, "", "Main", "\xce\xbb", "Main.\xce\xbb"},
      {"z39UpH_Foo_bar_info", K::Info, "9pH", "Foo", "bar", "Foo.bar"},
      {"ZZoo_bar_info", K::Info, "", "Zoo", "bar", "Zoo.bar"},
      // `con_info` is tried first, but `pkg_M_con` can only be `pkg:M.con`.
      {"pkg_M_con_info", K::Info, "pkg", "M", "con", "M.con"},
  };
  for (const SymbolCase &c : cases) {
    SCOPED_TRACE(c.symbol);
    std::optional<GHCSymbol> sym = ParseGHCSymbol(c.symbol);
    ASSERT_TRUE(sym.has_value());
    EXPECT_EQ(sym->kind, c.kind);
    EXPECT_EQ(sym->unit, c.unit);
    EXPECT_EQ(sym->module, c.module);
    EXPECT_EQ(sym->name, c.name);
    EXPECT_FALSE(sym->IsInternal());
    EXPECT_FALSE(sym->HasUsageSite());
    EXPECT_EQ(sym->GetQualifiedName(),
              std::string(c.module) + "." + std::string(c.name));
    EXPECT_EQ(sym->GetDisplayName(), c.display);
  }
}

TEST(HaskellDemangle, ParseInternalSymbols) {
  // -fexpose-internal-symbols: _<module>_<name>_<unique>_<flavour>.
  std::optional<GHCSymbol> sym = ParseGHCSymbol("_Main_sat_s1ZZ_entry");
  ASSERT_TRUE(sym.has_value());
  EXPECT_EQ(sym->kind, GHCSymbolKind::Entry);
  EXPECT_EQ(sym->module, "Main");
  EXPECT_EQ(sym->name, "sat");
  // The unique is not Z-encoded: "ZZ" stays as it is.
  EXPECT_EQ(sym->unique, "s1ZZ");
  EXPECT_TRUE(sym->IsInternal());
  EXPECT_EQ(sym->GetQualifiedName(), "Main.sat_s1ZZ");
  EXPECT_EQ(sym->GetDisplayName(), "Main.sat_s1ZZ");

  sym = ParseGHCSymbol("_ghczminternal_GHCziInternalziShow_go_r3Ab_info");
  ASSERT_TRUE(sym.has_value());
  EXPECT_EQ(sym->unit, "ghc-internal");
  EXPECT_EQ(sym->GetQualifiedName(), "GHC.Internal.Show.go_r3Ab");

  sym = ParseGHCSymbol("_ZZoo_sat_s1_closure");
  ASSERT_TRUE(sym.has_value());
  EXPECT_EQ(sym->GetDisplayName(), "Zoo.sat_s1 (closure)");
}

TEST(HaskellDemangle, ParseConstructorUsageSites) {
  // -fdistinct-constructor-tables: one table per allocation site.
  std::optional<GHCSymbol> sym =
      ParseGHCSymbol("ghczmprim_GHCziTypes_Izh_Main_0_con_info");
  ASSERT_TRUE(sym.has_value());
  EXPECT_EQ(sym->kind, GHCSymbolKind::ConInfo);
  EXPECT_EQ(sym->unit, "ghc-prim");
  EXPECT_EQ(sym->module, "GHC.Types");
  EXPECT_EQ(sym->name, "I#");
  EXPECT_EQ(sym->usage_module, "Main");
  EXPECT_EQ(sym->usage_index, 0u);
  EXPECT_EQ(sym->GetQualifiedName(), "GHC.Types.I#");
  EXPECT_EQ(sym->GetDisplayName(), "GHC.Types.I# (con_info, Main:0)");

  sym = ParseGHCSymbol("Main_Leaf_Main_12_con_info");
  ASSERT_TRUE(sym.has_value());
  EXPECT_EQ(sym->unit, "");
  EXPECT_EQ(sym->GetDisplayName(), "Main.Leaf (con_info, Main:12)");

  sym = ParseGHCSymbol(
      "ghczmprim_GHCziTypes_ZC_containerszm0zi7_DataziMap_3_con_entry");
  ASSERT_TRUE(sym.has_value());
  EXPECT_EQ(sym->kind, GHCSymbolKind::ConEntry);
  EXPECT_EQ(sym->GetDisplayName(), "GHC.Types.: (con_entry, Data.Map:3)");
}

TEST(HaskellDemangle, RejectRTSSymbols) {
  const char *cases[] = {
      "stg_upd_frame_info",
      "stg_bh_upd_frame_info",
      "stg_marked_upd_frame_info",
      "stg_catch_frame_info",
      "stg_ap_v_info",
      "stg_ap_2_upd_info",
      "stg_sel_0_upd_info",
      "stg_ap_pp_fast",
      "stg_CAF_BLACKHOLE_info",
      "stg_IND_STATIC_info",
      "stg_ARR_WORDS_info",
      "stg_END_TSO_QUEUE_closure",
      "stg_BLACKHOLE_info",
      "stg_stop_thread_info",
      "stg_raisezh",
      "stg_cloneMyStackzh",
      "__stg_EAGER_BLACKHOLE_info",
      "StgRun",
      "StgReturn",
      "hs_main",
      "rts_evalLazyIO",
  };
  for (const char *c : cases) {
    SCOPED_TRACE(c);
    EXPECT_FALSE(ParseGHCSymbol(c).has_value());
  }
}

TEST(HaskellDemangle, RejectCSymbols) {
  const char *cases[] = {
      // C symbols that end in a GHC flavour.
      "mark_closure",
      "scavenge_fun_srt",
      "ffi_prep_closure",
      "ffi_prep_go_closure",
      "add_hash_entry",
      "forkOS_entry",
      "__frame_dummy_init_array_entry",
      "__do_global_dtors_aux_fini_array_entry",
      // Other C and C++ symbols.
      "main",
      "_start",
      "_IO_stdin_used",
      "memcpy",
      "_ZN4llvm3fooEv",
      // C code generated by GHC for CApiFFI imports.
      "ghczuwrapperZC0ZCbasezm4zi21zi0zi0zmae91ZCSystemziCPUTimeziPosixzi"
      "ClockGetTimeZCclockzugettime",
  };
  for (const char *c : cases) {
    SCOPED_TRACE(c);
    EXPECT_FALSE(ParseGHCSymbol(c).has_value());
  }
}

TEST(HaskellDemangle, RejectLabelsWithoutABinding) {
  const char *cases[] = {
      // String literals and SRTs are named only by a unique.
      "i8YN_str",
      ".Lr1ru_bytes",
      "_u1Yp_srt",
      // Module initialisers.
      "ghczminternal_GHCziInternalziConcziBound_init_arr",
      "ghczminternal_GHCziInternalziConcziBound_init__fexports",
      // Cost centres and HPC tick boxes.
      "Main_CAFs_cc",
      "Main_CAFs_cc_ccs",
      "Main_main_HPC_cc",
      "_hpc_tickboxes_Main_hpc",
  };
  for (const char *c : cases) {
    SCOPED_TRACE(c);
    EXPECT_FALSE(ParseGHCSymbol(c).has_value());
  }
}

TEST(HaskellDemangle, RejectMalformed) {
  const char *cases[] = {
      "",
      "_",
      "info",
      "_info",
      "Main_info",                 // No name.
      "Main__info",                // Empty part.
      "Main_sumTree_",             // No flavour.
      "Main_sumTree",              //
      "Main_sumTree_infox",        // Unknown flavour.
      "main_foo_info",             // A module starts with an upper-case letter.
      "Main_zf_info",              // Bad escape.
      "Main_1foo_info",            // An encoded name never starts with a digit.
      "Main.foo_info",             // Not Z-encoded.
      "a_Main_Foo_bar_info",       // Too many parts.
      "Main_Leaf_main_0_con_info", // The allocating module is not a module.
      "Main_Leaf_0_con_info",
  };
  for (const char *c : cases) {
    SCOPED_TRACE(c);
    EXPECT_FALSE(ParseGHCSymbol(c).has_value());
  }
}

TEST(HaskellDemangle, IsCode) {
  EXPECT_TRUE(ParseGHCSymbol("Main_f_info")->IsCode());
  EXPECT_TRUE(ParseGHCSymbol("Main_f_entry")->IsCode());
  EXPECT_TRUE(ParseGHCSymbol("Main_f_slow")->IsCode());
  EXPECT_TRUE(ParseGHCSymbol("Main_C_con_info")->IsCode());
  EXPECT_TRUE(ParseGHCSymbol("Main_C_con_entry")->IsCode());
  EXPECT_FALSE(ParseGHCSymbol("Main_f_closure")->IsCode());
  EXPECT_FALSE(ParseGHCSymbol("Main_T_closure_tbl")->IsCode());
  EXPECT_FALSE(ParseGHCSymbol("Main_s_bytes")->IsCode());
  EXPECT_FALSE(ParseGHCSymbol("Main_f_ct")->IsCode());
}
