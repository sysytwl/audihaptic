#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <conio.h> // For _kbhit() and _getch()
#include <iomanip>
#include <mutex>

#include "AudioCaptureManager.h"
#include "AudioProcessor.h"
#include "HapticController.h"

class AudioHapticsApp {
public:
    AudioHapticsApp() = default;
    ~AudioHapticsApp() = default;

    bool Initialize() {
        std::cout << "=== Audio to Haptics Converter ===" << std::endl;
        std::cout << "Initializing components..." << std::endl;

            // Initialize audio capture with AUTO method (tries multiple approaches)
    if (!m_audioCapture.Initialize(AudioCaptureManager::CaptureMethod::AUTO)) {
        std::cerr << "Failed to initialize audio capture" << std::endl;
        return false;
    }
    
    std::cout << "Using audio capture method: " << m_audioCapture.GetMethodName() << std::endl;

        // Initialize haptic controller
        if (!m_hapticController.Initialize()) {
            std::cerr << "Failed to initialize haptic controller" << std::endl;
            return false;
        }

        // Set up audio processor
        m_audioProcessor.SetSampleRate(m_audioCapture.GetSampleRate());
        m_audioProcessor.SetSensitivity(4.0f); // Start with ultra sensitivity (4x)

        // Set up audio callback
        m_audioCapture.SetAudioCallback([this](const float* samples, size_t sampleCount, size_t channels) {
            this->OnAudioData(samples, sampleCount, channels);
        });

        std::cout << "Initialization complete!" << std::endl;
        return true;
    }

    void Run() {
        if (!m_audioCapture.StartCapture()) {
            std::cerr << "Failed to start audio capture" << std::endl;
            return;
        }

        std::cout << "\n=== Audio-to-Haptics Active ===" << std::endl;
        std::cout << m_hapticController.GetDeviceStatusString() << std::endl;
        std::cout << "Haptic mode: " << m_hapticController.GetHapticModeString() << std::endl;
        std::cout << "\nControls:" << std::endl;
        std::cout << "  [Q] Quit" << std::endl;
        std::cout << "  [S] Adjust sensitivity" << std::endl;
        std::cout << "  [F] Adjust frequency bands" << std::endl;
        std::cout << "  [H] Haptic settings" << std::endl;
        std::cout << "  [M] Haptic mode (GameInput 1.0/2.0)" << std::endl;
        std::cout << "  [T] Test haptics" << std::endl;
        std::cout << "  [R] Refresh devices" << std::endl;
        std::cout << "\nListening for audio... (Press any key for controls)\n" << std::endl;

        bool running = true;
        auto lastStatsUpdate = std::chrono::steady_clock::now();

        while (running) {
            // Update devices periodically
            m_hapticController.UpdateDevices();

            // Display live audio stats
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStatsUpdate).count() > 100) {
                DisplayLiveStats();
                lastStatsUpdate = now;
            }

            // Check for keyboard input
            if (_kbhit()) {
                char key = _getch();
                running = HandleKeyPress(key);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        m_audioCapture.StopCapture();
        m_hapticController.StopAllHaptics();
        std::cout << "\nShutting down..." << std::endl;
    }

