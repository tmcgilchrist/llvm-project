//===-- HaskellLanguage.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_HASKELL_HASKELLLANGUAGE_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_HASKELL_HASKELLLANGUAGE_H

#include "lldb/Target/Language.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

/// Language plugin for Haskell code compiled by GHC.
///
/// GHC symbols are not recognised by Mangled::IsMangledName, so LLDB stores
/// them as already demangled. This plugin claims the ones that parse as GHC
/// labels (see HaskellDemangle.h) and supplies their Haskell names.
class HaskellLanguage : public Language {
public:
  HaskellLanguage() = default;

  ~HaskellLanguage() override = default;

  lldb::LanguageType GetLanguageType() const override {
    return lldb::eLanguageTypeHaskell;
  }

  llvm::StringRef GetUserEntryPointName() const override { return "main"; }

  bool IsSourceFile(llvm::StringRef file_path) const override;

  bool SymbolNameFitsToLanguage(const Mangled &name) const override;

  ConstString
  GetDemangledFunctionNameWithoutArguments(Mangled mangled) const override;

  ConstString GetDisplayDemangledName(Mangled mangled) const override;

  bool GetFunctionDisplayName(const SymbolContext &sc,
                              const ExecutionContext *exe_ctx,
                              FunctionNameRepresentation representation,
                              Stream &s) override;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static lldb_private::Language *CreateInstance(lldb::LanguageType language);

  static llvm::StringRef GetPluginNameStatic() { return "haskell"; }

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_HASKELL_HASKELLLANGUAGE_H
