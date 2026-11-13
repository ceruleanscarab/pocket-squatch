#include "squatch/weather.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace squatch {

Sky skyFromWmo(int code) {
    if (code <= 1) return Sky::Clear;                                   // clear, mainly clear
    if (code <= 3) return Sky::Cloudy;                                  // partly cloudy, overcast
    if (code == 45 || code == 48) return Sky::Fog;
    if (code >= 51 && code <= 57) return Sky::Drizzle;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return Sky::Rain;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Sky::Snow;
    if (code >= 95 && code <= 99) return Sky::Storm;
    return Sky::Unknown;
}

namespace {

// The number after "key": inside [from, to), or false.
bool numberAfter(const char* from, const char* to, const char* key, float& out) {
    const size_t klen = std::strlen(key);
    for (const char* p = from; p && p + klen < to; p++) {
        p = std::strstr(p, key);
        if (!p || p + klen >= to) return false;
        const char* q = p + klen;
        while (q < to && (*q == ' ' || *q == ':')) q++;
        if (q < to && (*q == '-' || (*q >= '0' && *q <= '9'))) {
            out = std::strtof(q, nullptr);
            return true;
        }
    }
    return false;
}

}  // namespace

bool parseOpenMeteo(const char* json, Weather& out) {
    if (!json) return false;
    // "current_units" comes first and has the same keys (with unit strings): skip to "current":{
    const char* cur = std::strstr(json, "\"current\":");
    if (!cur) return false;
    const char* open = std::strchr(cur, '{');
    const char* close = open ? std::strchr(open, '}') : nullptr;
    if (!open || !close) return false;
    float code = -1, temp = 0, wind = 0;
    if (!numberAfter(open, close, "\"weather_code\"", code)) return false;
    numberAfter(open, close, "\"temperature_2m\"", temp);
    numberAfter(open, close, "\"wind_speed_10m\"", wind);
    out.sky = skyFromWmo(static_cast<int>(code));
    out.tempC = temp;
    out.windKmh = wind;
    float offset = 0;
    const char* end = json + std::strlen(json);
    out.utcOffsetSecs = numberAfter(json, end, "\"utc_offset_seconds\"", offset) ? static_cast<int32_t>(offset)
                                                                                : Weather::kNoOffset;
    return out.sky != Sky::Unknown;
}

namespace {

// The string value after "key": in [from, to), copied into out; false if there isn't one.
bool stringAfter(const char* from, const char* to, const char* key, char* out, size_t n) {
    const char* p = std::strstr(from, key);
    if (!p || p >= to) return false;
    p += std::strlen(key);
    while (p < to && (*p == ' ' || *p == ':')) p++;
    if (p >= to || *p != '"') return false;
    p++;
    size_t i = 0;
    for (; p < to && *p != '"' && i + 1 < n; p++) {
        if (*p == '\\' && p + 1 < to) p++;  // an escaped character: keep it plainly
        out[i++] = *p;
    }
    out[i] = '\0';
    return true;
}

void appendUpper(char* out, size_t n, const char* s) {
    size_t len = std::strlen(out);
    for (; *s && len + 1 < n; s++) {
        char ch = *s;
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        if (static_cast<unsigned char>(ch) >= 0x80) continue;  // letters the screen's font hasn't got
        out[len++] = ch;
    }
    out[len] = '\0';
}

}  // namespace

bool parseGeocode(const char* json, Place& out) {
    if (!json) return false;
    const char* r = std::strstr(json, "\"results\"");
    if (!r) return false;
    const char* open = std::strchr(r, '{');
    const char* close = open ? std::strchr(open, '}') : nullptr;
    if (!open || !close) return false;
    float lat = 0, lon = 0;
    if (!numberAfter(open, close, "\"latitude\"", lat) || !numberAfter(open, close, "\"longitude\"", lon)) return false;
    char name[48] = {}, region[48] = {}, country[48] = {};
    stringAfter(open, close, "\"name\"", name, sizeof name);
    if (!stringAfter(open, close, "\"admin1\"", region, sizeof region))
        stringAfter(open, close, "\"country\"", country, sizeof country);
    out = Place{};
    out.lat = lat;
    out.lon = lon;
    stringAfter(open, close, "\"timezone\"", out.zone, sizeof out.zone);
    appendUpper(out.name, sizeof out.name, name);
    const char* second = region[0] ? region : country;
    if (second[0] && std::strcmp(second, name) != 0) {
        appendUpper(out.name, sizeof out.name, ", ");
        appendUpper(out.name, sizeof out.name, second);
    }
    return true;
}

