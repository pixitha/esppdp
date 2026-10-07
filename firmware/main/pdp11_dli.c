/* Minimal DLI/DLO line-register compatibility layer. */
#include "pdp11_defs.h"

#define DL_CSR_IE   0000100
#define DL_CSR_DONE 0000200
#define DCN6_DLI_BASE (IOPAGEBASE + 016520)
#define DCN6_DLI_LINES 5
#define DCN6_DLI_SPAN 0050
static uint16 dli_csr[DCN6_DLI_LINES], dlo_csr[DCN6_DLI_LINES];
static uint16 dli_buf[DCN6_DLI_LINES], dlo_buf[DCN6_DLI_LINES];
static int dli_pending_line, dlo_pending_line;
static DIB dli_dib;
static int32 dli_inta (void) { return dli_dib.vec + (dli_pending_line * 010); }
static int32 dlo_inta (void) { return dli_dib.vec + (dlo_pending_line * 010) + 4; }

/* The five-line DL11 map includes the DCN6 ports at 17776520, 17776540,
 * and 17776560 (physical lines 0, 2, and 4). Each line occupies eight bytes
 * of RCSR/RBUF/TCSR/TBUF. SIMH passes the full physical address here. */
static int dl_line_index(int32 pa)
{
    int32 offset = pa - dli_dib.ba;
    if (offset < 0 || offset >= DCN6_DLI_SPAN)
        return -1;
    return offset >> 3;
}

static t_stat dl_rd (int32 *data, int32 pa, int32 access) {
    int line = dl_line_index(pa);
    if (line < 0) return SCPE_NXM;
    switch (pa & 0006) {
    case 0000: *data = dli_csr[line]; break;
    case 0002: *data = dli_buf[line]; dli_csr[line] &= ~DL_CSR_DONE; CLR_INT (DLI); break;
    case 0004: *data = dlo_csr[line]; break;
    case 0006: *data = dlo_buf[line]; break;
    }
    return SCPE_OK;
}
static t_stat dl_wr (int32 data, int32 pa, int32 access) {
    int line = dl_line_index(pa);
    if (line < 0) return SCPE_NXM;
    switch (pa & 0006) {
    case 0000:
        if (pa & 1) break; /* high CSR byte is read-only */
        dli_csr[line] = (dli_csr[line] & DL_CSR_DONE) | ((uint16)data & DL_CSR_IE);
        if (!(dli_csr[line] & DL_CSR_IE)) CLR_INT (DLI);
        else if (dli_csr[line] & DL_CSR_DONE) { dli_pending_line = line; SET_INT (DLI); }
        break;
    case 0002: break; /* receive buffer is read-only */
    case 0004:
        if (pa & 1) break; /* high CSR byte is read-only */
        dlo_csr[line] = (dlo_csr[line] & DL_CSR_DONE) | ((uint16)data & DL_CSR_IE);
        if (!(dlo_csr[line] & DL_CSR_IE)) CLR_INT (DLO);
        else if (dlo_csr[line] & DL_CSR_DONE) { dlo_pending_line = line; SET_INT (DLO); }
        break;
    case 0006:
        if (!(pa & 1)) dlo_buf[line] = (uint16)data & 0377;
        dlo_csr[line] |= DL_CSR_DONE;
        dlo_pending_line = line;
        if (dlo_csr[line] & DL_CSR_IE) SET_INT (DLO);
        break;
    }
    return SCPE_OK;
}
static t_stat dli_reset (DEVICE *dptr) { memset (dli_csr, 0, sizeof dli_csr); memset (dli_buf, 0, sizeof dli_buf); dli_pending_line = 0; CLR_INT (DLI); return SCPE_OK; }
static t_stat dlo_reset (DEVICE *dptr) { memset (dlo_csr, 0, sizeof dlo_csr); memset (dlo_buf, 0, sizeof dlo_buf); dlo_pending_line = 0; for (int i=0; i<DCN6_DLI_LINES; i++) dlo_csr[i] = DL_CSR_DONE; CLR_INT (DLO); return SCPE_OK; }
/* One DIB owns both halves of each DL11 line, as in desktop SIMH. DLO is a
 * logical companion device without a second, conflicting bus registration. */
static DIB dli_dib = { DCN6_DLI_BASE, DCN6_DLI_SPAN, &dl_rd, &dl_wr, 2, IVCL (DLI), 0320, { dli_inta, dlo_inta }, 010 };
void dli_configure_bos6_boot(void)
{
    dli_dib.ba = DCN6_DLI_BASE;
    dli_dib.vec = 0320;
}

static UNIT dli_unit = { UDATA (NULL, UNIT_IDLE, 0) }, dlo_unit = { UDATA (NULL, UNIT_IDLE, 0) };
static REG dl_reg[] = { { ORDATA (CSR, dli_csr[0], 16) }, { ORDATA (BUF, dli_buf[0], 16) }, { NULL } };
static MTAB dl_mod[] = { { MTAB_XTD|MTAB_VDV|MTAB_VALR, 004, "ADDRESS", "ADDRESS", &set_addr, &show_addr, NULL }, { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR", &set_vec, &show_vec, NULL }, { 0 } };
static MTAB dlo_mod[] = { { 0 } };
static DEBTAB dl_deb[] = { { NULL, 0 } };
DEVICE dli_dev = { "DLI", &dli_unit, dl_reg, dl_mod, 1, 10, 31, 1, 8, 8, NULL, NULL, &dli_reset, NULL, NULL, NULL, &dli_dib, DEV_DEBUG|DEV_DISABLE|DEV_DIS|DEV_QBUS, 0, dl_deb };
DEVICE dlo_dev = { "DLO", &dlo_unit, dl_reg, dlo_mod, 1, 10, 31, 1, 8, 8, NULL, NULL, &dlo_reset, NULL, NULL, NULL, NULL, DEV_DEBUG|DEV_DISABLE|DEV_DIS|DEV_QBUS, 0, dl_deb };
