#ifndef SD_UPLOAD_PORTAL_H
#define SD_UPLOAD_PORTAL_H

#include <stdbool.h>

#include "esp_err.h"

typedef bool (*sd_upload_cancel_fn)(void);

/* Run the local SD upload portal until the browser, console, or idle timeout
 * requests exit. The caller must ensure the simulator is not running. */
esp_err_t sd_upload_portal_run(sd_upload_cancel_fn cancel_requested);

/* Store or remove the portal's saved Wi-Fi credentials in NVS. */
esp_err_t sd_upload_portal_save_wifi_credentials(const char *ssid,
                                                  const char *password);
esp_err_t sd_upload_portal_clear_wifi_credentials(void);

#endif
