/* Embedded SCP command reader, adapted from SIMH's CTAB dispatch path.

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

   The port's original scp.c retained SIMH's glyph, device, and register
   interfaces but removed the command runtime.  Keep command dispatch here
   separate from its ESP boot-profile setup.  Only commands whose backing
   execution state exists in this port are registered below. */

#include "sim_defs.h"
/* SIMH's BIT initializer conflicts with ESP-IDF's BIT(n) expression. */
#ifdef BIT
#undef BIT
#endif
#include "sim_term.h"
#include "scp_console.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum { SCP_EX_DEPOSIT, SCP_EX_EXAMINE };
enum { SCP_RUN_CONTINUE, SCP_RUN_GO, SCP_RUN_BOOT, SCP_RUN_STEP, SCP_RUN_RESET, SCP_RUN_RUN };

static DEVICE *console_bootdev;
static t_bool console_resume;
extern const t_value width_mask[];
extern const REG *sim_PC;
extern UNIT *sim_clock_queue;
extern double sim_time, sim_rtime;
extern int32 noqueue_time;

/* SIMH SCP uses a scheduled unit to end STEP after N guest instructions. */
static t_stat step_svc(UNIT *uptr)
{
    (void)uptr;
    return SCPE_STEP;
}
static UNIT sim_step_unit = { UDATA(&step_svc, 0, 0) };

static t_stat console_run_cmd(int32 flag, CONST char *cptr);
static t_stat console_exdep_cmd(int32 flag, CONST char *cptr);
static t_stat console_show_cmd(int32 flag, CONST char *cptr);
static t_stat console_help_cmd(int32 flag, CONST char *cptr);

/* Like upstream SCP, command names and arguments are dispatched via CTAB.
   Prefix matching is supplied by find_ctab() in scp.c. */
static CTAB cmd_table[] = {
    { "EXAMINE",  console_exdep_cmd, SCP_EX_EXAMINE },
    { "DEPOSIT",  console_exdep_cmd, SCP_EX_DEPOSIT },
    { "RESET",    console_run_cmd,   SCP_RUN_RESET },
    { "RUN",      console_run_cmd,   SCP_RUN_RUN },
    { "GO",       console_run_cmd,   SCP_RUN_GO },
    { "STEP",     console_run_cmd,   SCP_RUN_STEP },
    { "CONTINUE", console_run_cmd,   SCP_RUN_CONTINUE },
    { "BOOT",     console_run_cmd,   SCP_RUN_BOOT },
    { "SHOW",     console_show_cmd,  0 },
    { "HELP",     console_help_cmd,  0 },
    { NULL, NULL, 0 }
};

static CONST char *skip_space(CONST char *p)
{
    while (isspace((unsigned char)*p)) ++p;
    return p;
}

static t_stat parse_number(CONST char *text, uint32 radix, t_value max,
                           t_value *value)
{
    t_stat status;
    if (*text == '-' || *text == '+') return SCPE_ARG;
    *value = get_uint(text, radix, max, &status);
    return status;
}

/* Upstream sim_run_boot_prep() discards the old event schedule before a
   fresh RUN or BOOT.  RESET alone deliberately leaves the queue intact. */
static t_stat run_boot_prep(void)
{
    UNIT *unit;
    sim_interval = 0;
    sim_time = sim_rtime = 0;
    noqueue_time = 0;
    while (sim_clock_queue != QUEUE_LIST_END) {
        unit = sim_clock_queue;
        sim_clock_queue = unit->next;
        unit->next = NULL;
    }
    return reset_all(0);
}

/* This stage supports scalar SIMH REG descriptors.  Do not silently treat
   array/structure registers as scalars: those need the upstream index code. */
static t_stat scalar_register(const REG *rptr, t_bool deposit, t_value *value)
{
    uint32 raw = 0;
    uint32 mask;
    if (rptr->loc == NULL || rptr->depth != 1 || rptr->obj_size > sizeof raw ||
        rptr->width == 0 || rptr->width > 32 ||
        rptr->offset + rptr->width > rptr->obj_size * CHAR_BIT)
        return SCPE_NOFNC;
    if (deposit && (rptr->flags & REG_RO)) return SCPE_RO;
    memcpy(&raw, rptr->loc, rptr->obj_size);
    mask = (uint32)width_mask[rptr->width];
    if (deposit) {
        raw = (raw & ~(mask << rptr->offset)) |
              (((uint32)*value & mask) << rptr->offset);
        memcpy(rptr->loc, &raw, rptr->obj_size);
    } else {
        *value = (raw >> rptr->offset) & mask;
    }
    return SCPE_OK;
}

