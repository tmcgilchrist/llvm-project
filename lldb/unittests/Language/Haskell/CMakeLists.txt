add_lldb_unittest(LanguageHaskellTests
  HaskellDemangleTest.cpp
  HaskellIPETest.cpp
  HaskellLanguageTest.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbPluginHaskellLanguage
  )
