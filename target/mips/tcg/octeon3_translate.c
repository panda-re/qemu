/*
 * QEMU MIPS: Octeon III fast-path translations (no helpers)
 *
 * Implements:
 *   V3MULU      rd, rs, rt
 *   MTM0_V3     rs, rt
 *   MTM1_V3     rs, rt
 *   MTM2_V3     rs, rt
 *   MTP0_V3     rs, rt
 *   MTP1_V3     rs, rt
 *   MTP2_V3     rs, rt
 *
 * Requires CPUMIPSState contains:
 *   uint64_t oct_mpl[6];
 *   uint64_t oct_p[6];
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"
#include "tcg/tcg-op.h"        /* tcg_gen_* */
#include "tcg/tcg-op-gvec.h"   /* (not strictly needed, harmless) */

/* Include the auto-generated decoder. */
#include "decode-octeon3.c.inc"

/* Offsets for direct env stores (compile-time, no helpers). */
#define OCT_MPL_OFF(i) (offsetof(CPUMIPSState, oct_mpl) + (i) * sizeof(uint64_t))
#define OCT_P_OFF(i)   (offsetof(CPUMIPSState, oct_p)   + (i) * sizeof(uint64_t))

/* Require Octeon3 for these opcodes. */
static inline bool require_octeon3(DisasContext *ctx)
{
    if (!(ctx->insn_flags & INSN_OCTEON3)) {
        generate_exception_err(ctx, EXCP_RI, 0);
        return false;
    }
    return true;
}

/* -------- V3MULU ----------------------------------------------------- */
/* Unsigned 64x64 -> 64 (low) multiply. */
static bool trans_V3MULU(DisasContext *ctx, arg_V3MULU *a)
{
    if (!require_octeon3(ctx)) {
        return true;
    }
    if (a->rd == 0) {
        return true; /* write to $zero is a NOP */
    }

    TCGv t_rs = tcg_temp_new();
    TCGv t_rt = tcg_temp_new();

    gen_load_gpr(t_rs, a->rs);
    gen_load_gpr(t_rt, a->rt);

    tcg_gen_mul_tl(cpu_gpr[a->rd], t_rs, t_rt);
    return true;
}

/* -------- MTM*_V3 / MTP*_V3 ------------------------------------------ */
/*
 * MTM{0,1,2}_V3 rs, rt  →  mpl[idx_a] = rs; mpl[idx_b] = rt
 *   MTM0: (0,3)  MTM1: (1,4)  MTM2: (2,5)
 *
 * MTP{0,1,2}_V3 rs, rt  →  p[idx_a] = rs; p[idx_b] = rt
 *   MTP0: (0,3)  MTP1: (1,4)  MTP2: (2,5)
 *
 * Matches the kernel-facing layout (pt_regs has mpl/mtp slots on Octeon).
 */

static inline bool do_mtm_v3(DisasContext *ctx, int rs, int rt, int idx_a, int idx_b)
{
    if (!require_octeon3(ctx)) {
        return true;
    }

    TCGv t_rs = tcg_temp_new();
    TCGv t_rt = tcg_temp_new();

    gen_load_gpr(t_rs, rs);
    gen_load_gpr(t_rt, rt);

    /* tcg_env is the TCGv_ptr base for the CPU state. */
    tcg_gen_st_tl(t_rs, tcg_env, OCT_MPL_OFF(idx_a));
    tcg_gen_st_tl(t_rt, tcg_env, OCT_MPL_OFF(idx_b));
    return true;
}

static inline bool do_mtp_v3(DisasContext *ctx, int rs, int rt, int idx_a, int idx_b)
{
    if (!require_octeon3(ctx)) {
        return true;
    }

    TCGv t_rs = tcg_temp_new();
    TCGv t_rt = tcg_temp_new();

    gen_load_gpr(t_rs, rs);
    gen_load_gpr(t_rt, rt);

    tcg_gen_st_tl(t_rs, tcg_env, OCT_P_OFF(idx_a));
    tcg_gen_st_tl(t_rt, tcg_env, OCT_P_OFF(idx_b));
    return true;
}

static bool trans_MTM0_V3(DisasContext *ctx, arg_MTM0_V3 *a)
{
    return do_mtm_v3(ctx, a->rs, a->rt, 0, 3);
}
static bool trans_MTM1_V3(DisasContext *ctx, arg_MTM1_V3 *a)
{
    return do_mtm_v3(ctx, a->rs, a->rt, 1, 4);
}
static bool trans_MTM2_V3(DisasContext *ctx, arg_MTM2_V3 *a)
{
    return do_mtm_v3(ctx, a->rs, a->rt, 2, 5);
}

static bool trans_MTP0_V3(DisasContext *ctx, arg_MTP0_V3 *a)
{
    return do_mtp_v3(ctx, a->rs, a->rt, 0, 3);
}
static bool trans_MTP1_V3(DisasContext *ctx, arg_MTP1_V3 *a)
{
    return do_mtp_v3(ctx, a->rs, a->rt, 1, 4);
}
static bool trans_MTP2_V3(DisasContext *ctx, arg_MTP2_V3 *a)
{
    return do_mtp_v3(ctx, a->rs, a->rt, 2, 5);
}