static t_stat console_exdep_cmd(int32 flag, CONST char *cptr)
{
    DEVICE *cpu = find_dev("CPU");
    char target[CBUFSIZE], valtext[CBUFSIZE];
    CONST char *end;
    const REG *rptr;
    t_value value, address;
    t_stat status;
    uint32 radix;

    if (cpu == NULL || cpu->units == NULL) return SCPE_NXDEV;
    cptr = skip_space(cptr);
    sim_switches = 0;
    if (*cptr == '-') {
        ++cptr;
        while (isalpha((unsigned char)*cptr)) {
            int sw = toupper((unsigned char)*cptr++);
            if (sw != 'O' && sw != 'D' && sw != 'V') return SCPE_INVSW;
            sim_switches |= SWMASK(sw);
        }
        cptr = skip_space(cptr);
    }
    if ((sim_switches & SWMASK('O')) && (sim_switches & SWMASK('D')))
        return SCPE_INVSW;
    if (!*cptr) return SCPE_2FARG;
    cptr = get_glyph(cptr, target, 0);
    cptr = skip_space(cptr);
    for (char *p = target; *p; ++p) *p = (char)toupper((unsigned char)*p);
    if (flag == SCP_EX_EXAMINE && strcmp(target, "STATE") == 0) {
        if (*cptr) return SCPE_2MARG;
        for (rptr = cpu->registers; rptr && rptr->name; ++rptr) {
            if (rptr->flags & REG_HIDDEN) continue;
            if (scalar_register(rptr, FALSE, &value) != SCPE_OK) continue;
            radix = (sim_switches & SWMASK('D')) ? 10 :
                    (sim_switches & SWMASK('O')) ? 8 : rptr->radix;
            printf("%s: ", rptr->name);
            fprint_val(stdout, value, radix, rptr->width, PV_RZRO);
            putchar('\n');
        }
        return SCPE_OK;
    }
    rptr = find_reg(target, &end, cpu);
    if (rptr && *end == '\0') {
        if (rptr->width == 0 || rptr->width > 32) return SCPE_NOFNC;
        radix = (sim_switches & SWMASK('D')) ? 10 :
                (sim_switches & SWMASK('O')) ? 8 : rptr->radix;
        if (flag == SCP_EX_DEPOSIT) {
            if (!*cptr) return SCPE_2FARG;
            cptr = get_glyph(cptr, valtext, 0);
            if (*skip_space(cptr)) return SCPE_2MARG;
            status = parse_number(valtext, radix, width_mask[rptr->width], &value);
            if (status != SCPE_OK) return status;
        }
        else if (*cptr) return SCPE_2MARG;
        status = scalar_register(rptr, flag == SCP_EX_DEPOSIT, &value);
        if (status == SCPE_OK && flag == SCP_EX_EXAMINE) {
            printf("%s: ", rptr->name);
            fprint_val(stdout, value, radix, rptr->width, PV_RZRO);
            putchar('\n');
        }
        return status;
    }

    /* CPU addresses and data are octal by default, as in PDP-11 SCP. */
    if (cpu->awidth == 0 || cpu->awidth > 32 ||
        cpu->dwidth == 0 || cpu->dwidth > 32 || cpu->aincr == 0)
        return SCPE_NOFNC;
    radix = (sim_switches & SWMASK('D')) ? 10 :
            (sim_switches & SWMASK('O')) ? 8 : cpu->aradix;
    status = parse_number(target, radix, width_mask[cpu->awidth], &address);
    if (status != SCPE_OK) return status;
    if (address % cpu->aincr) return SCPE_ARG;
    if (flag == SCP_EX_DEPOSIT) {
        if (!*cptr) return SCPE_2FARG;
        cptr = get_glyph(cptr, valtext, 0);
        if (*skip_space(cptr)) return SCPE_2MARG;
        status = parse_number(valtext, radix, width_mask[cpu->dwidth], &value);
        if (status != SCPE_OK) return status;
        return cpu->deposit ? cpu->deposit(value, address, cpu->units,
                                           sim_switches) : SCPE_NOFNC;
    }
    if (*cptr) return SCPE_2MARG;
    status = cpu->examine ? cpu->examine(&value, address, cpu->units,
                                         sim_switches) : SCPE_NOFNC;
    if (status == SCPE_OK) {
        printf("%06o: ", (unsigned)address);
        fprint_val(stdout, value,
                   (sim_switches & SWMASK('D')) ? 10 : cpu->dradix,
                   cpu->dwidth, PV_RZRO);
        putchar('\n');
    }
    return status;
}

