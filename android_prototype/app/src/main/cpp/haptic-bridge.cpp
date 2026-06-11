#include <jni.h>
#include <vector>
#include "AudioProcessor.h"

static AudioProcessor g_audioProcessor;
static bool g_processorInitialized = false;

extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_example_audihaptic_NativeLib_nativeProcessAudio(JNIEnv* env, jobject /* this */, jshortArray data_, jint length, jint sampleRate) {
    if (!g_processorInitialized) {
        g_audioProcessor.SetSampleRate(static_cast<uint32_t>(sampleRate));
        g_processorInitialized = true;
    }

    jshort* data = env->GetShortArrayElements(data_, NULL);
    if (!data) return NULL;

    int count = length / 2;
    std::vector<float> monoSamples;
    monoSamples.reserve(count);
    for (int i = 0; i + 1 < length; i += 2) {
        float left = static_cast<float>(data[i]) / 32768.0f;
        float right = static_cast<float>(data[i + 1]) / 32768.0f;
        monoSamples.push_back((left + right) * 0.5f);
    }
    env->ReleaseShortArrayElements(data_, data, JNI_ABORT);

    auto features = g_audioProcessor.ProcessAudio(monoSamples.data(), monoSamples.size(), 1);

    const int featureCount = 6;
    jdoubleArray out = env->NewDoubleArray(featureCount);
    if (!out) return NULL;

    jdouble values[featureCount] = {
        features.volume,
        features.bass,
        features.midrange,
        features.treble,
        features.peak,
        features.dynamic_range
    };
    env->SetDoubleArrayRegion(out, 0, featureCount, values);
    return out;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_audihaptic_NativeLib_nativeSetAnalysisMode(JNIEnv* env, jobject /* this */, jint mode) {
    g_audioProcessor.SetAnalysisMode(static_cast<AudioProcessor::AnalysisMode>(mode));
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_audihaptic_NativeLib_nativeSetSensitivity(JNIEnv* env, jobject /* this */, jfloat sensitivity) {
    g_audioProcessor.SetSensitivity(sensitivity);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_audihaptic_NativeLib_nativeSetFrequencyBands(JNIEnv* env, jobject /* this */, jfloat bassCutoff, jfloat trebleCutoff) {
    g_audioProcessor.SetFrequencyBands(bassCutoff, trebleCutoff);
}
