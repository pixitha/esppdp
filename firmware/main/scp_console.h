#ifndef ESP_SCP_CONSOLE_H_
#define ESP_SCP_CONSOLE_H_

#include "sim_defs.h"

/* Enter the SCP command reader after a host escape or simulator stop.
   Returns only when a run command resumes the guest. */
void scp_console_run(DEVICE *bootdev);

#endif
