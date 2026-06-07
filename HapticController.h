#pragma once

#include "GameInputConfig.h"
#include <GameInput.h>
#include <memory>
#include <vector>
#include <chrono>
#include "AudioProcessor.h"


// Use appropriate GameInput namespace
#if GAMEINPUT_API_VERSION >= 2
using namespace GameInput::v2;
#elif GAMEINPUT_API_VERSION >= 1
using namespace GameInput::v1;
#endif

class HapticController {
public:
    enum class HapticMode {
        Auto,           // Automatically detect best available mode
        Rumble,         // Use traditional rumble API (GameInput 1.0)
        Haptic,         // Use modern haptic API (GameInput 2.0)
        Hybrid,         // Use both if available
        HapticEmulation // Strong, short bursts to simulate haptics
    };

    struct HapticSettings {
        float bassIntensity = 1.0f;      // Intensity multiplier for bass (0.0 - 2.0)
        float trebleIntensity = 0.0f;    // Intensity multiplier for treble (0.0 - 2.0)
        float volumeIntensity = 0.0f;    // Intensity multiplier for overall volume (0.0 - 2.0)
        float dynamicIntensity = 0.0f;   // Intensity multiplier for dynamic range (0.0 - 2.0)
        
        // Motor assignments (which motors to use for different frequency ranges)
        bool useLowFrequencyMotor = true;   // Use low-frequency motor for bass
        bool useHighFrequencyMotor = true;  // Use high-frequency motor for treble
        bool useImpulseMotor = true;        // Use impulse triggers for dynamics
        bool useRumbleMotors = true;        // Use traditional rumble motors
        
        // Timing settings
        uint32_t updateRateMs = 16;         // Update rate in milliseconds (~60 FPS)
        uint32_t fadeTimeMs = 100;          // Fade time for smooth transitions
        
        // API preference
        HapticMode preferredMode = HapticMode::HapticEmulation;  // Preferred haptic mode
        
        // Haptic emulation settings
        float emulationBurstDuration = 0.05f;  // Duration of haptic bursts in seconds (0.01 - 0.2)
        float emulationMinInterval = 0.1f;     // Minimum interval between bursts in seconds (0.05 - 0.5)
        float emulationIntensity = 3.0f;       // Intensity multiplier for emulated haptics (3x stronger)
        float emulationVolumeThreshold = 0.3f; // Volume threshold - no haptics below 30%
    };

    HapticController();
    ~HapticController();

    bool Initialize();
    void Shutdown();

    // Device management
    bool FindGamepads();
    size_t GetGamepadCount() const { return m_gamepads.size(); }
    std::string GetDeviceStatusString() const {
        return "Connected gamepads: " + std::to_string(m_gamepads.size());
    }
    
    // Haptic feedback
    void ProcessAudioFeatures(const AudioProcessor::AudioFeatures& features);
    void SetHapticSettings(const HapticSettings& settings) { m_settings = settings; }
    const HapticSettings& GetHapticSettings() const { return m_settings; }
    
    // Manual control
    void SetRumble(float leftMotor, float rightMotor, float leftTrigger = 0.0f, float rightTrigger = 0.0f);
    void StopAllHaptics();
    
    // Status
    bool IsInitialized() const { return m_gameInput != nullptr; }
    void UpdateDevices(); // Call periodically to detect new/removed devices
    HapticMode GetActiveHapticMode() const { return m_activeMode; }
    const char* GetHapticModeString() const;

private:
    struct GamepadInfo {
        IGameInputDevice* device;
        std::chrono::steady_clock::time_point lastUpdate;
        
        // Device capabilities
        bool supportsRumble;
        bool supportsHaptics;
        uint32_t hapticMotorCount;
        uint32_t rumbleMotorCount;
        
        // Current haptic state
        float currentLeftMotor;
        float currentRightMotor;
        float currentLeftTrigger;
        float currentRightTrigger;
        
        GamepadInfo() : device(nullptr), supportsRumble(false), supportsHaptics(false),
                       hapticMotorCount(0), rumbleMotorCount(0),
                       currentLeftMotor(0), currentRightMotor(0),
                       currentLeftTrigger(0), currentRightTrigger(0) {}
    };

    void CleanupDevices();
    void UpdateGamepadHaptics(GamepadInfo& gamepad, const AudioProcessor::AudioFeatures& features);
    float SmoothTransition(float current, float target, float deltaTime);
    
    // Device capability detection
    void DetectDeviceCapabilities(GamepadInfo& gamepad);
    
    // Haptic emulation
    void ProcessHapticEmulation(float leftMotor, float rightMotor, float leftTrigger, float rightTrigger);

    
    // GameInput
    IGameInput* m_gameInput;
    std::vector<GamepadInfo> m_gamepads;
    

    
    // Settings
    HapticSettings m_settings;
    HapticMode m_activeMode;
    
    // Timing
    std::chrono::steady_clock::time_point m_lastUpdate;
    
    // Haptic emulation state
    std::chrono::steady_clock::time_point m_lastHapticBurst;
    bool m_hapticBurstActive;
    std::chrono::steady_clock::time_point m_hapticBurstStart;
    bool m_leftMotorTurn;  // Alternates between left and right motor
};