# Symbols in the shapes that GHC 9.12.2 emits on x86_64 Linux, next to RTS
# and C symbols that the Haskell plugin must leave alone.

        .text
        .globl  Main_sumTree_info
        .type   Main_sumTree_info, @function
Main_sumTree_info:
        nop
        ret
        .size   Main_sumTree_info, .-Main_sumTree_info

        .globl  Main_zdwmkTree_info
        .type   Main_zdwmkTree_info, @function
Main_zdwmkTree_info:
        nop
        ret
        .size   Main_zdwmkTree_info, .-Main_zdwmkTree_info

        .globl  ghczminternal_GHCziInternalziBase_zpzp_info
        .type   ghczminternal_GHCziInternalziBase_zpzp_info, @function
ghczminternal_GHCziInternalziBase_zpzp_info:
        nop
        ret
        .size   ghczminternal_GHCziInternalziBase_zpzp_info, .-ghczminternal_GHCziInternalziBase_zpzp_info

# -fexpose-internal-symbols
        .type   _Main_sat_s1ZZ_entry, @function
_Main_sat_s1ZZ_entry:
        nop
        ret
        .size   _Main_sat_s1ZZ_entry, .-_Main_sat_s1ZZ_entry

        .globl  Main_Leaf_con_info
        .type   Main_Leaf_con_info, @object
Main_Leaf_con_info:
        nop
        ret
        .size   Main_Leaf_con_info, .-Main_Leaf_con_info

        .globl  stg_ap_0_fast
        .type   stg_ap_0_fast, @function
stg_ap_0_fast:
        nop
        ret
        .size   stg_ap_0_fast, .-stg_ap_0_fast

        .globl  mark_closure
        .type   mark_closure, @function
mark_closure:
        nop
        ret
        .size   mark_closure, .-mark_closure

        .data
        .globl  Main_sumTree_closure
        .type   Main_sumTree_closure, @object
Main_sumTree_closure:
        .quad   Main_sumTree_info
        .size   Main_sumTree_closure, 8
