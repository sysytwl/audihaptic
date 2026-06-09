#include "AudioProcessor.h"
#include <numeric>
#include <iostream>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
// Simple in-file FFT implementation (radix-2 Cooley-Tukey)
namespace {
class SimpleFFT {
public:
    explicit SimpleFFT(int n) : n(n) {
        int levels = 0; int t = 1;
        while (t < n) { t <<= 1; ++levels; }
        if ((1 << levels) != n) throw std::invalid_argument("FFT size must be power of two");
        cosTable.resize(n/2);
        sinTable.resize(n/2);
        for (int i = 0; i < n/2; ++i) {
            cosTable[i] = std::cos(-2.0 * M_PI * i / n);
            sinTable[i] = std::sin(-2.0 * M_PI * i / n);
        }
    }

    std::vector<double> fft(const std::vector<double>& input) {
        std::vector<double> real(input.begin(), input.end());
        real.resize(n, 0.0);
        std::vector<double> imag(n, 0.0);

        // bit-reverse
        int j = 0;
        for (int i = 1; i < n; ++i) {
            int bit = n >> 1;
            while (j & bit) { j ^= bit; bit >>= 1; }
            j ^= bit;
            if (i < j) std::swap(real[i], real[j]);
        }

        for (int size = 2; size <= n; size <<= 1) {
            int halfSize = size / 2;
            int tableStep = n / size;
            for (int i = 0; i < n; i += size) {
                int k = 0;
                for (int j2 = i; j2 < i + halfSize; ++j2) {
                    int l = j2 + halfSize;
                    double tpre =  real[l] * cosTable[k] + imag[l] * sinTable[k];
                    double tpim = -real[l] * sinTable[k] + imag[l] * cosTable[k];
                    real[l] = real[j2] - tpre;
                    imag[l] = imag[j2] - tpim;
                    real[j2] += tpre;
                    imag[j2] += tpim;
                    k += tableStep;
                }
            }
        }

        std::vector<double> mag(n/2);
        for (int i = 0; i < n/2; ++i) mag[i] = std::hypot(real[i], imag[i]);
        return mag;
    }

private:
    int n;
    std::vector<double> cosTable;
    std::vector<double> sinTable;
};

// helper to pick next power of two
static int NextPowerOfTwo(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

static void ApplyHannWindow(std::vector<double>& buf) {
    int N = (int)buf.size();
    for (int i = 0; i < N; ++i) {
        double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (N - 1)));
        buf[i] *= w;
    }
}
// simple exponential smoothing helper
static double SmoothEnergy(std::vector<float>& history, size_t& index, double value) {
    history[index] = static_cast<float>(value);
    index = (index + 1) % history.size();
    double sum = 0.0;
    for (float v : history) sum += v;
    return sum / history.size();
}
}

AudioProcessor::AudioProcessor()
    : m_sampleRate(44100)
    , m_sensitivity(2.5f)   // Default to 2x sensitivity
    , m_bassCutoff(0.005f)    // ~220Hz at 44.1kHz
    , m_trebleCutoff(0.3f)  // ~13.2kHz at 44.1kHz
    , m_bassFilterState(0.0f)
    , m_trebleFilterPrevSample(0.0f)
    , m_trebleFilterPrevOutput(0.0f)
    , m_analysisMode(AnalysisMode::TimeDomain)
    , m_historyIndex(0)
    , m_bassHistoryIndex(0)
    , m_trebleHistoryIndex(0)
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
    if (m_analysisMode == AnalysisMode::FrequencyDomain) {
        // FFT-based band energy (low frequencies)
        int fftSize = NextPowerOfTwo((int)count);
        if (fftSize < 64) fftSize = 64;
        if (fftSize > 4096) fftSize = 4096;

        std::vector<double> in(count);
        for (size_t i = 0; i < count; ++i) in[i] = samples[i];
        ApplyHannWindow(in);
        SimpleFFT fft(fftSize);
        std::vector<double> spectrum = fft.fft(in);

        float cutoff = m_bassCutoff.load();
        double nyq = m_sampleRate / 2.0;
        double cutoffHz = cutoff * nyq;

        double energy = 0.0;
        for (size_t i = 0; i < spectrum.size(); ++i) {
            double f = i * (double)m_sampleRate / (double)fftSize;
            if (f <= cutoffHz) energy += spectrum[i] * spectrum[i];
        }
        double val = std::sqrt(energy / static_cast<double>(count));
        double sm = SmoothEnergy(m_bassHistory, m_bassHistoryIndex, val);
        return static_cast<float>(sm);
    }

    float energy = 0.0f;
    const float cutoff = m_bassCutoff.load();
    for (size_t i = 0; i < count; ++i) {
        float filtered = ApplyLowPass(samples[i], m_bassFilterState, cutoff);
        energy += filtered * filtered;
    }
    return std::sqrt(energy / static_cast<float>(count));
}

float AudioProcessor::CalculateTrebleEnergy(const float* samples, size_t count) {
    if (count == 0) return 0.0f;
    if (m_analysisMode == AnalysisMode::FrequencyDomain) {
        int fftSize = NextPowerOfTwo((int)count);
        if (fftSize < 64) fftSize = 64;
        if (fftSize > 4096) fftSize = 4096;

        std::vector<double> in(count);
        for (size_t i = 0; i < count; ++i) in[i] = samples[i];
        ApplyHannWindow(in);
        SimpleFFT fft(fftSize);
        std::vector<double> spectrum = fft.fft(in);

        float cutoff = m_trebleCutoff.load();
        double nyq = m_sampleRate / 2.0;
        double cutoffHz = cutoff * nyq;

        double energy = 0.0;
        for (size_t i = 0; i < spectrum.size(); ++i) {
            double f = i * (double)m_sampleRate / (double)fftSize;
            if (f >= cutoffHz) energy += spectrum[i] * spectrum[i];
        }
        double val = std::sqrt(energy / static_cast<double>(count));
        double sm = SmoothEnergy(m_trebleHistory, m_trebleHistoryIndex, val);
        return static_cast<float>(sm);
    }

    float energy = 0.0f;
    const float cutoff = m_trebleCutoff.load();
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