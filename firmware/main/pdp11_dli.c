/* Minimal DLI/DLO line-register compatibility layer. */
#include "pdp11_defs.h"

#define DL_CSR_IE   0000100
#define DL_CSR_DONE 0000200
static uint16 dli_csr, dlo_csr, dli_buf, dlo_buf;

static t_stat dli_rd (int32 *data, int32 pa, int32 access) {
    if (pa & 2) { *data = dli_buf; dli_csr &= ~DL_CSR_DONE; CLR_INT (DLI); }
    else *data = dli_csr;
    return SCPE_OK;
}
static t_stat dli_wr (int32 data, int32 pa, int32 access) {
    if (!(pa & 2)) { dli_csr = (uint16)data & DL_CSR_IE; CLR_INT (DLI); }
    return SCPE_OK;
}
static t_stat dlo_rd (int32 *data, int32 pa, int32 access) {
    if (pa & 2) *data = dlo_buf; else *data = dlo_csr;
    return SCPE_OK;
}
static t_stat dlo_wr (int32 data, int32 pa, int32 access) {
    if (pa & 2) { dlo_buf = (uint16)data; dlo_csr |= DL_CSR_DONE; if (dlo_csr & DL_CSR_IE) SET_INT (DLO); }
    else { dlo_csr = (uint16)data & DL_CSR_IE; if (!(dlo_csr & DL_CSR_IE)) CLR_INT (DLO); }
    return SCPE_OK;
}
static t_stat dli_reset (DEVICE *dptr) { dli_csr = dli_buf = 0; CLR_INT (DLI); return auto_config (0, 0); }
static t_stat dlo_reset (DEVICE *dptr) { dlo_csr = dlo_buf = DL_CSR_DONE; CLR_INT (DLO); return auto_config (0, 0); }
static DIB dli_dib = { IOBA_AUTO, 4, &dli_rd, &dli_wr, 1, IVCL (DLI), VEC_AUTO, { NULL } };
static DIB dlo_dib = { IOBA_AUTO, 4, &dlo_rd, &dlo_wr, 1, IVCL (DLO), VEC_AUTO, { NULL } };
static UNIT dli_unit = { UDATA (NULL, UNIT_IDLE, 0) }, dlo_unit = { UDATA (NULL, UNIT_IDLE, 0) };
static REG dl_reg[] = { { ORDATA (CSR, dli_csr, 16) }, { ORDATA (BUF, dli_buf, 16) }, { NULL } };
static MTAB dl_mod[] = { { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL, NULL, &show_addr, NULL }, { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR", &set_vec, &show_vec, NULL }, { 0 } };
static DEBTAB dl_deb[] = { { NULL, 0 } };
DEVICE dli_dev = { "DLI", &dli_unit, dl_reg, dl_mod, 1, 10, 31, 1, 8, 8, NULL, NULL, &dli_reset, NULL, NULL, NULL, &dli_dib, DEV_DEBUG|DEV_DISABLE|DEV_DIS|DEV_QBUS, 0, dl_deb };
DEVICE dlo_dev = { "DLO", &dlo_unit, dl_reg, dl_mod, 1, 10, 31, 1, 8, 8, NULL, NULL, &dlo_reset, NULL, NULL, NULL, &dlo_dib, DEV_DEBUG|DEV_DISABLE|DEV_DIS|DEV_QBUS, 0, dl_deb };
