add_lldb_library(lldbPluginHaskellLanguage PLUGIN
  HaskellLanguage.cpp
  HaskellDemangle.cpp

  LINK_LIBS
    lldbCore
    lldbTarget
    lldbUtility
)
