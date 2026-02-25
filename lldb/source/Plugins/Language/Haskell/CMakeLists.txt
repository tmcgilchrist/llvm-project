add_lldb_library(lldbPluginHaskellLanguage PLUGIN
  HaskellLanguage.cpp
  HaskellDemangle.cpp
  HaskellFormatters.cpp

  LINK_LIBS
    lldbCore
    lldbDataFormatters
    lldbTarget
    lldbUtility
)
