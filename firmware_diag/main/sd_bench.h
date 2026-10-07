#pragma once

#include <stdbool.h>

/* Mount the external SDMMC card, benchmark a new scratch file, then unmount. */
bool sd_bench_run(void);
