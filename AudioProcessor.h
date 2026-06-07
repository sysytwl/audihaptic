#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <atomic>

class AudioProcessor {
public:
    struct AudioFeatures {
        float volume;           // RMS volume (0.0 to 1.0)
        float bass;            // Low frequency energy (0.0 to 1.0)
        float midrange;        // Mid frequency energy (0.0 to 1.0)
        float treble;          // High frequency energy (0.0 to 1.0)
        float peak;            // Peak amplitude (0.0 to 1.0)
        float dynamic_range;   // Dynamic range indicator (0.0 to 1.0)
    };

    AudioProcessor();
    ~AudioProcessor();

    // Process audio samples and extract features for haptic feedback
    AudioFeatures ProcessAudio(const float* samples, size_t sampleCount, size_t channels);

    // Configuration
    void SetSampleRate(uint32_t sampleRate);
    void SetSensitivity(float sensitivity) { m_sensitivity.store(std::clamp(sensitivity, 0.1f, 6.0f)); }
    void SetFrequencyBands(float bassCutoff, float trebleCutoff);

private:
    // Simple frequency analysis using time-domain filtering
    float CalculateRMS(const float* samples, size_t count);
    float CalculatePeak(const float* samples, size_t count);
    float CalculateBassEnergy(const float* samples, size_t count);
    float CalculateTrebleEnergy(const float* samples, size_t count);
    
    // Simple low-pass and high-pass filters
    float ApplyLowPass(float sample, float& state, float cutoff);
    float ApplyHighPass(float sample, float& prevSample, float& prevOutput, float cutoff);

    uint32_t m_sampleRate;
    std::atomic<float> m_sensitivity;
    
    // Frequency band cutoffs (normalized 0-1)
    std::atomic<float> m_bassCutoff;
    std::atomic<float> m_trebleCutoff;
    
    // Filter states for frequency analysis
    float m_bassFilterState;
    float m_trebleFilterPrevSample;
    float m_trebleFilterPrevOutput;
    
    // Running averages for smoothing
    std::vector<float> m_volumeHistory;
    std::vector<float> m_bassHistory;
    std::vector<float> m_trebleHistory;
    size_t m_historyIndex;
    static const size_t HISTORY_SIZE = 10;
};