// The RTS that GHC ships has no debug information. Compiled with -g, this
// file gives LLDB the RTS's closure types, so that tests can cast to
// `StgClosure *`.
#include "Rts.h"

StgClosure *lldb_test_closure;
