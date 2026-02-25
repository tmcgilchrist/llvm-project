//===-- HaskellLanguage.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaskellLanguage.h"
#include "HaskellDemangle.h"

#include "lldb/Core/Mangled.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(HaskellLanguage)

/// Returns the name as it appears in the symbol table. Mangled keeps names
/// it doesn't recognise as mangled in the demangled slot, which is where GHC
/// symbols end up. A few start with `_Z` (an internal symbol in a module
/// whose name starts with Z), and Mangled stores those as mangled.
static llvm::StringRef GetSymbolName(const Mangled &mangled) {
  if (ConstString name = mangled.GetMangledName())
    return name.GetStringRef();
  return mangled.GetDemangledName().GetStringRef();
}

static std::optional<haskell::GHCSymbol> ParseMangled(const Mangled &mangled) {
  llvm::StringRef name = GetSymbolName(mangled);
  if (name.empty())
    return std::nullopt;
  return haskell::ParseGHCSymbol(name);
}

bool HaskellLanguage::IsSourceFile(llvm::StringRef file_path) const {
  return file_path.ends_with_insensitive(".hs") ||
         file_path.ends_with_insensitive(".lhs");
}

bool HaskellLanguage::SymbolNameFitsToLanguage(const Mangled &name) const {
  return ParseMangled(name).has_value();
}

ConstString HaskellLanguage::GetDemangledFunctionNameWithoutArguments(
    Mangled mangled) const {
  if (std::optional<haskell::GHCSymbol> sym = ParseMangled(mangled))
    return ConstString(sym->GetQualifiedName());
  return Language::GetDemangledFunctionNameWithoutArguments(mangled);
}

ConstString HaskellLanguage::GetDisplayDemangledName(Mangled mangled) const {
  if (std::optional<haskell::GHCSymbol> sym = ParseMangled(mangled))
    return ConstString(sym->GetDisplayName());
  return Language::GetDisplayDemangledName(mangled);
}

bool HaskellLanguage::GetFunctionDisplayName(
    const SymbolContext &sc, const ExecutionContext *exe_ctx,
    FunctionNameRepresentation representation, Stream &s) {
  // Haskell CUs produce no Functions yet, so frames in Haskell code have only
  // a symbol. Leave Functions, and anything that isn't a GHC symbol, to the
  // default formatting.
  if (sc.function || !sc.symbol)
    return false;
  std::optional<haskell::GHCSymbol> sym = ParseMangled(sc.symbol->GetMangled());
  if (!sym)
    return false;
  s << sym->GetDisplayName();
  return true;
}

void HaskellLanguage::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(), "Haskell Language",
                                CreateInstance);
}

void HaskellLanguage::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

Language *HaskellLanguage::CreateInstance(lldb::LanguageType language) {
  switch (language) {
  case lldb::eLanguageTypeHaskell:
    return new HaskellLanguage();
  default:
    return nullptr;
  }
}
