#include "sd_upload_portal.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <psa/crypto.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#define TAG "sd_upload"
#define SD_ROOT "/sdcard"
#define MAX_UPLOAD_NAME 63
#define MAX_UPLOAD_BYTES (1024ULL * 1024ULL * 1024ULL)
#define MIN_FREE_BYTES (1024ULL * 1024ULL)
#define IDLE_TIMEOUT_US (10LL * 60LL * 1000LL * 1000LL)
#define PORTAL_STOP_BIT BIT0
#define PORTAL_ACTIVITY_BIT BIT1
#define PORTAL_GOT_IP_BIT BIT2
#define PORTAL_DISCONNECTED_BIT BIT3
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define NETWORK_BODY_MAX 256

static char s_upload_token_storage[33];
static EventGroupHandle_t s_portal_events;
static SemaphoreHandle_t s_upload_lock;
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static bool s_wifi_is_sta;
static char s_network_ip[16];

static const char s_index_html[] =
    "<!doctype html><html lang='en'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta charset='utf-8'><title>Fuzzball SD Card</title>"
    "<style>body{font:16px system-ui;max-width:48rem;margin:2rem auto;padding:0 1rem;color:#182230}"
    "button,input{font:inherit;padding:.55rem;margin:.25rem 0}button{cursor:pointer}"
    "progress{width:100%;height:1.4rem}li{overflow-wrap:anywhere}.muted{color:#526174}</style>"
    "<h1>Fuzzball SD Card</h1><p id='space' class='muted'>Reading card status…</p>"
    "<label>Upload access code<br><input id='token' type='password' autocomplete='off' required></label><br>"
    "<label>Files to copy to the SD card<br><input id='files' type='file' multiple></label><br>"
    "<button id='upload'>Upload selected files</button> <button id='finish'>Finish and return</button>"
    "<h2>Wi-Fi network</h2><p class='muted'>Save your network for the next time the portal starts. Credentials are kept in device NVS.</p>"
    "<label>Network name (SSID)<br><input id='ssid' maxlength='32' autocomplete='off'></label><br>"
    "<label>Wi-Fi password<br><input id='wifiPassword' type='password' maxlength='63' autocomplete='new-password'></label><br>"
    "<button id='saveNetwork'>Save Wi-Fi for next time</button>"
    "<p id='status' role='status'></p><progress id='progress' max='100' value='0'></progress>"
    "<h2>Files on card</h2><ul id='list'></ul>"
    "<script>const $=id=>document.getElementById(id);async function refresh(){"
    "try{let s=await(await fetch('/api/status')).json();$('space').textContent=`SD card: ${s.free} free of ${s.total} bytes`;"
    "let a=await(await fetch('/api/files')).json();$('list').replaceChildren(...a.map(f=>{let li=document.createElement('li');"
    "li.textContent=`${f.name} (${f.size} bytes)`;return li;}));}catch(e){$('space').textContent='Unable to read SD card status.';}}"
    "$('upload').onclick=async()=>{let fs=[...$('files').files],token=$('token').value;if(!fs.length||!token){$('status').textContent='Choose files and enter the access code.';return;}"
    "for(let f of fs){$('status').textContent=`Uploading ${f.name}…`;$('progress').value=0;"
    "await new Promise(resolve=>{let x=new XMLHttpRequest();x.open('POST','/upload?name='+encodeURIComponent(f.name));"
    "x.setRequestHeader('X-Upload-Token',token);x.upload.onprogress=e=>{if(e.lengthComputable)$('progress').value=e.loaded/e.total*100;};"
    "x.onload=()=>{if(x.status===200){$('status').textContent=x.responseText;}else{$('status').textContent=`${f.name}: ${x.responseText||x.statusText}`;}resolve();};"
    "x.onerror=()=>{ $('status').textContent=`Connection lost while uploading ${f.name}.`;resolve();};x.send(f);});}"
    "$('files').value='';await refresh();};$('finish').onclick=async()=>{let token=$('token').value;if(!token){$('status').textContent='Enter the access code first.';return;}"
    "let r=await fetch('/api/stop',{method:'POST',headers:{'X-Upload-Token':token}});$('status').textContent=r.ok?'Returning to boot menu…':'Unable to stop portal.';};"
    "$('saveNetwork').onclick=async()=>{let token=$('token').value,ssid=$('ssid').value,password=$('wifiPassword').value;if(!token||!ssid){$('status').textContent='Enter the access code and network name.';return;}"
    "let body=new URLSearchParams({ssid,password});let r=await fetch('/api/network',{method:'POST',headers:{'X-Upload-Token':token,'Content-Type':'application/x-www-form-urlencoded'},body});"
    "$('status').textContent=await r.text();if(r.ok)$('wifiPassword').value='';};refresh();</script></html>";