const char* posixZone(const char* iana) {
    struct Zone {
        const char* iana;
        const char* posix;
    };
    static const Zone kZones[] = {
        // North America
        {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"}, {"America/Detroit", "EST5EDT,M3.2.0,M11.1.0"},
        {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"}, {"America/Indiana/Indianapolis", "EST5EDT,M3.2.0,M11.1.0"},
        {"America/Kentucky/Louisville", "EST5EDT,M3.2.0,M11.1.0"},
        {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"}, {"America/Winnipeg", "CST6CDT,M3.2.0,M11.1.0"},
        {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"}, {"America/Boise", "MST7MDT,M3.2.0,M11.1.0"},
        {"America/Edmonton", "MST7MDT,M3.2.0,M11.1.0"}, {"America/Phoenix", "MST7"},
        {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"}, {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
        {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"}, {"Pacific/Honolulu", "HST10"},
        {"America/Halifax", "AST4ADT,M3.2.0,M11.1.0"}, {"America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0"},
        {"America/Regina", "CST6"}, {"America/Puerto_Rico", "AST4"}, {"America/Mexico_City", "CST6"},
        // Europe
        {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"}, {"Europe/Dublin", "IST-1GMT0,M10.5.0,M3.5.0/1"},
        {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
        {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Oslo", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Copenhagen", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Prague", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Budapest", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Europe/Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Bucharest", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        // Asia-Pacific and elsewhere
        {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"}, {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
        {"Australia/Hobart", "AEST-10AEDT,M10.1.0,M4.1.0/3"}, {"Australia/Brisbane", "AEST-10"},
        {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"}, {"Australia/Darwin", "ACST-9:30"},
        {"Australia/Perth", "AWST-8"}, {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
        {"Asia/Tokyo", "JST-9"}, {"Asia/Seoul", "KST-9"}, {"Asia/Shanghai", "CST-8"}, {"Asia/Hong_Kong", "HKT-8"},
        {"Asia/Singapore", "<+08>-8"}, {"Asia/Kolkata", "IST-5:30"}, {"Asia/Dubai", "<+04>-4"},
        {"Africa/Johannesburg", "SAST-2"}, {"America/Sao_Paulo", "<-03>3"},
    };
    if (!iana) return nullptr;
    for (const Zone& z : kZones)
        if (std::strcmp(z.iana, iana) == 0) return z.posix;
    return nullptr;
}

void fixedZone(int32_t utcOffsetSecs, char* out, int n) {
    // POSIX counts west as positive: UTC+5:30 is "UTC-5:30".
    const int32_t west = -utcOffsetSecs;
    const int32_t a = west < 0 ? -west : west;
    const int h = static_cast<int>(a / 3600), m = static_cast<int>(a % 3600 / 60);
    if (m) std::snprintf(out, static_cast<size_t>(n), "UTC%s%d:%02d", west < 0 ? "-" : "", h, m);
    else std::snprintf(out, static_cast<size_t>(n), "UTC%s%d", west < 0 ? "-" : "", h);
}

const char* toString(Sky s) {
    switch (s) {
        case Sky::Clear: return "clear";
        case Sky::Cloudy: return "cloudy";
        case Sky::Fog: return "fog";
        case Sky::Drizzle: return "drizzle";
        case Sky::Rain: return "rain";
        case Sky::Snow: return "snow";
        case Sky::Storm: return "storm";
        case Sky::Unknown: break;
    }
    return "unknown";
}

bool parseSky(const char* name, Sky& out) {
    for (int i = 1; i <= static_cast<int>(Sky::Storm); i++) {
        if (std::strcmp(name, toString(static_cast<Sky>(i))) == 0) {
            out = static_cast<Sky>(i);
            return true;
        }
    }
    return false;
}

}  // namespace squatch
