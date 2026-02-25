add_lldb_library(lldbPluginHaskellLanguage PLUGIN
  HaskellLanguage.cpp
  HaskellDemangle.cpp
  HaskellFormatters.cpp
  HaskellIPE.cpp

  LINK_LIBS
    lldbCore
    lldbDataFormatters
    lldbSymbol
    lldbTarget
    lldbUtility
)
