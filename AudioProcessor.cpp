#include "AudioProcessor.h"
#include <numeric>
#include <iostream>

AudioProcessor::AudioProcessor()
    : m_sampleRate(44100)
    , m_sensitivity(2.5f)   // Default to 2x sensitivity
    , m_bassCutoff(0.005f)    // ~220Hz at 44.1kHz
    , m_trebleCutoff(0.3f)  // ~13.2kHz at 44.1kHz
    , m_bassFilterState(0.0f)
    , m_trebleFilterPrevSample(0.0f)
    , m_trebleFilterPrevOutput(0.0f)
    , m_historyIndex(0)
{
    m_volumeHistory.resize(HISTORY_SIZE, 0.0f);
    m_bassHistory.resize(HISTORY_SIZE, 0.0f);
    m_trebleHistory.resize(HISTORY_SIZE, 0.0f);
}

AudioProcessor::~AudioProcessor() = default;

void AudioProcessor::SetSampleRate(uint32_t sampleRate) {
    m_sampleRate = sampleRate;
    
    // Reset filter states when sample rate changes
    m_bassFilterState = 0.0f;
    m_trebleFilterPrevSample = 0.0f;
    m_trebleFilterPrevOutput = 0.0f;
}

void AudioProcessor::SetFrequencyBands(float bassLimit, float trebleLimit) {
    // Convert Hz to normalized frequency (0-1 where 1 = Nyquist frequency)
    m_bassCutoff.store(std::clamp(bassLimit / (m_sampleRate * 0.5f), 0.001f, 0.9f));
    m_trebleCutoff.store(std::clamp(trebleLimit / (m_sampleRate * 0.5f), 0.01f, 0.9f));
}

AudioProcessor::AudioFeatures AudioProcessor::ProcessAudio(const float* samples, size_t sampleCount, size_t channels) {
    if (!samples || sampleCount == 0) {
        return {};
    }

    // Convert to mono if stereo by averaging channels
    std::vector<float> monoSamples;
    if (channels > 1) {
        monoSamples.reserve(sampleCount / channels);
        for (size_t i = 0; i < sampleCount; i += channels) {
            float sum = 0.0f;
            for (size_t ch = 0; ch < channels && i + ch < sampleCount; ++ch) {
                sum += samples[i + ch];
            }
            monoSamples.push_back(sum / static_cast<float>(channels));
        }
    } else {
        monoSamples.assign(samples, samples + sampleCount);
    }

    AudioFeatures features = {};
    
    // Calculate basic audio features
    features.volume = CalculateRMS(monoSamples.data(), monoSamples.size());
    features.peak = CalculatePeak(monoSamples.data(), monoSamples.size());
    features.bass = CalculateBassEnergy(monoSamples.data(), monoSamples.size());
    features.treble = CalculateTrebleEnergy(monoSamples.data(), monoSamples.size());
    
    // Calculate midrange as total energy minus bass and treble
    features.midrange = std::max(0.0f, features.volume - (features.bass + features.treble) * 0.5f);
    
    // Dynamic range: difference between peak and RMS
    features.dynamic_range = features.peak - features.volume;
    
    // Apply sensitivity scaling
    const float sensitivity = m_sensitivity.load();
    features.volume *= sensitivity;
    features.bass *= sensitivity;
    features.midrange *= sensitivity;
    features.treble *= sensitivity;
    features.peak *= sensitivity;
    features.dynamic_range *= sensitivity;
    
    // Clamp all values to [0, 1]
    features.volume = std::clamp(features.volume, 0.0f, 1.0f);
    features.bass = std::clamp(features.bass, 0.0f, 1.0f);
    features.midrange = std::clamp(features.midrange, 0.0f, 1.0f);
    features.treble = std::clamp(features.treble, 0.0f, 1.0f);
    features.peak = std::clamp(features.peak, 0.0f, 1.0f);
    features.dynamic_range = std::clamp(features.dynamic_range, 0.0f, 1.0f);
    
    // Update history for smoothing
    m_volumeHistory[m_historyIndex] = features.volume;
    m_bassHistory[m_historyIndex] = features.bass;
    m_trebleHistory[m_historyIndex] = features.treble;
    m_historyIndex = (m_historyIndex + 1) % HISTORY_SIZE;
    
    return features;
}

float AudioProcessor::CalculateRMS(const float* samples, size_t count) {
    if (count == 0) return 0.0f;
    
    float sum = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        sum += samples[i] * samples[i];
    }
    return std::sqrt(sum / static_cast<float>(count));
}

float AudioProcessor::CalculatePeak(const float* samples, size_t count) {
    if (count == 0) return 0.0f;
    
    float peak = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        peak = std::max(peak, std::abs(samples[i]));
    }
    return peak;
}

float AudioProcessor::CalculateBassEnergy(const float* samples, size_t count) {
    if (count == 0) return 0.0f;
    
    float energy = 0.0f;
    const float cutoff = m_bassCutoff.load();
    
    // Simple low-pass filter to isolate bass frequencies
    for (size_t i = 0; i < count; ++i) {
        float filtered = ApplyLowPass(samples[i], m_bassFilterState, cutoff);
        energy += filtered * filtered;
    }
    
    return std::sqrt(energy / static_cast<float>(count));
}

float AudioProcessor::CalculateTrebleEnergy(const float* samples, size_t count) {
    if (count == 0) return 0.0f;
    
    float energy = 0.0f;
    const float cutoff = m_trebleCutoff.load();
    
    // Simple high-pass filter to isolate treble frequencies
    for (size_t i = 0; i < count; ++i) {
        float filtered = ApplyHighPass(samples[i], m_trebleFilterPrevSample, 
                                     m_trebleFilterPrevOutput, cutoff);
        energy += filtered * filtered;
    }
    
    return std::sqrt(energy / static_cast<float>(count));
}

float AudioProcessor::ApplyLowPass(float sample, float& state, float cutoff) {
    // Simple first-order low-pass filter
    float alpha = cutoff;
    state = alpha * sample + (1.0f - alpha) * state;
    return state;
}

float AudioProcessor::ApplyHighPass(float sample, float& prevSample, float& prevOutput, float cutoff) {
    // Simple first-order high-pass filter
    float alpha = 1.0f - cutoff;
    float output = alpha * (prevOutput + sample - prevSample);
    prevSample = sample;
    prevOutput = output;
    return output;
}