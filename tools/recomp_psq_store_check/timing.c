// SPDX-License-Identifier: GPL-3.0-or-later
// Separate translation unit preserves the actual out-of-line call boundary.
#define ppc_psq_store current_ppc_psq_store
#define g_mem_write_journal current_g_mem_write_journal
#define g_mem_write_journal_user current_g_mem_write_journal_user
#include "core/cpu.h"

// The same bounded pixel-store loop and live GQR4 are used in both variants.
// Inputs and CPU are opaque to this translation unit; RAM writes and register
// changes are observed by the caller, with no LTCG to erase those effects.
#define STORE_LOOP(helper) \
    for (u32 i = 0; i < pairs; ++i) { \
        cpu->fpr[1] = inputs[(i * 2u) & 255u]; \
        cpu->ps1[1] = inputs[(i * 2u + 1u) & 255u]; \
        helper(cpu, 1, 0x80000000u + ((i & 4095u) * 2u), false, 4, false, 0x81234564u); \
    }

__declspec(noinline) void timing_inline(CPUState* cpu, const f64* inputs, u32 pairs) {
    STORE_LOOP(ppc_psq_store_inline)
}

__declspec(noinline) void timing_outline(CPUState* cpu, const f64* inputs, u32 pairs) {
    STORE_LOOP(ppc_psq_store)
}