static void note_activity(void)
{
    if (s_portal_events) xEventGroupSetBits(s_portal_events, PORTAL_ACTIVITY_BIT);
}

static bool authorized(httpd_req_t *request)
{
    size_t length = httpd_req_get_hdr_value_len(request, "X-Upload-Token");
    if (length != strlen(s_upload_token_storage) || length >= 64) return false;
    char supplied[64];
    if (httpd_req_get_hdr_value_str(request, "X-Upload-Token", supplied,
                                    sizeof supplied) != ESP_OK) return false;
    return strcmp(supplied, s_upload_token_storage) == 0;
}

static esp_err_t index_handler(httpd_req_t *request)
{
    note_activity();
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, s_index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *request)
{
    note_activity();
    uint64_t total = 0, free_bytes = 0;
    if (esp_vfs_fat_info(SD_ROOT, &total, &free_bytes) != ESP_OK)
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "SD card status is unavailable");
    char response[128];
    int length = snprintf(response, sizeof response,
                          "{\"total\":\"%" PRIu64 "\",\"free\":\"%" PRIu64 "\"}",
                          total, free_bytes);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, length);
}

static esp_err_t send_json_string(httpd_req_t *request, const char *value)
{
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        char encoded[8];
        size_t length = 1;
        encoded[0] = (char)*p;
        if (*p == '"' || *p == '\\') {
            encoded[0] = '\\';
            encoded[1] = (char)*p;
            length = 2;
        } else if (*p < 0x20) {
            int count = snprintf(encoded, sizeof encoded, "\\u%04x", *p);
            if (count < 0) return ESP_FAIL;
            length = (size_t)count;
        }
        if (httpd_resp_send_chunk(request, encoded, length) != ESP_OK)
            return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t files_handler(httpd_req_t *request)
{
    note_activity();
    DIR *directory = opendir(SD_ROOT);
    if (!directory)
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "SD card is unavailable");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(request, "[", 1) != ESP_OK) {
        closedir(directory);
        return ESP_FAIL;
    }
    bool first = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char path[sizeof(SD_ROOT) + MAX_UPLOAD_NAME + 2];
        int count = snprintf(path, sizeof path, SD_ROOT "/%s", entry->d_name);
        if (count < 0 || (size_t)count >= sizeof path) continue;
        struct stat info;
        if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) continue;
        char prefix[48];
        count = snprintf(prefix, sizeof prefix, "%s{\"name\":\"",
                         first ? "" : ",");
        if (count < 0 || httpd_resp_send_chunk(request, prefix, count) != ESP_OK ||
            send_json_string(request, entry->d_name) != ESP_OK) {
            closedir(directory);
            return ESP_FAIL;
        }
        char suffix[48];
        count = snprintf(suffix, sizeof suffix, "\",\"size\":%" PRIu64 "}",
                         (uint64_t)info.st_size);
        if (count < 0 || httpd_resp_send_chunk(request, suffix, count) != ESP_OK) {
            closedir(directory);
            return ESP_FAIL;
        }
        first = false;
    }
    closedir(directory);
    return httpd_resp_send_chunk(request, "]", 1) == ESP_OK
        ? httpd_resp_send_chunk(request, NULL, 0) : ESP_FAIL;
}

