/* Minimal DMC/DMV registration shim for Fuzzball boot compatibility.
   No serial peer or DDCMP transport is implemented here yet. */
#include "pdp11_defs.h"

static uint16 dmc_sel0, dmc_sel2, dmc_sel4, dmc_sel6;

static t_stat dmc_rd (int32 *data, int32 pa, int32 access)
{
    switch ((pa >> 1) & 03) {
    case 0: *data = dmc_sel0; return SCPE_OK;
    case 1: *data = dmc_sel2; CLR_INT (DMCRX); return SCPE_OK;
    case 2: *data = dmc_sel4; return SCPE_OK;
    default: *data = dmc_sel6; return SCPE_OK;
    }
}

static t_stat dmc_wr (int32 data, int32 pa, int32 access)
{
    switch ((pa >> 1) & 03) {
    case 0:
        dmc_sel0 = (uint16)data;
        if (dmc_sel0 & 0x4000) { dmc_sel0 = 0x8000; dmc_sel2 = dmc_sel4 = dmc_sel6 = 0; CLR_INT (DMCRX); CLR_INT (DMCTX); }
        if (dmc_sel0 & 0x0080) { dmc_sel0 &= ~0x0080; dmc_sel2 |= 0x0010; SET_INT (DMCRX); }
        break;
    case 1: dmc_sel2 = (uint16)data; CLR_INT (DMCRX); break;
    case 2: dmc_sel4 = (uint16)data; break;
    case 3: dmc_sel6 = (uint16)data; break;
    }
    return SCPE_OK;
}

static t_stat dmc_reset (DEVICE *dptr)
{
    dmc_sel0 = 0x8000; dmc_sel2 = dmc_sel4 = dmc_sel6 = 0;
    CLR_INT (DMCRX); CLR_INT (DMCTX);
    return auto_config (0, 0);
}

static DIB dmc_dib = { IOBA_AUTO, 020, &dmc_rd, &dmc_wr, 1, IVCL (DMCRX), VEC_AUTO, { NULL } };
static UNIT dmc_unit = { UDATA (NULL, UNIT_IDLE, 0) };
static REG dmc_reg[] = { { ORDATA (SEL0, dmc_sel0, 16) }, { ORDATA (SEL2, dmc_sel2, 16) }, { ORDATA (SEL4, dmc_sel4, 16) }, { ORDATA (SEL6, dmc_sel6, 16) }, { NULL } };
static MTAB dmc_mod[] = { { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL, NULL, &show_addr, NULL }, { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR", &set_vec, &show_vec, NULL }, { 0 } };
static DEBTAB dmc_deb[] = { { NULL, 0 } };
DEVICE dmc_dev = { "DMV", &dmc_unit, dmc_reg, dmc_mod, 1, 10, 31, 1, 8, 8, NULL, NULL, &dmc_reset, NULL, NULL, NULL, &dmc_dib, DEV_DEBUG|DEV_DISABLE|DEV_DIS|DEV_QBUS, 0, dmc_deb };
