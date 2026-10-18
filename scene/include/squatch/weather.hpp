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
};

// WMO weather interpretation codes, as Open-Meteo reports them.
Sky skyFromWmo(int code);

// Reads the "current" block of an Open-Meteo forecast reply, as requested with
// current=temperature_2m,weather_code,wind_speed_10m. False if it isn't there.
bool parseOpenMeteo(const char* json, Weather& out);

// Lower-case name, e.g. "snow"; also accepts these from parseSky() for the PC's flag.
const char* toString(Sky s);
bool parseSky(const char* name, Sky& out);

}  // namespace squatch