static bool safe_filename(const char *name)
{
    size_t length = strlen(name);
    if (length == 0 || length > MAX_UPLOAD_NAME || name[0] == '.') return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (!(isalnum(ch) || ch == '.' || ch == '_' || ch == '-')) return false;
    }
    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool decode_filename(const char *query, char *name, size_t name_size)
{
    const char *value = strchr(query, '=');
    if (!value) return false;
    ++value;
    size_t out = 0;
    while (*value && *value != '&') {
        unsigned char ch = (unsigned char)*value++;
        if (ch == '%') {
            if (value[0] == '\0' || value[1] == '\0') return false;
            int high = hex_value(*value++);
            int low = hex_value(*value++);
            if (high < 0 || low < 0) return false;
            ch = (unsigned char)((high << 4) | low);
        } else if (ch == '+') {
            ch = ' ';
        }
        if (out + 1 >= name_size || ch == '\0') return false;
        name[out++] = (char)ch;
    }
    name[out] = '\0';
    return safe_filename(name);
}

static bool file_sha256_hex(const char *path, char hex[65])
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    uint8_t digest[32];
    size_t digest_length = 0;
    bool ok = psa_crypto_init() == PSA_SUCCESS &&
              psa_hash_setup(&operation, PSA_ALG_SHA_256) == PSA_SUCCESS;
    uint8_t buffer[4096];
    while (ok) {
        size_t count = fread(buffer, 1, sizeof buffer, file);
        if (count && psa_hash_update(&operation, buffer, count) != PSA_SUCCESS)
            ok = false;
        if (count) vTaskDelay(1);
        if (count < sizeof buffer) {
            if (ferror(file)) ok = false;
            break;
        }
    }
    if (ok && psa_hash_finish(&operation, digest, sizeof digest,
                              &digest_length) != PSA_SUCCESS)
        ok = false;
    if (!ok) (void)psa_hash_abort(&operation);
    fclose(file);
    if (!ok || digest_length != sizeof digest) return false;
    for (size_t i = 0; i < sizeof digest; ++i)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    hex[64] = '\0';
    return true;
}

static esp_err_t upload_handler(httpd_req_t *request)
{
    note_activity();
    if (!authorized(request))
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "Enter the access code shown on the console");
    if (request->content_len == 0 || request->content_len > MAX_UPLOAD_BYTES)
        return httpd_resp_send_err(request, HTTPD_413_CONTENT_TOO_LARGE,
                                   "File is empty or exceeds the 1 GiB limit");
    size_t query_length = httpd_req_get_url_query_len(request);
    if (query_length == 0 || query_length > 300)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "A valid filename is required");
    char query[301], name[MAX_UPLOAD_NAME + 1];
    if (httpd_req_get_url_query_str(request, query, sizeof query) != ESP_OK ||
        strncmp(query, "name=", 5) != 0 || !decode_filename(query, name, sizeof name))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Filename must use letters, numbers, dot, dash, or underscore");

    if (!s_upload_lock || xSemaphoreTake(s_upload_lock, 0) != pdTRUE) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "Another upload is in progress");
    }

    char destination[sizeof(SD_ROOT) + MAX_UPLOAD_NAME + 2];
    snprintf(destination, sizeof destination, SD_ROOT "/%s", name);
    struct stat existing;
    if (stat(destination, &existing) == 0) {
        xSemaphoreGive(s_upload_lock);
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "A file with that name already exists");
    }

    uint64_t total = 0, free_bytes = 0;
    if (esp_vfs_fat_info(SD_ROOT, &total, &free_bytes) != ESP_OK ||
        free_bytes < MIN_FREE_BYTES ||
        (uint64_t)request->content_len > free_bytes - MIN_FREE_BYTES) {
        xSemaphoreGive(s_upload_lock);
        return httpd_resp_send_err(request, HTTPD_413_CONTENT_TOO_LARGE,
                                   "Not enough free space for this upload");
    }

    char temporary[sizeof(SD_ROOT) + 40];
    int fd = -1;
    for (unsigned attempt = 0; attempt < 8 && fd < 0; ++attempt) {
        snprintf(temporary, sizeof temporary, SD_ROOT "/.fuzzball-upload-%08" PRIx32 ".part",
                 esp_random());
        fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    }
    if (fd < 0) {
        xSemaphoreGive(s_upload_lock);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Unable to create a temporary file on the SD card");
    }
    FILE *file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        unlink(temporary);
        xSemaphoreGive(s_upload_lock);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Unable to open the temporary file");
    }

    char buffer[4096];
    size_t remaining = request->content_len;
    bool failed = false;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof buffer ? remaining : sizeof buffer;
        int received = httpd_req_recv(request, buffer, chunk);
        if (received <= 0 || fwrite(buffer, 1, (size_t)received, file) != (size_t)received) {
            failed = true;
            break;
        }
        remaining -= (size_t)received;
        note_activity();
        vTaskDelay(1);
    }
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) failed = true;
    if (fclose(file) != 0) failed = true;

    struct stat uploaded;
    if (!failed && (stat(temporary, &uploaded) != 0 ||
                    (uint64_t)uploaded.st_size != (uint64_t)request->content_len))
        failed = true;
    char sha256[65] = {0};
    if (!failed && !file_sha256_hex(temporary, sha256)) failed = true;
    if (!failed && rename(temporary, destination) != 0) failed = true;
    if (failed) {
        unlink(temporary);
        xSemaphoreGive(s_upload_lock);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Upload failed; the incomplete file was discarded");
    }

    xSemaphoreGive(s_upload_lock);
    ESP_LOGI(TAG, "Uploaded %s (%" PRIu64 " bytes)", name,
             (uint64_t)uploaded.st_size);
    char response[192];
    snprintf(response, sizeof response,
             "Stored %s (%" PRIu64 " bytes, SHA-256 %s)", name,
             (uint64_t)uploaded.st_size, sha256);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, response);
}

