#ifndef ESP_SCP_EXDEP_ADAPTER_H_
#define ESP_SCP_EXDEP_ADAPTER_H_

#include "sim_defs.h"

/* Bounded ESP adaptation of SIMH's exdep_reg_loop/exdep_addr_loop.
   UINT32_MAX as highs means every element of each selected register. */
enum { SCP_EXDEP_MAX_VALUES = 64 };

CONST char *get_sim_sw(CONST char *cptr);
t_stat scp_exdep_options(CONST char **cursor, DEVICE **dev, UNIT **unit);
uint32 scp_exdep_radix(uint32 default_radix);

t_stat scp_exdep_range(DEVICE *dev, CONST char *text, uint32 radix,
                       t_addr max, char terminator, t_addr *low, t_addr *high);
t_stat scp_exdep_register(const REG *reg, uint32 index, t_bool deposit,
                          t_value *value);
t_stat scp_exdep_reg_loop(FILE *out, DEVICE *dev,
                          const REG *first, const REG *last,
                          uint32 lows, uint32 highs, t_bool deposit,
                          t_value value, t_bool skip_hidden);
t_stat scp_exdep_addr_loop(FILE *out, DEVICE *dev, UNIT *unit,
                           t_addr low, t_addr high, t_bool deposit,
                           CONST char *deposit_text);

#endif