static t_stat console_run_cmd(int32 flag, CONST char *cptr)
{
    char arg[CBUFSIZE];
    t_value address;
    t_stat status;
    DEVICE *dev = console_bootdev;
    uint32 unit = 0;

    cptr = skip_space(cptr);
    if (flag == SCP_RUN_CONTINUE) {
        if (*cptr) return SCPE_2MARG;
        console_resume = TRUE;
        return SCPE_OK;
    }
    if (flag == SCP_RUN_RESET) {
        if (*cptr) return SCPE_2MARG;
        return reset_all(0);
    }
    if (flag == SCP_RUN_GO || flag == SCP_RUN_RUN) {
        t_bool has_address = *cptr != '\0';
        const REG *pc = sim_PC;
        if (pc == NULL) return SCPE_NXREG;
        if (*cptr) {
            cptr = get_glyph(cptr, arg, 0);
            if (*skip_space(cptr)) return SCPE_2MARG;
            status = parse_number(arg, pc->radix, width_mask[pc->width], &address);
            if (status != SCPE_OK) return status;
        }
        if (flag == SCP_RUN_RUN) {
            status = run_boot_prep();
            if (status != SCPE_OK) return status;
        }
        if (has_address) {
            status = scalar_register(pc, TRUE, &address);
            if (status != SCPE_OK) return status;
        }
        console_resume = TRUE;
        return SCPE_OK;
    }
    if (flag == SCP_RUN_STEP) {
        t_value count = 1;
        if (*cptr) {
            cptr = get_glyph(cptr, arg, 0);
            if (*skip_space(cptr)) return SCPE_2MARG;
            status = parse_number(arg, 10, INT_MAX, &count);
            if (status != SCPE_OK || count == 0) return SCPE_ARG;
        }
        status = sim_activate(&sim_step_unit, (int32)count);
        if (status == SCPE_OK) console_resume = TRUE;
        return status;
    }
    if (*cptr) {
        cptr = get_glyph(cptr, arg, 0);
        if (*skip_space(cptr)) return SCPE_2MARG;
        dev = find_dev(arg);
        if (dev == NULL) {
            /* SCP accepts BOOT RQ0 as well as BOOT RQ.  Try the exact
               device name first because some device names end in digits. */
            char *digits = arg + strlen(arg);
            while (digits > arg && isdigit((unsigned char)digits[-1])) --digits;
            if (digits == arg || !*digits) return SCPE_NXDEV;
            t_value parsed;
            status = parse_number(digits, 10, INT_MAX, &parsed);
            if (status != SCPE_OK) return status;
            *digits = '\0';
            dev = find_dev(arg);
            if (dev == NULL) return SCPE_NXDEV;
            if (parsed >= dev->numunits) return SCPE_NXUN;
            unit = (uint32)parsed;
        }
    }
    if (dev == NULL || dev->boot == NULL) return SCPE_NOFNC;
    status = run_boot_prep();
    if (status != SCPE_OK) return status;
    status = dev->boot((int32)unit, dev);
    if (status == SCPE_OK) console_resume = TRUE;
    return status;
}

static t_stat console_show_cmd(int32 flag, CONST char *cptr)
{
    char arg[CBUFSIZE];
    DEVICE *dev;
    (void)flag;
    cptr = skip_space(cptr);
    if (!*cptr) {
        for (size_t i = 0; sim_devices[i] != NULL; ++i) {
            dev = sim_devices[i];
            printf("%s%s%s\n", dev->name,
                   (dev->flags & DEV_DIS) ? " disabled" : " enabled",
                   dev == console_bootdev ? " [boot]" : "");
        }
        return SCPE_OK;
    }
    cptr = get_glyph(cptr, arg, 0);
    if (*skip_space(cptr)) return SCPE_2MARG;
    dev = find_dev(arg);
    if (dev == NULL) return SCPE_NXDEV;
    printf("%s: %s, %u unit(s)", dev->name,
           (dev->flags & DEV_DIS) ? "disabled" : "enabled",
           (unsigned)dev->numunits);
    if (dev->description != NULL) printf(" - %s", dev->description(dev));
    putchar('\n');
    for (uint32 i = 0; i < dev->numunits; ++i) {
        UNIT *unit = &dev->units[i];
        if (unit->flags & UNIT_ATTABLE)
            printf("  %s%u: %s\n", dev->name, (unsigned)i,
                   (unit->flags & UNIT_ATT) ?
                   (unit->filename ? unit->filename : "attached") : "not attached");
    }
    return SCPE_OK;
}

