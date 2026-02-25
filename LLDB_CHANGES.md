# LLDB changes for Haskell support

This file lists every change this fork makes outside the Haskell plugin
directories (`lldb/source/Plugins/*/Haskell`, `lldb/unittests/*/Haskell`,
`lldb/test/Shell/*/Haskell`). Keep it current: it is the checklist for
rebasing onto upstream and for proposing changes upstream.

## Build wiring (fork only)

| File | Change |
|---|---|
| `lldb/source/Plugins/Language/CMakeLists.txt` | `add_subdirectory(Haskell)` |
| `lldb/unittests/Language/CMakeLists.txt` | `add_subdirectory(Haskell)` |

## LLDB core

None yet. Planned changes:

- `DWARFCallFrameInfo.cpp`: parse `DW_CFA_val_offset` and
  `DW_CFA_val_offset_sf` in CIEs, which GHC uses to describe Sp. A plain
  bug fix, to send upstream first.
- `Symtab`: a hook that lets a Language plugin name symbols with no
  mangling scheme, so GHC names are indexed.
- `DWARFIndex`: match demangled linkage names.
- `llvm/include/llvm/BinaryFormat/Dwarf.def`: GHC vendor attributes and
  tags.
- `DWARFASTParser.h`: a parser kind for Haskell.
