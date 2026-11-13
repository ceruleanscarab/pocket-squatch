// The weather outside, shown in the clearing.
//
// Hardware-free: the board fetches the current conditions from Open-Meteo (a free weather
// service, no account) about once an hour and hands the reply to parseOpenMeteo(); the PC
// can pretend with --weather. The scene only draws it: rain, snow, fog, clouds and storms.
// It isn't part of the pet's save, and it doesn't change how the pet is doing.
#pragma once

#include <cstdint>

namespace squatch {

enum class Sky : uint8_t { Unknown, Clear, Cloudy, Fog, Drizzle, Rain, Snow, Storm };

struct Weather {
    Sky sky = Sky::Unknown;
    float tempC = 0;
    float windKmh = 0;
    // With &timezone=auto the reply also gives the place's offset from UTC right now
    // (daylight saving included); kNoOffset if it didn't.
    static constexpr int32_t kNoOffset = -999999;
    int32_t utcOffsetSecs = kNoOffset;
};

// A town, as Open-Meteo's geocoding finds it (its first result).
struct Place {
    char name[40] = {};      // e.g. "COLORADO SPRINGS, COLORADO", in capitals for the screen
    float lat = 0, lon = 0;
    char zone[48] = {};      // the IANA time zone, e.g. "America/Denver"
};

// Reads a geocoding reply (geocoding-api.open-meteo.com/v1/search?name=...&count=1).
// False if nothing was found.
bool parseGeocode(const char* json, Place& out);

// The POSIX TZ string (with its daylight-saving rules) for a common IANA zone, or nullptr
// for one it doesn't know: then the board follows the weather's utcOffsetSecs instead.
const char* posixZone(const char* iana);
// A fixed-offset POSIX TZ string for an offset east of UTC, e.g. 19800 -> "UTC-5:30".
void fixedZone(int32_t utcOffsetSecs, char* out, int n);

// WMO weather interpretation codes, as Open-Meteo reports them.
Sky skyFromWmo(int code);

// Reads the "current" block of an Open-Meteo forecast reply, as requested with
// current=temperature_2m,weather_code,wind_speed_10m. False if it isn't there.
bool parseOpenMeteo(const char* json, Weather& out);

// Lower-case name, e.g. "snow"; also accepts these from parseSky() for the PC's flag.
const char* toString(Sky s);
bool parseSky(const char* name, Sky& out);

}  // namespace squatch
