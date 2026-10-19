// The watch app's bridge to the game: one pet (the phone's, synced over the Wear OS Data
// Layer) and the watch view of it. Called from Java with the Pet class's lock held, so
// there's never more than one caller at a time.
#include <jni.h>

#include <ctime>
#include <vector>

#include "squatch/canvas.hpp"
#include "squatch/save.hpp"
#include "squatch/sim.hpp"
#include "squatch/wrist.hpp"

using namespace squatch;

namespace {

Sim gSim;
Wrist* gWrist = nullptr;
bool gLoaded = false;
std::vector<uint16_t> gFb(Wrist::kSize * Wrist::kSize);

int32_t localTzOffset() {
    std::time_t now = std::time(nullptr);
    std::tm utc = *std::gmtime(&now);
    utc.tm_isdst = -1;
    return static_cast<int32_t>(std::difftime(now, std::mktime(&utc)));
}

Wrist& wrist() {
    if (!gWrist) gWrist = new Wrist(gSim);
    return *gWrist;
}

void tickTo(int64_t now) {
    if (gLoaded && now > gSim.state().now) gSim.tick(now - gSim.state().now);
}

}  // namespace

#define JNI(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_ceruleanscarab_pocketsquatch_wear_Native_##name

JNI(jboolean, load)(JNIEnv* env, jclass, jbyteArray save, jlong nowSecs) {
    if (!save) return JNI_FALSE;
    const jsize n = env->GetArrayLength(save);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    env->GetByteArrayRegion(save, 0, n, reinterpret_cast<jbyte*>(buf.data()));
    State s;
    if (load(buf.data(), buf.size(), s) != LoadResult::Ok) return JNI_FALSE;
    gSim.mutableState() = s;
    gSim.mutableState().tzOffset = localTzOffset();
    gLoaded = true;
    tickTo(nowSecs);
    Event ev[16];
    while (gSim.drainEvents(ev, 16) > 0) {}  // what happened while away isn't news on the watch
    return JNI_TRUE;
}

JNI(jboolean, hasPet)(JNIEnv*, jclass) { return gLoaded ? JNI_TRUE : JNI_FALSE; }

JNI(jbyteArray, save)(JNIEnv* env, jclass) {
    if (!gLoaded) return nullptr;
    std::vector<uint8_t> buf(kSaveBytes);
    const size_t n = save(gSim.state(), buf.data(), buf.size());
    jbyteArray out = env->NewByteArray(static_cast<jsize>(n));
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(n), reinterpret_cast<const jbyte*>(buf.data()));
    return out;
}

JNI(void, tickTo)(JNIEnv*, jclass, jlong nowSecs) { tickTo(nowSecs); }

// One frame of the watch screen, 480x480, as ARGB for a Bitmap.
JNI(void, frame)(JNIEnv* env, jclass, jintArray argb, jlong ms) {
    if (!gLoaded || !argb) return;
    Wrist& w = wrist();
    w.update(static_cast<uint32_t>(ms));
    Canvas c(gFb.data(), Wrist::kSize, Wrist::kSize);
    w.draw(c);
    jint* out = env->GetIntArrayElements(argb, nullptr);
    for (size_t i = 0; i < gFb.size(); i++) {
        const uint32_t p = gFb[i];
        const uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
        out[i] = static_cast<jint>(0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2));
    }
    env->ReleaseIntArrayElements(argb, out, 0);
}

// A tap in 480x480 terms; the action done (0 for none).
JNI(jint, tap)(JNIEnv*, jclass, jint x, jint y) {
    if (!gLoaded) return 0;
    return static_cast<jint>(wrist().tap(x, y));
}

JNI(jstring, name)(JNIEnv* env, jclass) {
    const char* n = gLoaded && gSim.state().name[0] ? gSim.state().name : "SQUATCH";
    return env->NewStringUTF(n);
}

JNI(jstring, status)(JNIEnv* env, jclass) { return env->NewStringUTF(gLoaded ? wrist().status() : "NO PET YET"); }

// food, happiness, cleanliness, each 0..4
JNI(jfloatArray, meters)(JNIEnv* env, jclass) {
    const State& s = gSim.state();
    const float v[3] = {s.hunger, s.happiness, static_cast<float>(s.messes >= 4 ? 0 : 4 - s.messes)};
    jfloatArray out = env->NewFloatArray(3);
    env->SetFloatArrayRegion(out, 0, 3, v);
    return out;
}

// What it needs right now ("is hungry"), or null.
JNI(jstring, needNow)(JNIEnv* env, jclass) {
    const char* w = gLoaded ? Wrist::needNow(gSim.state()) : nullptr;
    return w ? env->NewStringUTF(w) : nullptr;
}

// Seconds until it next needs you, looking up to two days ahead; -1 if not that soon.
JNI(jlong, nextNeedSecs)(JNIEnv*, jclass) {
    if (!gLoaded) return -1;
    return static_cast<jlong>(Wrist::nextNeed(gSim.state(), 2 * 86400, nullptr));
}
