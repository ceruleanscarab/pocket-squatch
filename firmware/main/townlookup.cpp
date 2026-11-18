#include "townlookup.hpp"

#include <cstdio>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "squatch/weather.hpp"
#include "walltime.hpp"

namespace townlookup {

namespace {
const char* TAG = "town";
char gBody[2048];
}  // namespace

bool find(const char* town, storage::Location& out) {
    if (!town || !town[0]) return false;
    char q[200] = {}, url[300];
    size_t n = 0;
    for (const char* p = town; *p && n + 4 < sizeof q; p++) {  // URL-encode it
        const unsigned char ch = static_cast<unsigned char>(*p);
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.') {
            q[n++] = static_cast<char>(ch);
        } else {
            n += static_cast<size_t>(std::snprintf(q + n, sizeof q - n, "%%%02X", ch));
        }
    }
    // A US ZIP code works too (five digits, or ZIP+4): looked up in the US only, since the same digits
    // can be a postcode elsewhere (80919 is also one in France).
    bool zip = true;
    int digits = 0;
    for (const char* p = town; *p; p++) {
        if (*p >= '0' && *p <= '9') digits++;
        else if (!(*p == '-' && digits == 5) && *p != ' ') zip = false;
    }
    zip = zip && (digits == 5 || digits == 9);
    if (zip) {
        std::snprintf(q, sizeof q, "%.5s", town[0] == ' ' ? town + 1 : town);
        for (int i = 0; q[i]; i++)
            if (q[i] < '0' || q[i] > '9') q[i] = '\0';
    }
    std::snprintf(url, sizeof url, "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json%s",
                  q, zip ? "&countryCode=US" : "");
    gBody[0] = '\0';
    esp_http_client_config_t cfg{};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 10000;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client && esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int got, len = 0;
        while (len < static_cast<int>(sizeof gBody) - 1 &&
               (got = esp_http_client_read(client, gBody + len, sizeof gBody - 1 - len)) > 0)
            len += got;
        gBody[len] = '\0';
    }
    if (client) esp_http_client_cleanup(client);
    squatch::Place place;
    if (!squatch::parseGeocode(gBody, place)) {
        ESP_LOGW(TAG, "no town called \"%s\"", town);
        return false;
    }
    out = storage::Location{};
    out.lat = place.lat;
    out.lon = place.lon;
    const char* posix = squatch::posixZone(place.zone);
    std::snprintf(out.zone, sizeof out.zone, "%s", posix ? posix : "");  // empty: the weather's offset decides
    std::snprintf(out.name, sizeof out.name, "%s", place.name);
    storage::saveLocation(out);
    if (posix) walltime::setZone(posix);
    ESP_LOGI(TAG, "%s (%.3f, %.3f), %s", place.name, place.lat, place.lon, place.zone);
    return true;
}

}  // namespace townlookup
