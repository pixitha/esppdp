/* SIMH find_unit, adapted from simh/simh commit 8bbec614 for this fork's
   device registry. Device names, logical names, numbered units, and unit
   aliases retain upstream matching behavior; disabled devices are skipped.

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
   DEALINGS IN THE SOFTWARE. */

#include "sim_defs.h"
#include <ctype.h>
#include <string.h>

extern DEVICE **sim_internal_devices;
extern int sim_internal_device_count;

static DEVICE *match_numbered_or_alias(DEVICE *dev, const char *text,
                                       UNIT **unit)
{
    const char *prefixes[2];
    const char *suffix, *end;
    t_value index;
    if (!dev || !dev->units || !dev->numunits || (dev->flags & DEV_DIS))
        return NULL;
    prefixes[0] = dev->name;
    prefixes[1] = dev->lname;
    for (size_t p = 0; p < 2; ++p) {
        if (!prefixes[p] || strncmp(text, prefixes[p], strlen(prefixes[p])))
            continue;
        suffix = text + strlen(prefixes[p]);
        if (!isdigit((unsigned char)*suffix)) continue;
        index = strtotv(suffix, &end, 10);
        *unit = (*end || index >= dev->numunits) ? NULL : dev->units + index;
        return dev;
    }
    for (uint32 index = 0; index < dev->numunits; ++index) {
        const char *alias = sim_uname(&dev->units[index]);
        if (alias && *alias && !strcmp(text, alias)) {
            *unit = &dev->units[index];
            return dev;
        }
    }
    return NULL;
}

DEVICE *find_unit(const char *text, UNIT **unit)
{
    DEVICE *dev;
    if (!unit) return NULL;
    *unit = NULL;
    if (!text) return NULL;
    dev = find_dev(text);
    if (dev) {
        if ((dev->flags & DEV_DIS) || !dev->units || !dev->numunits)
            return NULL;
        *unit = dev->units;
        return dev;
    }
    for (size_t i = 0; (dev = sim_devices[i]) != NULL; ++i) {
        DEVICE *found = match_numbered_or_alias(dev, text, unit);
        if (found) return found;
    }
    for (int i = 0; i < sim_internal_device_count &&
                    (dev = sim_internal_devices[i]) != NULL; ++i) {
        DEVICE *found = match_numbered_or_alias(dev, text, unit);
        if (found) return found;
    }
    return NULL;
}
