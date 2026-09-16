/* Minimal KWV11-A/C programmable clock for the ESP32 Q-bus build. */
#include "pdp11_defs.h"
#include "sim_evtq.h"

#define KWV_CSR_GO      0000001
#define KWV_CSR_MODE    0000006
#define KWV_CSR_RATE    0000070
#define KWV_CSR_INTOV   0000100
#define KWV_CSR_OVFLO   0000200
#define KWV_CSR_DIO     010000
#define KWV_CSR_FOR     020000
#define KWV_CSR_ST2GOE  040000
#define KWV_CSR_INT2    0100000
#define KWV_CSR_ST2FLG  0200000
#define KWV_CSR_RW      (KWV_CSR_GO|KWV_CSR_MODE|KWV_CSR_RATE|KWV_CSR_INTOV|KWV_CSR_DIO|KWV_CSR_ST2GOE|KWV_CSR_INT2)
#define KWV_CSR_RCLR    (KWV_CSR_OVFLO|KWV_CSR_FOR|KWV_CSR_ST2FLG)

static uint32 kwv_csr, kwv_bpr, kwv_ctr;
static t_stat kwv_svc (UNIT *uptr);
static UNIT kwv_unit = { UDATA (&kwv_svc, UNIT_IDLE, 0) };

static uint32 kwv_usec (void)
{
    switch ((kwv_csr >> 3) & 7) {
    case 1: return 1; case 2: return 10; case 3: return 100;
    case 4: return 1000; case 5: return 10000; case 7: return 16667;
    default: return 0;
    }
}

static t_stat kwv_svc (UNIT *uptr)
{
    if (!(kwv_csr & KWV_CSR_GO) || kwv_usec () == 0) return SCPE_OK;
    if (++kwv_ctr > 0177777) {
        kwv_ctr = 0;
        if (kwv_csr & KWV_CSR_OVFLO) kwv_csr |= KWV_CSR_FOR;
        kwv_csr |= KWV_CSR_OVFLO;
        if (((kwv_csr >> 1) & 3) == 0) kwv_csr &= ~KWV_CSR_GO;
        if (((kwv_csr >> 1) & 3) == 1) kwv_ctr = kwv_bpr;
        if (kwv_csr & KWV_CSR_INTOV) SET_INT (KWV11);
    }
    if (kwv_csr & KWV_CSR_GO) sim_activate_after (uptr, kwv_usec ());
    return SCPE_OK;
}

static t_stat kwv_rd (int32 *data, int32 pa, int32 access)
{
    if (pa & 2) *data = kwv_bpr & 0177777;
    else {
        *data = kwv_csr;
        kwv_csr &= ~KWV_CSR_RCLR;
        CLR_INT (KWV11);
    }
    return SCPE_OK;
}

static t_stat kwv_wr (int32 data, int32 pa, int32 access)
{
    if (pa & 2) {
        kwv_bpr = data & 0177777;
        if ((kwv_csr & KWV_CSR_GO) && (((kwv_csr >> 1) & 3) == 0)) kwv_ctr = kwv_bpr;
        return SCPE_OK;
    }
    if (data & KWV_CSR_RCLR) kwv_csr &= ~(data & KWV_CSR_RCLR);
    kwv_csr = (kwv_csr & ~KWV_CSR_RW) | (data & KWV_CSR_RW);
    if (data & 0000400) {                              /* maintenance ST1 */
        if (++kwv_ctr > 0177777) { kwv_ctr = 0; kwv_csr |= KWV_CSR_OVFLO; if (kwv_csr & KWV_CSR_INTOV) SET_INT (KWV11); }
    }
    if (kwv_csr & KWV_CSR_GO) { if (kwv_usec ()) { sim_cancel (&kwv_unit); sim_activate_after (&kwv_unit, kwv_usec ()); } }
    else sim_cancel (&kwv_unit);
    if (!(kwv_csr & (KWV_CSR_OVFLO|KWV_CSR_ST2FLG))) CLR_INT (KWV11);
    return SCPE_OK;
}

static t_stat kwv_reset (DEVICE *dptr)
{
    kwv_csr = kwv_bpr = kwv_ctr = 0; CLR_INT (KWV11); sim_cancel (&kwv_unit);
    return auto_config (0, 0);
}

static DIB kwv_dib = { IOBA_AUTO, 4, &kwv_rd, &kwv_wr, 1, IVCL (KWV11), VEC_AUTO, { NULL } };
static REG kwv_reg[] = { { ORDATA (CSR, kwv_csr, 16) }, { ORDATA (BPR, kwv_bpr, 16) }, { ORDATA (CTR, kwv_ctr, 16) }, { NULL } };
static MTAB kwv_mod[] = { { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL, NULL, &show_addr, NULL }, { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR", &set_vec, &show_vec, NULL }, { 0 } };
static DEBTAB kwv_deb[] = { { NULL, 0 } };
DEVICE kwv11_dev = { "KWV11", &kwv_unit, kwv_reg, kwv_mod, 1, 10, 31, 1, 8, 8, NULL, NULL, &kwv_reset, NULL, NULL, NULL, &kwv_dib, DEV_DEBUG|DEV_DISABLE|DEV_DIS|DEV_QBUS, 0, kwv_deb };
