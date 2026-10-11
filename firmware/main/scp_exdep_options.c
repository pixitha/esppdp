/* The switch grammar of SIMH 8bbec614 get_sim_sw/get_switches, with
   bounded ESP option selection in place of the host-only get_sim_opt state. */

#include "scp_exdep_adapter.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

extern int32 sim_switches;
extern int32 sim_switch_number;

CONST char *get_sim_sw(CONST char *cptr)
{
    char glyph[CBUFSIZE];
    while (*cptr == '-') {
        const char *end = get_glyph(cptr, glyph, 0);
        const char *p = glyph + 1;
        if (!*p) return NULL;
        if (isdigit((unsigned char)*p)) {
            char *num_end;
            errno = 0;
            long number = strtol(p, &num_end, 10);
            if (errno == ERANGE || *num_end ||
                number < INT32_MIN || number > INT32_MAX)
                return NULL;
            sim_switch_number = (int32)number;
        } else {
            for (; *p; ++p) {
                if (!isalpha((unsigned char)*p)) return NULL;
                sim_switches |= SWMASK(toupper((unsigned char)*p));
            }
        }
        cptr = end;
    }
    return cptr;
}

uint32 scp_exdep_radix(uint32 default_radix)
{
    if (sim_switches & SWMASK('O')) return 8;
    if (sim_switches & SWMASK('D')) return 10;
    if (sim_switches & SWMASK('H')) return 16;
    if (sim_switch_number >= 2 && sim_switch_number <= 36)
        return (uint32)sim_switch_number;
    return default_radix;
}

t_stat scp_exdep_options(CONST char **cursor, DEVICE **dev, UNIT **unit)
{
    const uint32 allowed = SWMASK('O') | SWMASK('D') | SWMASK('H') |
                           SWMASK('V') | SWMASK('A') | SWMASK('B') |
                           SWMASK('C') | SWMASK('M') | SWMASK('R');
    const uint32 symbols = SWMASK('A') | SWMASK('B') | SWMASK('C') |
                           SWMASK('M') | SWMASK('R');
    char glyph[CBUFSIZE];
    CONST char *p;
    t_bool selected = FALSE;

    if (!cursor || !*cursor || !dev || !*dev || !unit || !*unit)
        return SCPE_IERR;
    sim_switches = 0;
    sim_switch_number = 0;
    p = *cursor;
    while (*p) {
        DEVICE *candidate;
        UNIT *candidate_unit = NULL;
        CONST char *next;
        if (*p == '-') {
            p = get_sim_sw(p);
            if (!p) return SCPE_INVSW;
            continue;
        }
        if (selected) break;
        next = get_glyph(p, glyph, 0);
        candidate = find_unit(glyph, &candidate_unit);
        if (!candidate) break;
        if (!candidate_unit) return SCPE_NXUN;
        *dev = candidate;
        *unit = candidate_unit;
        selected = TRUE;
        p = next;
    }
    if (sim_switches & ~allowed) return SCPE_INVSW;
    if (sim_switch_number &&
        (sim_switch_number < 2 || sim_switch_number > 36))
        return SCPE_INVSW;
    if (((sim_switches & SWMASK('O')) != 0) +
        ((sim_switches & SWMASK('D')) != 0) +
        ((sim_switches & SWMASK('H')) != 0) > 1) return SCPE_INVSW;
    if ((sim_switches & symbols) &&
        ((sim_switches & SWMASK('A')) != 0) +
        ((sim_switches & SWMASK('B')) != 0) +
        ((sim_switches & SWMASK('C')) != 0) +
        ((sim_switches & SWMASK('M')) != 0) +
        ((sim_switches & SWMASK('R')) != 0) > 1) return SCPE_INVSW;
    *cursor = p;
    return SCPE_OK;
}