static t_stat console_help_cmd(int32 flag, CONST char *cptr)
{
    char arg[CBUFSIZE];
    CTAB *cmd;
    (void)flag;
    cptr = skip_space(cptr);
    if (*cptr) {
        cptr = get_glyph(cptr, arg, 0);
        if (*skip_space(cptr)) return SCPE_2MARG;
        cmd = find_ctab(cmd_table, arg);
        if (cmd == NULL) return SCPE_UNK;
        printf("%s\n", cmd->help ? cmd->help : cmd->name);
        return SCPE_OK;
    }
    puts("EXAMINE [-O|-D|-V] STATE|PC|register|address");
    puts("DEPOSIT [-O|-D|-V] PC|register|address value");
    puts("SHOW [device], GO [address], RUN [address], RESET, STEP [count]");
    puts("CONTINUE, BOOT [device or unit], HELP");
    puts("BREAK, SET, and full SCP media commands are not ported yet.");
    return SCPE_OK;
}

static char *console_read_line(char *line, size_t capacity)
{
    size_t used = 0;
    t_bool overflow = FALSE;
    printf("sim> ");
    fflush(stdout);
    for (;;) {
        t_stat input = sim_poll_kbd();
        if (input < SCPE_KFLAG) {
            vTaskDelay(1);
            continue;
        }
        int c = input & 0377;
        if (c == '\r' || c == '\n') {
            putchar('\n');
            line[used] = '\0';
            if (overflow) {
                puts("Command too long");
                return NULL;
            }
            return line;
        }
        if (c == 3) {
            puts("^C");
            return NULL;
        }
        if ((c == '\b' || c == 0177) && used != 0) {
            --used;
            fputs("\b \b", stdout);
            fflush(stdout);
            continue;
        }
        if (c >= 0x20 && c < 0x7f) {
            if (used + 1 < capacity) line[used++] = (char)c;
            else overflow = TRUE;
            putchar(c);
            fflush(stdout);
        }
    }
}

static const char *console_status_text(t_stat status)
{
    switch (status) {
    case SCPE_ARG: return "Invalid argument";
    case SCPE_2FARG: return "Missing argument";
    case SCPE_2MARG: return "Too many arguments";
    case SCPE_INVSW: return "Invalid switch";
    case SCPE_NOFNC: return "Function not implemented";
    case SCPE_RO: return "Read-only register";
    case SCPE_NXDEV: return "No such device";
    case SCPE_NXUN: return "No such unit";
    case SCPE_NXM: return "Non-existent memory";
    default: return sim_error_text(status);
    }
}

void scp_console_run(DEVICE *bootdev)
{
    char line[CBUFSIZE], glyph[CBUFSIZE];
    CONST char *args;
    CTAB *cmd;
    t_stat status;
    console_bootdev = bootdev;
    console_resume = FALSE;
    sim_cancel(&sim_step_unit); /* Cancel a pending step on any other stop. */
    sim_host_escape_enable(FALSE);
    puts("\nSIMH SCP command console (ESP port)");
    while (!console_resume) {
        char *input = console_read_line(line, sizeof line);
        if (input == NULL || !*skip_space(input)) continue;
        args = get_glyph_cmd(skip_space(input), glyph);
        cmd = find_ctab(cmd_table, glyph);
        sim_switches = 0;
        status = cmd ? cmd->action(cmd->arg, args) : SCPE_UNK;
        if (status != SCPE_OK)
            printf("%s%s\n", status == SCPE_UNK ? "Unknown command: " : "SCP: ",
                   status == SCPE_UNK ? glyph : console_status_text(status));
    }
    sim_host_escape_enable(TRUE);
}
