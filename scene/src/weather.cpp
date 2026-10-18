#include "squatch/weather.hpp"

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
    return out.sky != Sky::Unknown;
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
