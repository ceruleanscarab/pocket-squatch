#include "wifisetup.hpp"

#include <algorithm>
#include <cctype>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "cloud.hpp"
#include "netjoin.hpp"
#include "radio.hpp"
#include "storage.hpp"
#include "townlookup.hpp"

namespace wifisetup {

namespace {

const char* TAG = "wifisetup";
constexpr int kMaxNetworks = 15;
constexpr uint32_t kTimeoutMs = 10 * 60 * 1000;

std::atomic<State> gState{State::Idle};
std::atomic<bool> gCancel{false}, gGotIt{false};
char gHotspot[32];
char gSsid[33], gPass[65];
char gTown[64], gTownFound[40];
char gRestore[17];  // a backup code typed in, tidied ("" for none)
std::atomic<bool> gBedtimeChanged{false};
std::atomic<int> gPhones{0};  // phones joined to the hotspot
std::atomic<int> gRestoreResult{0};  // 1 brought back, -1 not found
char gNetworks[kMaxNetworks][33];
int gNetworkCount = 0;
TaskHandle_t gDnsTask = nullptr;
int gDnsSock = -1;

// ---- the page

void appendEscaped(char* out, size_t n, const char* s) {  // for HTML text and attribute values
    size_t len = std::strlen(out);
    for (; *s && len + 7 < n; s++) {
        const char* rep = nullptr;
        switch (*s) {
            case '&': rep = "&amp;"; break;
            case '<': rep = "&lt;"; break;
            case '>': rep = "&gt;"; break;
            case '"': rep = "&quot;"; break;
            default: break;
        }
        if (rep) {
            std::strcpy(out + len, rep);
            len += std::strlen(rep);
        } else {
            out[len++] = *s;
            out[len] = '\0';
        }
    }
}

#if CONFIG_SQUATCH_PET_HORSE
// The illustrated guide, built into the app (firmware/main/guide/, EMBED_FILES in CMakeLists.txt).
extern const uint8_t kGuideStart[] asm("_binary_pocket_pony_guide_pdf_start");
extern const uint8_t kGuideEnd[] asm("_binary_pocket_pony_guide_pdf_end");

esp_err_t guideGet(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/pdf");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=\"pocket-pony-guide.pdf\"");
    const uint8_t* at = kGuideStart;
    while (at < kGuideEnd) {  // in pieces, so a slow phone doesn't stall one huge send
        const size_t n = std::min<size_t>(8192, kGuideEnd - at);
        if (httpd_resp_send_chunk(req, reinterpret_cast<const char*>(at), n) != ESP_OK) return ESP_FAIL;
        at += n;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}
#endif

esp_err_t redirect(httpd_req_t* req, httpd_err_code_t);

// The captive-portal API (RFC 8908), whose address the hotspot hands out with DHCP option 114:
// a phone that reads it is told plainly that there's a page to sign in on, and where.
esp_err_t capportGet(httpd_req_t* req) {
    ESP_LOGI(TAG, "phone asked the portal API: told it the setup page is at 192.168.4.1");
    httpd_resp_set_type(req, "application/captive+json");
    httpd_resp_set_hdr(req, "Cache-Control", "private");
    return httpd_resp_sendstr(req, "{\"captive\": true, \"user-portal-url\": \"http://192.168.4.1/\"}");
}

esp_err_t pageGet(httpd_req_t* req) {
    // Asked for under some other name (a phone checking for internet, say www.google.com):
    // redirect, which is how a phone knows there's a page to sign in on and offers it.
    char host[64] = "";
    httpd_req_get_hdr_value_str(req, "Host", host, sizeof host);
    if (host[0] && std::strcmp(host, "192.168.4.1") != 0 && std::strcmp(host, "192.168.4.1:80") != 0)
        return redirect(req, HTTPD_404_NOT_FOUND);
    ESP_LOGI(TAG, "setup page sent to a phone");
    static char html[9216];
#if CONFIG_SQUATCH_PET_HORSE
    static char line2[640];
#endif
    std::snprintf(html, sizeof html,
                  "<!doctype html><html><head><meta charset=utf-8>"
                  "<meta name=viewport content='width=device-width,initial-scale=1'><title>%s</title>"
                  "<style>body{font-family:sans-serif;background:#f4ece0;color:#3b2a1e;margin:0;padding:20px}"
                  "h1{font-size:1.4em}label{display:block;margin:14px 0 6px;font-weight:bold}"
                  "select,input{width:100%%;box-sizing:border-box;font-size:1.1em;padding:12px;border:2px solid #b8a98f;"
                  "border-radius:10px;background:#fff}button{margin-top:20px;width:100%%;font-size:1.2em;padding:14px;"
                  "border:0;border-radius:12px;background:#6e8f5a;color:#fff}small{color:#8a7a66}</style></head><body>"
                  "<h1>Wi-Fi for %s</h1><p>Pick your home network and type its password. The toy uses it "
                  "to set its clock and check the weather.</p>",
                  gHotspot, gHotspot);
#if CONFIG_SQUATCH_PET_HORSE
    std::snprintf(line2, sizeof line2,
                  "<p style='background:#fff;border-radius:10px;padding:12px;margin:0 0 4px'>&#128214; "
                  "<a href=/guide.pdf style='color:#9e2b23;font-weight:bold'>The Pocket Pony guide</a> "
                  "(PDF, %u KB): how to look after your pony, the games, and more. Save it to your phone first: "
                  "this page closes once the toy joins your Wi-Fi.<br><small>If it doesn't open here, open your web browser and go to "
                  "<b>192.168.4.1/guide.pdf</b> while the phone is still on %s.</small></p>",
                  static_cast<unsigned>((kGuideEnd - kGuideStart + 1023) / 1024), gHotspot);
    std::strncat(html, line2, sizeof html - std::strlen(html) - 1);
#endif
    if (cloud::enabled()) {  // its backup code, for safe keeping
        char code[24], line[384];
        cloud::code(code, sizeof code);
        std::snprintf(line, sizeof line,
                      "<p style='background:#fff;border-radius:10px;padding:10px'>This pet's <b>backup code</b>: "
                      "<b style='font-size:1.2em;letter-spacing:1px'>%s</b><br><small>Take a photo of it or write it "
                      "down. It brings the pet back if the toy is ever reset or replaced.</small></p>",
                      code);
        std::strncat(html, line, sizeof html - std::strlen(html) - 1);
    }
    std::strncat(html, "<form method=post action=/save><label>Network</label><select name=ssid>",
                 sizeof html - std::strlen(html) - 1);
    for (int i = 0; i < gNetworkCount; i++) {
        std::strncat(html, "<option value=\"", sizeof html - std::strlen(html) - 1);
        appendEscaped(html, sizeof html, gNetworks[i]);
        std::strncat(html, "\">", sizeof html - std::strlen(html) - 1);
        appendEscaped(html, sizeof html, gNetworks[i]);
        std::strncat(html, "</option>", sizeof html - std::strlen(html) - 1);
    }
    std::strncat(html,
                 "</select><label>Or type its name</label><input name=other placeholder='(only if it isn&#39;t listed)' "
                 "autocapitalize=off autocorrect=off><label>Password</label>"
                 "<input name=pass type=password id=p autocapitalize=off autocorrect=off>"
                 "<small><input type=checkbox style='width:auto' onclick=\"p.type=this.checked?'text':'password'\"> "
                 "show the password. Already set up? Leave it empty to keep the saved one.</small>"
                 "<label>Your town</label><input name=town placeholder='e.g. Colorado Springs' autocorrect=off>"
                 "<small>For the weather and the clock. Leave it empty to keep what's set.</small>",
                 sizeof html - std::strlen(html) - 1);
    {  // bedtime: when the pet goes to sleep and wakes (and when the toy keeps quiet)
        int bed = 21, wake = 7;
        storage::bedtime(bed, wake);
        static const char* kBed[] = {"7 pm", "8 pm", "9 pm", "10 pm", "11 pm"};
        static const char* kWake[] = {"5 am", "6 am", "7 am", "8 am", "9 am"};
        char opt[96];
        std::strncat(html, "<label>Bedtime</label><select name=bed>", sizeof html - std::strlen(html) - 1);
        for (int i = 0; i < 5; i++) {
            std::snprintf(opt, sizeof opt, "<option value=%d%s>%s</option>", 19 + i, 19 + i == bed ? " selected" : "", kBed[i]);
            std::strncat(html, opt, sizeof html - std::strlen(html) - 1);
        }
        std::strncat(html, "</select><label>Wake-up time</label><select name=wake>", sizeof html - std::strlen(html) - 1);
        for (int i = 0; i < 5; i++) {
            std::snprintf(opt, sizeof opt, "<option value=%d%s>%s</option>", 5 + i, 5 + i == wake ? " selected" : "", kWake[i]);
            std::strncat(html, opt, sizeof html - std::strlen(html) - 1);
        }
        std::strncat(html, "</select><small>The pet sleeps from bedtime until it wakes up, and the toy keeps quiet "
                           "then.</small>", sizeof html - std::strlen(html) - 1);
    }
    if (cloud::enabled())
        std::strncat(html,
                     "<label>Bring back a pet (optional)</label><input name=restore placeholder='its backup code' "
                     "autocapitalize=characters autocorrect=off><small>Only to restore a pet from a backup: it "
                     "<b>replaces</b> the pet on this toy.</small>",
                     sizeof html - std::strlen(html) - 1);
    std::strncat(html, "<button>Save</button></form>", sizeof html - std::strlen(html) - 1);
    std::strncat(html, "</body></html>", sizeof html - std::strlen(html) - 1);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

void urlDecode(char* s) {
    char* o = s;
    for (; *s; s++) {
        if (*s == '+') {
            *o++ = ' ';
        } else if (*s == '%' && std::isxdigit(static_cast<unsigned char>(s[1])) && std::isxdigit(static_cast<unsigned char>(s[2]))) {
            char hex[3] = {s[1], s[2], 0};
            *o++ = static_cast<char>(std::strtol(hex, nullptr, 16));
            s += 2;
        } else {
            *o++ = *s;
        }
    }
    *o = '\0';
}

esp_err_t saveposted(httpd_req_t* req) {
    // Room for every field even when a phone percent-encodes it (a symbol is 3 bytes: %21).
    static char body[1536];
    std::memset(body, 0, sizeof body);
    const int len = httpd_req_recv(req, body, std::min<size_t>(req->content_len, sizeof body - 1));
    if (len <= 0) return httpd_resp_send_500(req);
    body[len] = '\0';
    char ssid[100] = {}, other[100] = {}, pass[200] = {}, town[200] = {};
    httpd_query_key_value(body, "town", town, sizeof town);
    urlDecode(town);
    std::snprintf(gTown, sizeof gTown, "%s", town);
    char bed[8] = {}, wake[8] = {};
    httpd_query_key_value(body, "bed", bed, sizeof bed);
    httpd_query_key_value(body, "wake", wake, sizeof wake);
    if (bed[0] && wake[0]) {
        const int b = std::atoi(bed), w = std::atoi(wake);
        int oldB = 21, oldW = 7;
        const bool had = storage::bedtime(oldB, oldW);
        if (b >= 19 && b <= 23 && w >= 5 && w <= 9 && (!had || b != oldB || w != oldW)) {
            storage::saveBedtime(b, w);
            gBedtimeChanged = true;
        }
    }
    char restore[48] = {};
    httpd_query_key_value(body, "restore", restore, sizeof restore);
    urlDecode(restore);
    if (restore[0] && !cloud::tidy(restore, gRestore))
        return httpd_resp_send(req, "That backup code doesn't look right: it's 16 letters and numbers. Go back and check it.",
                               HTTPD_RESP_USE_STRLEN);
    httpd_query_key_value(body, "ssid", ssid, sizeof ssid);
    httpd_query_key_value(body, "other", other, sizeof other);
    httpd_query_key_value(body, "pass", pass, sizeof pass);
    urlDecode(ssid);
    urlDecode(other);
    urlDecode(pass);
    const char* chosen = other[0] ? other : ssid;
    if (!chosen[0]) return httpd_resp_send(req, "Pick a network first.", HTTPD_RESP_USE_STRLEN);
    char savedSsid[33], savedPass[65];  // the same network again with no password: keep the saved one
    if (!pass[0] && storage::wifiCredentials(savedSsid, sizeof savedSsid, savedPass, sizeof savedPass) &&
        std::strcmp(savedSsid, chosen) == 0)
        std::snprintf(pass, sizeof pass, "%s", savedPass);
    std::snprintf(gSsid, sizeof gSsid, "%s", chosen);
    std::snprintf(gPass, sizeof gPass, "%s", pass);
    gGotIt = true;
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req,
                           "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                           "<body style='font-family:sans-serif;background:#f4ece0;color:#3b2a1e;padding:20px'>"
                           "<h1>Saved!</h1><p>The toy is trying your network now: look at its screen. "
                           "You can close this page.</p></body>",
                           HTTPD_RESP_USE_STRLEN);
}

// Anything else a phone asks for (its captive-portal checks): send it to the page.
esp_err_t redirect(httpd_req_t* req, httpd_err_code_t) {
    char host[64] = "?";
    httpd_req_get_hdr_value_str(req, "Host", host, sizeof host);
    ESP_LOGI(TAG, "phone asked for http://%s%s: sent to the setup page", host, req->uri);  // its captive-portal checks
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, nullptr, 0);
}

// ---- a DNS server that answers every name with the board's own address

void dnsTask(void*) {
    gDnsSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (gDnsSock < 0 || bind(gDnsSock, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        ESP_LOGW(TAG, "no DNS: the page has to be opened at 192.168.4.1");
        if (gDnsSock >= 0) close(gDnsSock);
        gDnsSock = -1;
        gDnsTask = nullptr;
        vTaskDelete(nullptr);
    }
    uint8_t buf[512];
    for (;;) {
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        const int n = recvfrom(gDnsSock, buf, sizeof buf - 16, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n < 0) break;  // the socket was closed: we're done
        if (n < 12 || (buf[2] & 0x80)) continue;  // too short, or not a question
        // Find the end of the question (name, type, class) and add one A record after it.
        int p = 12;
        while (p < n && buf[p]) p += buf[p] + 1;
        if (p + 5 > n) continue;
        const bool wantsA = buf[p + 1] == 0 && buf[p + 2] == 1;  // an IPv4 address (not IPv6 and the like)
        if (wantsA) {  // which names the phone looks up, for the log
            char name[64];
            int o = 0;
            for (int q = 12; q < p && buf[q] && o < 60;) {
                const int len = buf[q++];
                if (o) name[o++] = '.';
                for (int k = 0; k < len && q < p && o < 62; k++) name[o++] = static_cast<char>(buf[q++]);
            }
            name[o] = '\0';
            ESP_LOGI(TAG, "phone looked up %s", name);
        }
        p += 5;
        buf[2] = 0x81;  // a response, recursion desired
        buf[3] = 0x80;  // recursion available, no error
        buf[6] = 0;     // one answer, or none for anything but an address
        buf[7] = wantsA ? 1 : 0;
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        const uint8_t answer[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
        if (wantsA) std::memcpy(buf + p, answer, sizeof answer);
        sendto(gDnsSock, buf, p + (wantsA ? sizeof answer : 0), 0, reinterpret_cast<sockaddr*>(&from), fl);
    }
    gDnsTask = nullptr;
    vTaskDelete(nullptr);
}

// ---- the whole visit

void setupTask(void*) {
    radio::take();
    esp_netif_t* ap = esp_netif_create_default_wifi_ap();
    esp_netif_t* sta = esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    wifi_config_t cfg{};
    std::snprintf(reinterpret_cast<char*>(cfg.ap.ssid), sizeof cfg.ap.ssid, "%s", gHotspot);
    cfg.ap.ssid_len = static_cast<uint8_t>(std::strlen(gHotspot));
    cfg.ap.channel = 1;
    cfg.ap.max_connection = 2;
    cfg.ap.authmode = WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_AP, &cfg);
    esp_wifi_start();
    {  // tell phones straight away there's a page to open (DHCP option 114, RFC 8910)
        static char uri[] = "http://192.168.4.1/portal-api";
        esp_netif_dhcps_stop(ap);
        const esp_err_t e = esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, uri, std::strlen(uri));
        esp_netif_dhcps_start(ap);
        ESP_LOGI(TAG, "page address handed out with DHCP: %s", esp_err_to_name(e));
    }

    // The networks it can hear, strongest first, each once.
    gNetworkCount = 0;
    wifi_scan_config_t scan{};
    if (esp_wifi_scan_start(&scan, true) == ESP_OK) {
        uint16_t found = 30;
        static wifi_ap_record_t recs[30];
        esp_wifi_scan_get_ap_records(&found, recs);
        std::sort(recs, recs + found, [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) { return a.rssi > b.rssi; });
        for (int i = 0; i < found && gNetworkCount < kMaxNetworks; i++) {
            const char* name = reinterpret_cast<const char*>(recs[i].ssid);
            if (!name[0]) continue;
            bool seen = false;
            for (int k = 0; k < gNetworkCount; k++) seen |= std::strcmp(gNetworks[k], name) == 0;
            if (!seen) std::snprintf(gNetworks[gNetworkCount++], 33, "%s", name);
        }
    }

    httpd_handle_t server = nullptr;
    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.lru_purge_enable = true;
    hc.stack_size = 8192;
    if (httpd_start(&server, &hc) == ESP_OK) {
        const httpd_uri_t page{"/", HTTP_GET, pageGet, nullptr};
        const httpd_uri_t save{"/save", HTTP_POST, saveposted, nullptr};
        httpd_register_uri_handler(server, &page);
        httpd_register_uri_handler(server, &save);
        const httpd_uri_t capport{"/portal-api", HTTP_GET, capportGet, nullptr};
        httpd_register_uri_handler(server, &capport);
#if CONFIG_SQUATCH_PET_HORSE
        const httpd_uri_t guide{"/guide.pdf", HTTP_GET, guideGet, nullptr};
        httpd_register_uri_handler(server, &guide);
#endif
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect);
    }
    xTaskCreatePinnedToCore(dnsTask, "dns", 4096, nullptr, 3, &gDnsTask, 0);
    ESP_LOGI(TAG, "hotspot \"%s\" up, %d networks heard", gHotspot, gNetworkCount);