static esp_err_t stop_handler(httpd_req_t *request)
{
    if (!authorized(request))
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "Enter the access code shown on the console");
    esp_err_t result = httpd_resp_sendstr(request, "Portal stopped");
    note_activity();
    if (s_portal_events) xEventGroupSetBits(s_portal_events, PORTAL_STOP_BIT);
    return result;
}

static bool decode_form_value(const char *body, const char *key,
                              char *output, size_t output_size)
{
    size_t key_length = strlen(key);
    const char *field = body;
    while (*field) {
        const char *end = strchr(field, '&');
        size_t field_length = end ? (size_t)(end - field) : strlen(field);
        if (field_length > key_length && field[key_length] == '=' &&
            memcmp(field, key, key_length) == 0) {
            const char *value = field + key_length + 1;
            size_t out = 0;
            while (value < field + field_length) {
                unsigned char ch = (unsigned char)*value++;
                if (ch == '%') {
                    if (value + 1 >= field + field_length) return false;
                    int high = hex_value(*value++);
                    int low = hex_value(*value++);
                    if (high < 0 || low < 0) return false;
                    ch = (unsigned char)((high << 4) | low);
                } else if (ch == '+') {
                    ch = ' ';
                }
                if (ch == '\0' || out + 1 >= output_size) return false;
                output[out++] = (char)ch;
            }
            output[out] = '\0';
            return true;
        }
        if (!end) break;
        field = end + 1;
    }
    return false;
}

static esp_err_t network_handler(httpd_req_t *request)
{
    note_activity();
    if (!authorized(request))
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "Enter the access code shown on the console");
    if (request->content_len <= 0 || request->content_len > NETWORK_BODY_MAX)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Invalid network settings");
    char body[NETWORK_BODY_MAX + 1];
    int received = httpd_req_recv(request, body, request->content_len);
    if (received != request->content_len)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Could not read network settings");
    body[received] = '\0';

    char ssid[33] = {0};
    char password[64] = {0};
    if (!decode_form_value(body, "ssid", ssid, sizeof ssid) ||
        !decode_form_value(body, "password", password, sizeof password) ||
        strlen(ssid) == 0 || strlen(ssid) > 32 ||
        (strlen(password) != 0 && strlen(password) < 8)) {
        memset(password, 0, sizeof password);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "SSID is required; password must be empty or at least 8 characters");
    }

    esp_err_t result = sd_upload_portal_save_wifi_credentials(ssid, password);
    memset(password, 0, sizeof password);
    if (result != ESP_OK)
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Could not save Wi-Fi settings");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request,
        "Wi-Fi saved. Finish and return, then press W again to join this network.");
}

