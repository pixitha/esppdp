/* Minimal DMC/DMV registration shim for Fuzzball boot compatibility.
   No serial peer or DDCMP transport is implemented here yet. */
#include "pdp11_defs.h"

static uint16 dmc_csr;
static uint16 dmc_data;

static t_stat dmc_rd (int32 *data, int32 pa, int32 access)
{
    switch ((pa >> 1) & 07) {
    case 0: *data = dmc_csr; return SCPE_OK;
    case 1: *data = dmc_data; return SCPE_OK;
    default: *data = 0; return SCPE_OK;
    }
}

static t_stat dmc_wr (int32 data, int32 pa, int32 access)
{
    switch ((pa >> 1) & 07) {
    case 0: dmc_csr = (uint16)data; CLR_INT (DMCRX); CLR_INT (DMCTX); break;
    case 1: dmc_data = (uint16)data; break;
    default: break;
    }
    return SCPE_OK;
}

static t_stat dmc_reset (DEVICE *dptr)
{
    dmc_csr = dmc_data = 0; CLR_INT (DMCRX); CLR_INT (DMCTX);
    return auto_config (0, 0);
}

static DIB dmc_dib = { IOBA_AUTO, 020, &dmc_rd, &dmc_wr, 1, IVCL (DMCRX), VEC_AUTO, { NULL } };
static UNIT dmc_unit = { UDATA (NULL, UNIT_IDLE, 0) };
static REG dmc_reg[] = { { ORDATA (CSR, dmc_csr, 16) }, { ORDATA (DATA, dmc_data, 16) }, { NULL } };
static MTAB dmc_mod[] = { { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL, NULL, &show_addr, NULL }, { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR", &set_vec, &show_vec, NULL }, { 0 } };
static DEBTAB dmc_deb[] = { { NULL, 0 } };
DEVICE dmc_dev = { "DMV", &dmc_unit, dmc_reg, dmc_mod, 1, 10, 31, 1, 8, 8, NULL, NULL, &dmc_reset, NULL, NULL, NULL, &dmc_dib, DEV_DEBUG|DEV_DISABLE|DEV_DIS|DEV_QBUS, 0, dmc_deb };
