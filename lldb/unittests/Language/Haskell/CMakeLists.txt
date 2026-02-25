add_lldb_unittest(LanguageHaskellTests
  HaskellDemangleTest.cpp
  HaskellLanguageTest.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbPluginHaskellLanguage
  )