static const httpd_uri_t s_index_uri = {
    .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL
};
static const httpd_uri_t s_status_uri = {
    .uri = "/api/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL
};
static const httpd_uri_t s_files_uri = {
    .uri = "/api/files", .method = HTTP_GET, .handler = files_handler, .user_ctx = NULL
};
static const httpd_uri_t s_upload_uri = {
    .uri = "/upload", .method = HTTP_POST, .handler = upload_handler, .user_ctx = NULL
};
static const httpd_uri_t s_stop_uri = {
    .uri = "/api/stop", .method = HTTP_POST, .handler = stop_handler, .user_ctx = NULL
};
static const httpd_uri_t s_network_uri = {
    .uri = "/api/network", .method = HTTP_POST, .handler = network_handler, .user_ctx = NULL
};

static esp_err_t start_wifi_ap(char *ssid, size_t ssid_size,
                               char *password, size_t password_size)
{
    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (!s_ap_netif) return ESP_FAIL;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init_config);
    if (result != ESP_OK) return result;
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) goto fail_deinit;

    uint32_t suffix = esp_random() & 0x00ffffffU;
    uint32_t pass_a = esp_random();
    uint32_t pass_b = esp_random();
    snprintf(ssid, ssid_size, "Fuzzball-SD-%06" PRIx32, suffix);
    snprintf(password, password_size, "%08" PRIx32 "%04" PRIx32,
             pass_a, pass_b & 0xffffU);

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.ap.ssid, sizeof wifi_config.ap.ssid, "%s", ssid);
    snprintf((char *)wifi_config.ap.password, sizeof wifi_config.ap.password,
             "%s", password);
    wifi_config.ap.ssid_len = strlen(ssid);
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 2;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (result != ESP_OK) goto fail_deinit;
    result = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (result != ESP_OK) goto fail_deinit;
    result = esp_wifi_start();
    if (result != ESP_OK) goto fail_deinit;
    return ESP_OK;

fail_deinit:
    (void)esp_wifi_deinit();
    return result;
}

static void portal_wifi_event(void *arg, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        esp_netif_ip_info_t ip_info;
        if (s_sta_netif && esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) {
            snprintf(s_network_ip, sizeof s_network_ip, IPSTR, IP2STR(&ip_info.ip));
        }
        if (s_portal_events) xEventGroupSetBits(s_portal_events, PORTAL_GOT_IP_BIT);
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        (void)esp_wifi_connect();
        if (s_portal_events) xEventGroupSetBits(s_portal_events, PORTAL_DISCONNECTED_BIT);
    }
}

static bool load_wifi_credentials(char *ssid, size_t ssid_size,
                                  char *password, size_t password_size)
{
    nvs_handle_t handle;
    if (nvs_open("sdportal", NVS_READONLY, &handle) != ESP_OK) return false;
    size_t ssid_length = ssid_size;
    size_t password_length = password_size;
    esp_err_t ssid_result = nvs_get_str(handle, "ssid", ssid, &ssid_length);
    esp_err_t password_result = nvs_get_str(handle, "password", password,
                                             &password_length);
    nvs_close(handle);
    if (ssid_result != ESP_OK || password_result != ESP_OK ||
        ssid_length <= 1 || ssid_length > ssid_size ||
        password_length > password_size) {
        memset(password, 0, password_size);
        return false;
    }
    return true;
}

