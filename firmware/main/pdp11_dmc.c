/* Minimal DMC/DMV registration shim for Fuzzball boot compatibility.
   No serial peer or DDCMP transport is implemented here yet. */
#include "pdp11_defs.h"

static uint16 dmc_sel0, dmc_sel2, dmc_sel4, dmc_sel6;

#define DMC_SEL0_M_MCLEAR  0x4000
#define DMC_SEL0_M_STEPUP  0x0100
#define DMC_SEL0_M_ROMI    0x0200
#define DMC_SEL0_M_ROMO    0x0400
#define DMC_SEL0_M_LU_LOOP 0x0800
#define DMC_SEL0_M_STEPLU  0x1000
#define DMV_STARTUP_BSEL4  000033
#define DMV_STARTUP_BSEL6  000305

static uint16 *dmc_reg_ptr (int32 pa)
{
    switch (pa & 016) {
    case 000: return &dmc_sel0;   /* BSEL0/BSEL1 */
    case 002: return &dmc_sel2;
    case 004: return &dmc_sel4;
    case 006: return &dmc_sel6;
    default:  return NULL;
    }
}

static void dmc_master_clear (void)
{
    /* Match the host SIMH DMV's no-peer post-MCLR state.  Fuzzball's MCLR is
       an odd-byte write to BSEL1, i.e. bit 14 of the SEL0 word. */
    dmc_sel0 &= (uint16)~(DMC_SEL0_M_MCLEAR | DMC_SEL0_M_STEPUP |
                          DMC_SEL0_M_ROMI | DMC_SEL0_M_ROMO |
                          DMC_SEL0_M_LU_LOOP | DMC_SEL0_M_STEPLU);
    dmc_sel2 = 0;
    dmc_sel4 = DMV_STARTUP_BSEL4;
    dmc_sel6 = DMV_STARTUP_BSEL6;
    CLR_INT (DMCRX);
    CLR_INT (DMCTX);
}

#if defined(ESP_PLATFORM) && CONFIG_ESPPDP_BOOT_TRACE
static uint32 dmc_trace_count;
static void dmc_trace (const char *op, int32 pa, int32 value)
{
    if (dmc_trace_count < 64) {
        printf("[DMV %s #%u reg=%02o value=%06o sel0=%06o sel2=%06o sel4=%06o sel6=%06o]\n",
               op, (unsigned)dmc_trace_count++,
               (unsigned)(pa & 017), (unsigned)value,
               (unsigned)dmc_sel0, (unsigned)dmc_sel2,
               (unsigned)dmc_sel4, (unsigned)dmc_sel6);
    }
}
#endif

static t_stat dmc_rd (int32 *data, int32 pa, int32 access)
{
    uint16 *reg = dmc_reg_ptr (pa);
    if (reg == NULL)
        return SCPE_NXM;
    *data = *reg;
    if ((pa & 016) == 002)
        CLR_INT (DMCRX);
#if defined(ESP_PLATFORM) && CONFIG_ESPPDP_BOOT_TRACE
    dmc_trace("read", pa, *data);
#endif
    return SCPE_OK;
}

static t_stat dmc_wr (int32 data, int32 pa, int32 access)
{
    uint16 *reg = dmc_reg_ptr (pa);
    if (reg == NULL)
        return SCPE_NXM;

    if (access == WRITE)
        *reg = (uint16)data;
    else if (pa & 1)
        *reg = (uint16)((*reg & 0x00ff) | (((uint16)data & 0x00ff) << 8));
    else
        *reg = (uint16)((*reg & 0xff00) | ((uint16)data & 0x00ff));

    if ((pa & 016) == 000 && (dmc_sel0 & DMC_SEL0_M_MCLEAR))
        dmc_master_clear ();
    if ((pa & 016) == 002)
        CLR_INT (DMCRX);
#if defined(ESP_PLATFORM) && CONFIG_ESPPDP_BOOT_TRACE
    dmc_trace("write", pa, data);
#endif
    return SCPE_OK;
}

static t_stat dmc_reset (DEVICE *dptr)
{
    dmc_sel0 = dmc_sel2 = dmc_sel4 = dmc_sel6 = 0;
    dmc_master_clear ();
    return auto_config (0, 0);
}

static DIB dmc_dib = { IOBA_AUTO, 020, &dmc_rd, &dmc_wr, 1, IVCL (DMCRX), VEC_AUTO, { NULL } };
static UNIT dmc_unit = { UDATA (NULL, UNIT_IDLE, 0) };
static REG dmc_reg[] = { { ORDATA (SEL0, dmc_sel0, 16) }, { ORDATA (SEL2, dmc_sel2, 16) }, { ORDATA (SEL4, dmc_sel4, 16) }, { ORDATA (SEL6, dmc_sel6, 16) }, { NULL } };
static MTAB dmc_mod[] = { { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL, NULL, &show_addr, NULL }, { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR", &set_vec, &show_vec, NULL }, { 0 } };
static DEBTAB dmc_deb[] = { { NULL, 0 } };
/* Public device identity matches SIMH's DMV slot; this remains a boot shim,
   not the full DMC/DMV controller implementation. */
DEVICE dmv_dev = { "DMV", &dmc_unit, dmc_reg, dmc_mod, 1, 10, 31, 1, 8, 8, NULL, NULL, &dmc_reset, NULL, NULL, NULL, &dmc_dib, DEV_DEBUG|DEV_DISABLE|DEV_DIS|DEV_QBUS, 0, dmc_deb };

/* Match the known BOS6 no-peer desktop boot profile. */
void dmv_configure_bos6_boot(void)
{
    dmc_dib.ba = IOPAGEBASE + 000020;
    dmc_dib.vec = 000300;
}
