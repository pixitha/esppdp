/* Host-only lifecycle check for the ESP disk-file adapter.  Uses a temp file. */
#include "sim_defs.h"
#include "sim_disk.h"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

static UNIT disk_unit;
static DEVICE disk_device;
FILE *sim_deb;

DEVICE *find_dev_from_unit(UNIT *unit)
{
    return unit == &disk_unit ? &disk_device : NULL;
}

void _sim_debug_unit(uint32 bits, UNIT *unit, const char *format, ...)
{
    (void)bits;
    (void)unit;
    (void)format;
}

int main(void)
{
    char path[] = "/tmp/esppdp-disk-XXXXXX";
    int file = mkstemp(path);
    assert(file >= 0);
    assert(ftruncate(file, 512) == 0);
    assert(close(file) == 0);

    disk_unit.flags = UNIT_ATTABLE;
    disk_unit.capac = 1;
    disk_device.units = &disk_unit;
    disk_device.numunits = 1;
    disk_device.aincr = 1;
    disk_device.dwidth = 16;
    disk_device.flags = DEV_DISK | DEV_SECTORS;

    assert(sim_disk_attach_ex(&disk_unit, path, 512, 2, TRUE, 0,
                              "TEST", 0, 0, NULL) == SCPE_OK);
    assert((disk_unit.flags & UNIT_ATT) && disk_unit.fileref &&
           disk_unit.filename && disk_unit.up8);
    assert(sim_disk_attach_ex(&disk_unit, path, 512, 2, TRUE, 0,
                              "TEST", 0, 0, NULL) == SCPE_ALATT);
    assert(sim_disk_detach(&disk_unit) == SCPE_OK);
    assert(!(disk_unit.flags & UNIT_ATT) && !disk_unit.fileref &&
           !disk_unit.filename && !disk_unit.up8);
    assert(sim_disk_detach(&disk_unit) == SCPE_UNATT);
    assert(unlink(path) == 0);
    assert(sim_disk_attach_ex(&disk_unit, path, 512, 2, TRUE, 0,
                              "TEST", 0, 0, NULL) == SCPE_OPENERR);
    assert(!disk_unit.fileref && !disk_unit.filename && !disk_unit.up8);
    return 0;
}
