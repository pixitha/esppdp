/* Minimal DLI/DLO line-register compatibility layer. */
#include "pdp11_defs.h"

#define DL_CSR_IE   0000100
#define DL_CSR_DONE 0000200
#define DCN6_DLI_BASE 0176520
#define DCN6_DLI_LINES 3
static uint16 dli_csr[DCN6_DLI_LINES], dlo_csr[DCN6_DLI_LINES];
static uint16 dli_buf[DCN6_DLI_LINES], dlo_buf[DCN6_DLI_LINES];
static DIB dli_dib, dlo_dib;

static t_stat dli_rd (int32 *data, int32 pa, int32 access) {
    int line = ((pa - (int32)dli_dib.ba) >> 3) % DCN6_DLI_LINES;
    if (pa & 2) { *data = dli_buf[line]; dli_csr[line] &= ~DL_CSR_DONE; CLR_INT (DLI); }
    else *data = dli_csr[line];
    return SCPE_OK;
}
static t_stat dli_wr (int32 data, int32 pa, int32 access) {
    int line = ((pa - (int32)dli_dib.ba) >> 3) % DCN6_DLI_LINES;
    if (!(pa & 2)) { dli_csr[line] = (uint16)data & DL_CSR_IE; CLR_INT (DLI); }
    return SCPE_OK;
}
static t_stat dlo_rd (int32 *data, int32 pa, int32 access) {
    int line = ((pa - (int32)dlo_dib.ba) >> 3) % DCN6_DLI_LINES;
    if (pa & 2) *data = dlo_buf[line]; else *data = dlo_csr[line];
    return SCPE_OK;
}
static t_stat dlo_wr (int32 data, int32 pa, int32 access) {
    int line = ((pa - (int32)dlo_dib.ba) >> 3) % DCN6_DLI_LINES;
    if (pa & 2) { dlo_buf[line] = (uint16)data; dlo_csr[line] |= DL_CSR_DONE; if (dlo_csr[line] & DL_CSR_IE) SET_INT (DLO); }
    else { dlo_csr[line] = (uint16)data & DL_CSR_IE; if (!(dlo_csr[line] & DL_CSR_IE)) CLR_INT (DLO); }
    return SCPE_OK;
}
static t_stat dli_reset (DEVICE *dptr) { memset (dli_csr, 0, sizeof dli_csr); memset (dli_buf, 0, sizeof dli_buf); CLR_INT (DLI); return SCPE_OK; }
static t_stat dlo_reset (DEVICE *dptr) { memset (dlo_csr, 0, sizeof dlo_csr); memset (dlo_buf, 0, sizeof dlo_buf); for (int i=0; i<DCN6_DLI_LINES; i++) dlo_csr[i] = DL_CSR_DONE; CLR_INT (DLO); return SCPE_OK; }
static DIB dli_dib = { DCN6_DLI_BASE, 024, &dli_rd, &dli_wr, 1, IVCL (DLI), 0320, { NULL } };
static DIB dlo_dib = { DCN6_DLI_BASE, 024, &dlo_rd, &dlo_wr, 1, IVCL (DLO), 0320, { NULL } };
static UNIT dli_unit = { UDATA (NULL, UNIT_IDLE, 0) }, dlo_unit = { UDATA (NULL, UNIT_IDLE, 0) };
static REG dl_reg[] = { { ORDATA (CSR, dli_csr[0], 16) }, { ORDATA (BUF, dli_buf[0], 16) }, { NULL } };
static MTAB dl_mod[] = { { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL, NULL, &show_addr, NULL }, { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR", &set_vec, &show_vec, NULL }, { 0 } };
static DEBTAB dl_deb[] = { { NULL, 0 } };
DEVICE dli_dev = { "DLI", &dli_unit, dl_reg, dl_mod, 1, 10, 31, 1, 8, 8, NULL, NULL, &dli_reset, NULL, NULL, NULL, &dli_dib, DEV_DEBUG|DEV_DISABLE|DEV_DIS|DEV_QBUS, 0, dl_deb };
DEVICE dlo_dev = { "DLO", &dlo_unit, dl_reg, dl_mod, 1, 10, 31, 1, 8, 8, NULL, NULL, &dlo_reset, NULL, NULL, NULL, &dlo_dib, DEV_DEBUG|DEV_DISABLE|DEV_DIS|DEV_QBUS, 0, dl_deb };
