/* SIMH breakpoint table for the embedded SCP command runtime.

   Copyright (c) 1993-2016, Robert M Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.

   This is the upstream breakpoint-table behavior needed by PDP-11 CPU hooks:
   address/type lookup, proceed counts, same-instruction hit suppression, and
   type summaries. SCP action scripts and dynamic step-over breakpoints are
   not supported by the embedded command runtime. */

#include "sim_defs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32 sim_brk_summ;
uint32 sim_brk_match_type;
t_addr sim_brk_match_addr;

static BRKTAB **brk_tab;
static size_t brk_count;
static size_t brk_capacity;

/* The table is sorted by address; entries at one address form a type list. */
static size_t brk_index(t_addr loc, t_bool *found)
{
    size_t lo = 0, hi = brk_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (brk_tab[mid]->addr < loc) lo = mid + 1;
        else hi = mid;
    }
    *found = (lo < brk_count && brk_tab[lo]->addr == loc);
    return lo;
}

static void brk_recompute_summary(void)
{
    sim_brk_summ = 0;
    for (size_t i = 0; i < brk_count; ++i)
        for (BRKTAB *bp = brk_tab[i]; bp; bp = bp->next)
            sim_brk_summ |= bp->typ;
}

t_stat sim_brk_init(void)
{
    for (size_t i = 0; i < brk_count; ++i) {
        BRKTAB *bp = brk_tab[i];
        while (bp) {
            BRKTAB *next = bp->next;
            free(bp->act);
            free(bp);
            bp = next;
        }
    }
    free(brk_tab);
    brk_tab = NULL;
    brk_count = brk_capacity = 0;
    sim_brk_summ = sim_brk_match_type = 0;
    sim_brk_match_addr = 0;
    return SCPE_OK;
}

BRKTAB *sim_brk_fnd(t_addr loc)
{
    t_bool found;
    size_t index = brk_index(loc, &found);
    return found ? brk_tab[index] : NULL;
}

t_stat sim_brk_set(t_addr loc, int32 sw, int32 ncnt, CONST char *act)
{
    t_bool found;
    size_t index;
    BRKTAB *bp;
    uint32 type = sw ? (uint32)sw : sim_brk_dflt;

    /* One type per entry keeps counts and hit suppression independent. */
    if (!type || (type & (type - 1)) || (type & ~sim_brk_types) || ncnt < 0)
        return SCPE_ARG;
    if (act && *act) return SCPE_NOFNC;
    index = brk_index(loc, &found);
    if (found) {
        for (bp = brk_tab[index]; bp; bp = bp->next) {
            if (bp->typ == type) {
                bp->cnt = ncnt;
                return SCPE_OK;
            }
        }
    } else if (brk_count == brk_capacity) {
        size_t capacity = brk_capacity ? brk_capacity * 2 : 16;
        BRKTAB **table = realloc(brk_tab, capacity * sizeof(*table));
        if (!table) return SCPE_MEM;
        brk_tab = table;
        brk_capacity = capacity;
    }
    bp = calloc(1, sizeof(*bp));
    if (!bp) return SCPE_MEM;
    bp->addr = loc;
    bp->typ = type;
    bp->cnt = ncnt;
    for (size_t i = 0; i < SIM_BKPT_N_SPC; ++i)
        bp->time_fired[i] = -1.0;
    if (found) bp->next = brk_tab[index], brk_tab[index] = bp;
    else {
        memmove(&brk_tab[index + 1], &brk_tab[index],
                (brk_count - index) * sizeof(*brk_tab));
        brk_tab[index] = bp;
        ++brk_count;
    }
    sim_brk_summ |= type;
    return SCPE_OK;
}

t_stat sim_brk_clr(t_addr loc, int32 sw)
{
    t_bool found;
    size_t index = brk_index(loc, &found);
    if (!found) return SCPE_OK;
    BRKTAB **link = &brk_tab[index];
    while (*link) {
        BRKTAB *bp = *link;
        if (!sw || (bp->typ & (uint32)sw)) {
            *link = bp->next;
            free(bp->act);
            free(bp);
        } else link = &bp->next;
    }
    if (!brk_tab[index]) {
        memmove(&brk_tab[index], &brk_tab[index + 1],
                (brk_count - index - 1) * sizeof(*brk_tab));
        --brk_count;
    }
    brk_recompute_summary();
    return SCPE_OK;
}

t_stat sim_brk_clrall(int32 sw)
{
    for (size_t i = brk_count; i > 0; --i)
        sim_brk_clr(brk_tab[i - 1]->addr, sw);
    return SCPE_OK;
}

static void brk_print(FILE *st, const BRKTAB *bp)
{
    uint32 type = bp->typ;
    char letter = 'A';
    while (type > 1) type >>= 1, ++letter;
    fprintf(st, "%06o: -%c", (unsigned)bp->addr, letter);
    if (bp->cnt > 0) fprintf(st, " [%d]", bp->cnt);
    fputc('\n', st);
}

t_stat sim_brk_show(FILE *st, t_addr loc, int32 sw)
{
    for (BRKTAB *bp = sim_brk_fnd(loc); bp; bp = bp->next)
        if (!sw || (bp->typ & (uint32)sw)) brk_print(st, bp);
    return SCPE_OK;
}

t_stat sim_brk_showall(FILE *st, int32 sw)
{
    if (!brk_count) fputs("No breakpoints set\n", st);
    for (size_t i = 0; i < brk_count; ++i)
        sim_brk_show(st, brk_tab[i]->addr, sw);
    return SCPE_OK;
}

uint32 sim_brk_test(t_addr loc, uint32 btyp)
{
    uint32 spc = (btyp >> SIM_BKPT_V_SPC) & (SIM_BKPT_N_SPC - 1);
    double now;
    if (!(sim_brk_summ & btyp)) return 0;
    now = sim_gtime();
    for (BRKTAB *bp = sim_brk_fnd(loc); bp; bp = bp->next) {
        if (!(bp->typ & btyp) || bp->time_fired[spc] == now) continue;
        bp->time_fired[spc] = now;
        if (--bp->cnt > 0) continue;
        bp->cnt = 0;
        sim_brk_match_type = bp->typ & btyp;
        sim_brk_match_addr = loc;
        return sim_brk_match_type;
    }
    return 0;
}

void sim_brk_npc(uint32 cnt)
{
    if (!cnt || cnt > SIM_BKPT_N_SPC) cnt = SIM_BKPT_N_SPC;
    for (size_t i = 0; i < brk_count; ++i)
        for (BRKTAB *bp = brk_tab[i]; bp; bp = bp->next)
            for (size_t spc = 0; spc < cnt; ++spc)
                bp->time_fired[spc] = -1.0;
}

void sim_brk_clrspc(uint32 spc, uint32 btyp)
{
    if (spc >= SIM_BKPT_N_SPC) return;
    for (size_t i = 0; i < brk_count; ++i)
        for (BRKTAB *bp = brk_tab[i]; bp; bp = bp->next)
            if (bp->typ & btyp) bp->time_fired[spc] = -1.0;
}

const char *sim_brk_message(void)
{
    static char message[80];
    uint32 type = sim_brk_match_type;
    char letter = 'A';
    while (type > 1) type >>= 1, ++letter;
    snprintf(message, sizeof(message), "-%c Breakpoint at: %06o",
             letter, (unsigned)sim_brk_match_addr);
    return message;
}