    void RunService() {
        // Service mode - non-blocking, no console UI
        std::cout << "Starting Audio-to-Haptics Service..." << std::endl;
        
        try {
            if (!m_audioCapture.StartCapture()) {
                std::cerr << "Failed to start audio capture" << std::endl;
                return;
            }

            m_running = true;
            std::cout << "Audio-to-Haptics Service started successfully" << std::endl;

            while (m_running) {
                // Update devices periodically
                m_hapticController.UpdateDevices();
                
                // Sleep to prevent high CPU usage
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            m_audioCapture.StopCapture();
            m_hapticController.StopAllHaptics();
            std::cout << "Audio-to-Haptics Service stopped" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "Service error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown service error occurred" << std::endl;
        }
    }

    void Shutdown() {
        m_running = false;
    }

private:
    void OnAudioData(const float* samples, size_t sampleCount, size_t channels) {
        // Process audio to extract features
        auto features = m_audioProcessor.ProcessAudio(samples, sampleCount, channels);
        
        // Store latest features for display
        {
            std::lock_guard<std::mutex> lock(m_featuresMutex);
            m_latestFeatures = features;
        }

        // Send to haptic controller
        m_hapticController.ProcessAudioFeatures(features);
    }

    void DisplayLiveStats() {
        AudioProcessor::AudioFeatures features;
        {
            std::lock_guard<std::mutex> lock(m_featuresMutex);
            features = m_latestFeatures;
        }

        // Move cursor to beginning of stats area (assuming we're at the bottom)
        std::cout << "\r";
        
        // Create a visual bar representation
        auto makeBar = [](float value, int width = 20) {
            int filled = static_cast<int>(value * width);
            std::string bar = "[";
            for (int i = 0; i < width; ++i) {
                bar += (i < filled) ? "|" : "-";
            }
            bar += "]";
            return bar;
        };

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Volume: " << makeBar(features.volume) << " " << features.volume << "  ";
        std::cout << "Bass: " << makeBar(features.bass, 10) << " " << features.bass << "  ";
        std::cout << "Treble: " << makeBar(features.treble, 10) << " " << features.treble;
        std::cout << std::flush;
    }

    bool HandleKeyPress(char key) {
        key = std::tolower(key);
        
        switch (key) {
            case 'q':
                return false; // Quit

            case 's':
                AdjustSensitivity();
                break;

            case 'f':
                AdjustFrequencyBands();
                break;

            case 'h':
                ConfigureHaptics();
                break;

            case 'm':
                ConfigureHapticMode();
                break;

            case 't':
                TestHaptics();
                break;

            case 'r':
                RefreshDevices();
                break;

            default:
                break;
        }
        
        return true;
    }

    void AdjustSensitivity() {
        std::cout << "\n\nCurrent sensitivity levels:" << std::endl;
        std::cout << "1. Low (0.5x)" << std::endl;
        std::cout << "2. Normal (1.0x)" << std::endl;
        std::cout << "3. High (1.5x)" << std::endl;
        std::cout << "4. Very High (2.0x)" << std::endl;
        std::cout << "5. Extreme (3.0x)" << std::endl;
        std::cout << "6. Ultra (4.0x)" << std::endl;
        std::cout << "Select (1-6): ";

        char choice = _getch();
        float sensitivity = 4.0f; // Default to 4x

        switch (choice) {
            case '1': sensitivity = 0.5f; break;
            case '2': sensitivity = 1.0f; break;
            case '3': sensitivity = 1.5f; break;
            case '4': sensitivity = 2.0f; break;
            case '5': sensitivity = 3.0f; break;
            case '6': sensitivity = 4.0f; break;
            default:
                std::cout << "\nInvalid choice. Keeping current setting." << std::endl;
                return;
        }

        m_audioProcessor.SetSensitivity(sensitivity);
        std::cout << "\nSensitivity set to " << sensitivity << "x" << std::endl;
        std::cout << "Press any key to continue..." << std::endl;
        _getch();
        std::cout << "\n";
    }

    void AdjustFrequencyBands() {
        float bassHz = 441.0f;
        float trebleHz = 13200.0f;

        std::cout << "\n\nEnter bass cutoff frequency in Hz (e.g. 50-1000): ";
        std::cin >> bassHz;
        std::cout << "Enter treble cutoff frequency in Hz (e.g. 2000-16000): ";
        std::cin >> trebleHz;

        if (bassHz <= 0.0f || trebleHz <= 0.0f) {
            std::cout << "\nInvalid values. Keeping current cutoff frequencies." << std::endl;
        } else {
            m_audioProcessor.SetFrequencyBands(bassHz, trebleHz);
            std::cout << "\nCutoff frequencies updated: bass=" << bassHz << " Hz, treble=" << trebleHz << " Hz" << std::endl;
        }

        std::cout << "Press any key to continue..." << std::endl;
        _getch();
        std::cout << "\n";
    }

    void ConfigureHaptics() {
        auto settings = m_hapticController.GetHapticSettings();
        
        std::cout << "\n\nHaptic Settings:" << std::endl;
        std::cout << "1. Bass intensity: " << settings.bassIntensity << std::endl;
        std::cout << "2. Treble intensity: " << settings.trebleIntensity << std::endl;
        std::cout << "3. Volume intensity: " << settings.volumeIntensity << std::endl;
        std::cout << "4. Dynamic intensity: " << settings.dynamicIntensity << std::endl;
        std::cout << "5. Reset to defaults" << std::endl;
        std::cout << "Select (1-5) or press any other key to return: ";

        char choice = _getch();
        
        switch (choice) {
            case '1':
                std::cout << "\nBass intensity (0.0-2.0): ";
                std::cin >> settings.bassIntensity;
                settings.bassIntensity = std::clamp(settings.bassIntensity, 0.0f, 2.0f);
                break;
            case '2':
                std::cout << "\nTreble intensity (0.0-2.0): ";
                std::cin >> settings.trebleIntensity;
                settings.trebleIntensity = std::clamp(settings.trebleIntensity, 0.0f, 2.0f);
                break;
            case '3':
                std::cout << "\nVolume intensity (0.0-2.0): ";
                std::cin >> settings.volumeIntensity;
                settings.volumeIntensity = std::clamp(settings.volumeIntensity, 0.0f, 2.0f);
                break;
            case '4':
                std::cout << "\nDynamic intensity (0.0-2.0): ";
                std::cin >> settings.dynamicIntensity;
                settings.dynamicIntensity = std::clamp(settings.dynamicIntensity, 0.0f, 2.0f);
                break;
            case '5':
                settings = HapticController::HapticSettings{}; // Reset to defaults
                std::cout << "\nSettings reset to defaults." << std::endl;
                break;
            default:
                std::cout << "\n";
                return;
        }

        m_hapticController.SetHapticSettings(settings);
        std::cout << "Settings updated. Press any key to continue..." << std::endl;
        _getch();
        std::cout << "\n";
    }

    void TestHaptics() {
        std::cout << "\n\nTesting haptic feedback..." << std::endl;
        std::cout << "You should feel vibration patterns on your gamepad." << std::endl;
        
        // Test different patterns
        std::cout << "Testing left motor...";
        m_hapticController.SetRumble(1.0f, 0.0f, 0.0f, 0.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        std::cout << " right motor...";
        m_hapticController.SetRumble(0.0f, 1.0f, 0.0f, 0.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        std::cout << " triggers...";
        m_hapticController.SetRumble(0.0f, 0.0f, 1.0f, 1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        std::cout << " all motors..." << std::endl;
        m_hapticController.SetRumble(0.5f, 0.5f, 0.5f, 0.5f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        m_hapticController.StopAllHaptics();
        std::cout << "Test complete. Press any key to continue..." << std::endl;
        _getch();
        std::cout << "\n";
    }

    void ConfigureHapticMode() {
        auto settings = m_hapticController.GetHapticSettings();
        
        std::cout << "\n\nHaptic Mode Configuration:" << std::endl;
        std::cout << "Current mode: " << m_hapticController.GetHapticModeString() << std::endl;
        std::cout << "\nAvailable modes:" << std::endl;
        std::cout << "1. Auto (detect best available)" << std::endl;
        std::cout << "2. Rumble (GameInput 1.0 - traditional)" << std::endl;
        std::cout << "3. Haptic (GameInput 2.0 - modern)" << std::endl;
        std::cout << "4. Hybrid (try both APIs)" << std::endl;
        std::cout << "5. Haptic Emulation (strong bursts)" << std::endl;
        std::cout << "Select (1-5) or press any other key to return: ";

        char choice = _getch();
        
        switch (choice) {
            case '1':
                settings.preferredMode = HapticController::HapticMode::Auto;
                break;
            case '2':
                settings.preferredMode = HapticController::HapticMode::Rumble;
                break;
            case '3':
                settings.preferredMode = HapticController::HapticMode::Haptic;
                break;
            case '4':
                settings.preferredMode = HapticController::HapticMode::Hybrid;
                break;
            case '5':
                settings.preferredMode = HapticController::HapticMode::HapticEmulation;
                break;
            default:
                std::cout << "\n";
                return;
        }

        m_hapticController.SetHapticSettings(settings);
        std::cout << "\nHaptic mode updated. Reinitializing..." << std::endl;
        
        // Reinitialize to apply new mode
        m_hapticController.Shutdown();
        if (m_hapticController.Initialize()) {
            std::cout << "New mode: " << m_hapticController.GetHapticModeString() << std::endl;
        } else {
            std::cout << "Failed to reinitialize with new mode!" << std::endl;
        }
        
        std::cout << "Press any key to continue..." << std::endl;
        _getch();
        std::cout << "\n";
    }

    void RefreshDevices() {
        std::cout << "\n\nRefreshing devices..." << std::endl;
        m_hapticController.FindGamepads();
        std::cout << m_hapticController.GetDeviceStatusString() << std::endl;
        std::cout << "Haptic mode: " << m_hapticController.GetHapticModeString() << std::endl;
        std::cout << "Press any key to continue..." << std::endl;
        _getch();
        std::cout << "\n";
    }

    AudioCaptureManager m_audioCapture;
    AudioProcessor m_audioProcessor;
    HapticController m_hapticController;
    
    std::mutex m_featuresMutex;
    AudioProcessor::AudioFeatures m_latestFeatures{};
    bool m_running = false;
};

int main(int argc, char* argv[]) {
    try {
        // Check for command-line arguments
        if (argc > 1) {
            std::string arg = argv[1];
            if (arg == "--console") {
                // Run as console application
                AudioHapticsApp app;
                if (!app.Initialize()) {
                    std::cerr << "Failed to initialize application" << std::endl;
                    return -1;
                }
                app.Run();
                return 0;
            }
            else if (arg == "--service") {
                // Run as service (non-blocking)
                AudioHapticsApp app;
                if (!app.Initialize()) {
                    std::cerr << "Failed to initialize service" << std::endl;
                    return -1;
                }
                app.RunService();
                return 0;
            }
            else if (arg == "--help") {
                std::cout << "Audio-to-Haptics Usage:" << std::endl;
                std::cout << "  --console    Run as console application (default)" << std::endl;
                std::cout << "  --service    Run as background service" << std::endl;
                std::cout << "  --help       Show this help message" << std::endl;
                return 0;
            }
        }

        // Default: run as console application
        AudioHapticsApp app;
        
        if (!app.Initialize()) {
            std::cerr << "Failed to initialize application" << std::endl;
            return -1;
        }
        
        app.Run();
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}