esp_err_t sd_upload_portal_save_wifi_credentials(const char *ssid,
                                                 const char *password)
{
    if (!ssid || !password) return ESP_ERR_INVALID_ARG;
    size_t ssid_length = strlen(ssid);
    size_t password_length = strlen(password);
    if (ssid_length == 0 || ssid_length > 32 ||
        (password_length != 0 && (password_length < 8 || password_length > 63)))
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t result = nvs_open("sdportal", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_str(handle, "ssid", ssid);
    if (result == ESP_OK) result = nvs_set_str(handle, "password", password);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}

esp_err_t sd_upload_portal_clear_wifi_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open("sdportal", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    esp_err_t ssid_result = nvs_erase_key(handle, "ssid");
    esp_err_t password_result = nvs_erase_key(handle, "password");
    if (ssid_result != ESP_OK && ssid_result != ESP_ERR_NVS_NOT_FOUND)
        result = ssid_result;
    else if (password_result != ESP_OK && password_result != ESP_ERR_NVS_NOT_FOUND)
        result = password_result;
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}

static esp_err_t start_wifi_sta(const char *ssid, const char *password)
{
    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_FAIL;

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init_config);
    if (result != ESP_OK) goto fail_netif;
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) goto fail_deinit;
    result = esp_event_handler_instance_register(WIFI_EVENT,
        WIFI_EVENT_STA_DISCONNECTED, portal_wifi_event, NULL, &s_wifi_handler);
    if (result != ESP_OK) goto fail_deinit;
    result = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
        portal_wifi_event, NULL, &s_ip_handler);
    if (result != ESP_OK) goto fail_handlers;

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
    snprintf((char *)wifi_config.sta.password, sizeof wifi_config.sta.password,
             "%s", password);
    wifi_config.sta.threshold.authmode = password[0]
        ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) goto fail_handlers;
    result = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (result != ESP_OK) goto fail_handlers;
    result = esp_wifi_start();
    if (result != ESP_OK) goto fail_handlers;
    s_wifi_is_sta = true;
    result = esp_wifi_connect();
    if (result != ESP_OK) goto fail_started;

    EventBits_t bits = xEventGroupWaitBits(s_portal_events, PORTAL_GOT_IP_BIT,
        pdFALSE, pdTRUE, pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (bits & PORTAL_GOT_IP_BIT) return ESP_OK;
    result = ESP_ERR_TIMEOUT;

fail_started:
    (void)esp_wifi_stop();
    s_wifi_is_sta = false;
fail_handlers:
    if (s_ip_handler) {
        (void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                     s_ip_handler);
        s_ip_handler = NULL;
    }
    if (s_wifi_handler) {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT,
            WIFI_EVENT_STA_DISCONNECTED, s_wifi_handler);
        s_wifi_handler = NULL;
    }
fail_deinit:
    (void)esp_wifi_deinit();
fail_netif:
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    return result;
}

static void stop_wifi(void)
{
    if (s_wifi_is_sta) {
        (void)esp_wifi_stop();
        s_wifi_is_sta = false;
    } else {
        (void)esp_wifi_stop();
    }
    if (s_ip_handler) {
        (void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                     s_ip_handler);
        s_ip_handler = NULL;
    }
    if (s_wifi_handler) {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT,
            WIFI_EVENT_STA_DISCONNECTED, s_wifi_handler);
        s_wifi_handler = NULL;
    }
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();
    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
}

static void remove_stale_uploads(void)
{
    DIR *directory = opendir(SD_ROOT);
    if (!directory) return;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strncmp(entry->d_name, ".fuzzball-upload-", 17) != 0) continue;
        char path[sizeof(SD_ROOT) + 40];
        int length = snprintf(path, sizeof path, SD_ROOT "/%s", entry->d_name);
        if (length < 0 || (size_t)length >= sizeof path) continue;
        struct stat info;
        if (stat(path, &info) == 0 && S_ISREG(info.st_mode)) (void)unlink(path);
    }
    closedir(directory);
}

