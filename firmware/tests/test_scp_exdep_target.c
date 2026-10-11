/* Host-only target resolution checks for the ESP adaptation of find_unit. */
#include "scp_exdep_adapter.h"
#include <assert.h>
#include <ctype.h>
#include <string.h>

static UNIT cpu_units[1];
static UNIT rq_units[2];
static DEVICE cpu = { .name = "CPU", .units = cpu_units, .numunits = 1 };
static DEVICE rq = { .name = "RQ", .units = rq_units, .numunits = 2,
                     .lname = "DISK" };
DEVICE *sim_devices[] = { &cpu, &rq, NULL };
DEVICE **sim_internal_devices;
int sim_internal_device_count;
int32 sim_switches;
int32 sim_switch_number;
const char *get_glyph(const char *input, char *output, char terminator)
{
    while (*input && !isspace((unsigned char)*input) &&
           (!terminator || *input != terminator))
        *output++ = (char)toupper((unsigned char)*input++);
    *output = 0;
    while (isspace((unsigned char)*input)) ++input;
    return input;
}
DEVICE *find_dev(const char *text)
{
    for (int i = 0; sim_devices[i]; ++i)
        if (!strcmp(text, sim_devices[i]->name) ||
            (sim_devices[i]->lname && !strcmp(text, sim_devices[i]->lname)))
            return sim_devices[i];
    return NULL;
}
const char *sim_uname(UNIT *unit) { return unit->uname ? unit->uname : ""; }
t_value strtotv(const char *text, const char **end, uint32 radix)
{
    t_value value = 0;
    const char *p = text;
    while (*p >= '0' && *p <= '9') {
        value = value * radix + (*p++ - '0');
    }
    *end = p;
    return value;
}

int main(void)
{
    UNIT *unit;
    rq_units[1].uname = "SYSTEM";
    assert(find_unit("CPU", &unit) == &cpu && unit == cpu_units);
    assert(find_unit("RQ", &unit) == &rq && unit == rq_units);
    assert(find_unit("RQ1", &unit) == &rq && unit == &rq_units[1]);
    assert(find_unit("DISK1", &unit) == &rq && unit == &rq_units[1]);
    assert(find_unit("SYSTEM", &unit) == &rq && unit == &rq_units[1]);
    assert(find_unit("RQ2", &unit) == &rq && unit == NULL);
    assert(find_unit("MISSING", &unit) == NULL && unit == NULL);
    rq.flags = DEV_DIS;
    assert(find_unit("RQ", &unit) == NULL && unit == NULL);
    assert(find_unit("RQ1", &unit) == NULL && unit == NULL);
    rq.flags = 0;
    DEVICE *selected = &cpu;
    const char *options = "RQ1 -MH 1000";
    unit = cpu_units;
    assert(scp_exdep_options(&options, &selected, &unit) == SCPE_OK);
    assert(selected == &rq && unit == &rq_units[1]);
    assert(strcmp(options, "1000") == 0);
    assert(sim_switches == (SWMASK('M') | SWMASK('H')));
    assert(scp_exdep_radix(8) == 16);
    selected = &cpu;
    unit = cpu_units;
    options = "-10 CPU PC";
    assert(scp_exdep_options(&options, &selected, &unit) == SCPE_OK);
    assert(selected == &cpu && unit == cpu_units && sim_switch_number == 10);
    assert(scp_exdep_radix(8) == 10);
    options = "-OD PC";
    assert(scp_exdep_options(&options, &selected, &unit) == SCPE_INVSW);
    options = "-Q PC";
    assert(scp_exdep_options(&options, &selected, &unit) == SCPE_INVSW);
    options = "-999999999999999999999999 PC";
    assert(scp_exdep_options(&options, &selected, &unit) == SCPE_INVSW);
    options = "RQ2 SA";
    assert(scp_exdep_options(&options, &selected, &unit) == SCPE_NXUN);
    return 0;
}