    const TickType_t until = xTaskGetTickCount() + pdMS_TO_TICKS(kTimeoutMs);
    while (!gGotIt && !gCancel && xTaskGetTickCount() < until) {
        vTaskDelay(pdMS_TO_TICKS(200));
        wifi_sta_list_t list{};
        const int phones = esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;  // so the screen can follow along
        if (phones != gPhones) ESP_LOGI(TAG, "%d phone(s) on the hotspot", phones);
        gPhones = phones;
    }
    gPhones = 0;
    vTaskDelay(pdMS_TO_TICKS(gGotIt ? 1500 : 0));  // let the "Saved!" page reach the phone

    if (gDnsSock >= 0) {
        shutdown(gDnsSock, SHUT_RDWR);
        close(gDnsSock);
        gDnsSock = -1;
    }
    for (int i = 0; i < 20 && gDnsTask; i++) vTaskDelay(pdMS_TO_TICKS(50));
    if (server) httpd_stop(server);
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(ap);
    esp_netif_destroy_default_wifi(sta);

    if (gGotIt) {  // try it, so the board can say whether it worked
        storage::saveWifi(gSsid, gPass);
        gState = State::Checking;
        const bool ok = netjoin::join(gSsid, gPass);
        if (ok && gRestore[0]) {  // bring a pet back from its backup, while it's on the network
            squatch::State pet;
            if (cloud::fetchJoined(gRestore, pet)) {
                cloud::offerRestored(pet, gRestore);
                gRestoreResult = 1;
            } else {
                gRestoreResult = -1;
            }
        }
        if (ok && gTown[0]) {  // while it's on the network
            storage::Location here;
            if (townlookup::find(gTown, here)) std::snprintf(gTownFound, sizeof gTownFound, "%s", here.name);
            else std::snprintf(gTownFound, sizeof gTownFound, "TOWN NOT FOUND");
        }
        netjoin::leave();
        gState = ok ? State::Joined : State::Failed;
        ESP_LOGI(TAG, "\"%s\": %s", gSsid, ok ? "joined" : "couldn't join");
    } else {
        gState = State::Idle;
    }
    radio::give();
    vTaskDelete(nullptr);
}

}  // namespace

void start(const char* hotspotName) {
    if (gState == State::Running || gState == State::Checking) return;
    std::snprintf(gHotspot, sizeof gHotspot, "%s", hotspotName);
    gCancel = gGotIt = false;
    gTown[0] = gTownFound[0] = gRestore[0] = '\0';
    gRestoreResult = 0;
    gState = State::Running;
    if (xTaskCreatePinnedToCore(setupTask, "wifisetup", 6144, nullptr, 3, nullptr, 0) != pdPASS) gState = State::Idle;
}

void cancel() { gCancel = true; }

State state() { return gState; }

void network(char* out, size_t n) { std::snprintf(out, n, "%s", gSsid); }

void town(char* out, size_t n) { std::snprintf(out, n, "%s", gTownFound); }

int restoreResult() { return gRestoreResult; }

bool takeBedtimeChanged() { return gBedtimeChanged.exchange(false); }

int phonesJoined() { return gPhones; }

}  // namespace wifisetup
