#pragma once

#include <stdbool.h>

typedef enum {
    BOOT_RK05,
    BOOT_BOS6_RD54,
    BOOT_RA92,
    BOOT_RX
} boot_profile_t;

typedef struct {
    boot_profile_t profile;
    char path[256];
} boot_choice_t;

/* Called after filesystems and input devices are initialized, before SIMH. */
void boot_menu_select(void);
const boot_choice_t *boot_menu_choice(void);
/* Remember only a selection whose simulator attach and bootstrap succeeded. */
void boot_menu_remember(void);