esp_err_t sd_upload_portal_run(sd_upload_cancel_fn cancel_requested)
{
    char ssid[33] = {0}, password[64] = {0};
    bool station_mode = false;
    uint32_t token[4];
    esp_err_t result;
    httpd_handle_t server = NULL;
    uint64_t total = 0, free_bytes = 0;

    if (s_portal_events) return ESP_ERR_INVALID_STATE;
    result = esp_vfs_fat_info(SD_ROOT, &total, &free_bytes);
    if (result != ESP_OK) {
        puts("SD upload portal unavailable: the SD card is not mounted.");
        return result;
    }
    s_portal_events = xEventGroupCreate();
    s_upload_lock = xSemaphoreCreateMutex();
    if (!s_portal_events || !s_upload_lock) {
        result = ESP_ERR_NO_MEM;
        goto done;
    }

    if (load_wifi_credentials(ssid, sizeof ssid, password, sizeof password)) {
        printf("Joining saved Wi-Fi network: %s\n", ssid);
        result = start_wifi_sta(ssid, password);
        memset(password, 0, sizeof password);
        if (result == ESP_OK) {
            station_mode = true;
            printf("SD portal connected. Browser: http://%s/\n", s_network_ip);
        } else {
            printf("Could not join saved Wi-Fi network; starting setup access point.\n");
        }
    }
    if (!station_mode) {
        result = start_wifi_ap(ssid, sizeof ssid, password, sizeof password);
        if (result != ESP_OK) goto stop_ap;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 6;
    config.max_open_sockets = 4;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;
    config.lru_purge_enable = true;
    result = httpd_start(&server, &config);
    if (result != ESP_OK) goto stop_ap;

    if ((result = httpd_register_uri_handler(server, &s_index_uri)) != ESP_OK ||
        (result = httpd_register_uri_handler(server, &s_status_uri)) != ESP_OK ||
        (result = httpd_register_uri_handler(server, &s_files_uri)) != ESP_OK ||
        (result = httpd_register_uri_handler(server, &s_upload_uri)) != ESP_OK ||
        (result = httpd_register_uri_handler(server, &s_stop_uri)) != ESP_OK ||
        (result = httpd_register_uri_handler(server, &s_network_uri)) != ESP_OK)
        goto stop_server;

    for (size_t i = 0; i < 4; ++i) token[i] = esp_random();
    snprintf(s_upload_token_storage, sizeof s_upload_token_storage,
             "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32,
             token[0], token[1], token[2], token[3]);
    remove_stale_uploads();
    printf("\nSD upload portal is active.\n");
    if (station_mode) {
        printf("  Wi-Fi network: %s\n  Browser: http://%s/\n",
               ssid, s_network_ip);
    } else {
        printf("  Setup Wi-Fi: %s\n  Password: %s\n"
               "  Browser: http://192.168.4.1/\n", ssid, password);
    }
    printf("  Upload access code: %s\n  SD free space: %" PRIu64 " bytes\n"
           "Press Q here or choose Finish and return in the browser.\n",
           s_upload_token_storage, free_bytes);

    int64_t last_activity = esp_timer_get_time();
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_portal_events,
                                               PORTAL_STOP_BIT | PORTAL_ACTIVITY_BIT,
                                               pdTRUE, pdFALSE,
                                               pdMS_TO_TICKS(100));
        if (bits & PORTAL_STOP_BIT) break;
        if (bits & PORTAL_ACTIVITY_BIT) last_activity = esp_timer_get_time();
        if (cancel_requested && cancel_requested()) break;
        if (esp_timer_get_time() - last_activity >= IDLE_TIMEOUT_US) {
            puts("SD upload portal stopped after ten minutes without activity.");
            break;
        }
    }

stop_server:
    if (server) httpd_stop(server);
stop_ap:
    stop_wifi();
done:
    memset(password, 0, sizeof password);
    memset(s_upload_token_storage, 0, sizeof s_upload_token_storage);
    if (s_upload_lock) vSemaphoreDelete(s_upload_lock);
    s_upload_lock = NULL;
    if (s_portal_events) vEventGroupDelete(s_portal_events);
    s_portal_events = NULL;
    puts("SD upload portal stopped.");
    return result;
}
