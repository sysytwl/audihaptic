#include "HapticController.h"
#include <iostream>
#include <algorithm>

HapticController::HapticController()
    : m_gameInput(nullptr)
    , m_activeMode(HapticMode::Auto)
    , m_lastUpdate(std::chrono::steady_clock::now())
    , m_lastHapticBurst(std::chrono::steady_clock::now())
    , m_hapticBurstActive(false)
    , m_hapticBurstStart(std::chrono::steady_clock::now())
    , m_leftMotorTurn(true)
{
}

HapticController::~HapticController() {
    Shutdown();
}

bool HapticController::Initialize() {
    HRESULT hr = GameInputCreate(&m_gameInput);
    if (FAILED(hr)) {
        std::cerr << "Failed to create GameInput: " << std::hex << hr << std::endl;
        return false;
    }

    std::cout << "GameInput initialized successfully" << std::endl;
    
    // Determine the best haptic mode based on API version and settings
    switch (m_settings.preferredMode) {
        case HapticMode::Auto:
            // Try haptic first (GameInput 2.0), fall back to rumble (GameInput 1.0)
            #if GAMEINPUT_API_VERSION >= 2
                m_activeMode = HapticMode::Haptic;
                std::cout << "Using GameInput 2.0 Haptic API (auto-detected)" << std::endl;
            #else
                m_activeMode = HapticMode::Rumble;
                std::cout << "Using GameInput 1.0 Rumble API (auto-detected)" << std::endl;
            #endif
            break;
        case HapticMode::Haptic:
            m_activeMode = HapticMode::Haptic;
            std::cout << "Using GameInput 2.0 Haptic API (forced)" << std::endl;
            break;
        case HapticMode::Rumble:
            m_activeMode = HapticMode::Rumble;
            std::cout << "Using GameInput 1.0 Rumble API (forced)" << std::endl;
            break;
        case HapticMode::Hybrid:
            m_activeMode = HapticMode::Hybrid;
            std::cout << "Using Hybrid mode (both APIs)" << std::endl;
            break;
        case HapticMode::HapticEmulation:
            m_activeMode = HapticMode::HapticEmulation;
            std::cout << "Using Haptic Emulation mode (strong bursts)" << std::endl;
            break;
    }
    
    // Find initial gamepads
    FindGamepads();
    
    return true;
}

void HapticController::Shutdown() {
    StopAllHaptics();
    CleanupDevices();
    
    if (m_gameInput) {
        m_gameInput->Release();
        m_gameInput = nullptr;
    }
}

bool HapticController::FindGamepads() {
    if (!m_gameInput) {
        return false;
    }

    // Enumerate gamepad devices without cleaning up existing ones
    IGameInputReading* reading = nullptr;
    HRESULT hr = m_gameInput->GetCurrentReading(GameInputKindGamepad, nullptr, &reading);
    
    bool foundNewDevice = false;
    
    if (SUCCEEDED(hr) && reading) {
        IGameInputDevice* device = nullptr;
        reading->GetDevice(&device);
        if (device) {
            // Check if we already have this device
            bool found = false;
            for (const auto& gamepad : m_gamepads) {
                if (gamepad.device == device) {
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                GamepadInfo info;
                info.device = device;
                info.device->AddRef(); // Keep reference
                info.lastUpdate = std::chrono::steady_clock::now();
                
                // Detect device capabilities
                DetectDeviceCapabilities(info);
                
                m_gamepads.push_back(info);
                foundNewDevice = true;
                
                std::cout << "Found new gamepad device - Rumble: " << (info.supportsRumble ? "Yes" : "No") 
                         << ", Haptics: " << (info.supportsHaptics ? "Yes" : "No") << std::endl;
                std::cout << "Total gamepads found: " << m_gamepads.size() << std::endl;
            }
        }
        reading->Release();
    }

    // Only print total if this is the initial scan or we found a new device
    static bool initialScanDone = false;
    if (!initialScanDone) {
        std::cout << "Total gamepads found: " << m_gamepads.size() << std::endl;
        initialScanDone = true;
    }
    
    return !m_gamepads.empty();
}

void HapticController::UpdateDevices() {
    // Periodically check for new devices (less frequently)
    auto now = std::chrono::steady_clock::now();
    static auto lastDeviceCheck = now;
    
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDeviceCheck).count() > 5000) {
        // Only scan for new devices, don't clean up existing ones
        FindGamepads();
        lastDeviceCheck = now;
    }
}

void HapticController::ProcessAudioFeatures(const AudioProcessor::AudioFeatures& features) {
    if (m_gamepads.empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - m_lastUpdate).count();
    m_lastUpdate = now;

    // Update haptics for all connected gamepads
    for (auto& gamepad : m_gamepads) {
        UpdateGamepadHaptics(gamepad, features);
    }
}

void HapticController::UpdateGamepadHaptics(GamepadInfo& gamepad, const AudioProcessor::AudioFeatures& features) {
    if (!gamepad.device) {
        return;
    }

    // Calculate target intensities based on audio features
    float targetLeftMotor = 0.0f;
    float targetRightMotor = 0.0f;
    float targetLeftTrigger = 0.0f;
    float targetRightTrigger = 0.0f;

    if (m_settings.useRumbleMotors) {
        // Map bass to left motor, treble to right motor
        if (m_settings.useLowFrequencyMotor) {
            targetLeftMotor = features.bass * m_settings.bassIntensity;
        }
        
        if (m_settings.useHighFrequencyMotor) {
            targetRightMotor = features.treble * m_settings.trebleIntensity;
        }
        
        // Add overall volume contribution to both motors
        float volumeContribution = features.volume * m_settings.volumeIntensity * 0.5f;
        targetLeftMotor += volumeContribution;
        targetRightMotor += volumeContribution;
    }

    if (m_settings.useImpulseMotor) {
        // Use trigger motors for dynamic range and peaks
        float dynamicContribution = features.dynamic_range * m_settings.dynamicIntensity;
        targetLeftTrigger = dynamicContribution;
        targetRightTrigger = dynamicContribution;
        
        // Add peak information to triggers
        float peakContribution = features.peak * 0.3f;
        targetLeftTrigger += peakContribution;
        targetRightTrigger += peakContribution;
    }

    // Clamp values to valid range [0, 1]
    targetLeftMotor = std::clamp(targetLeftMotor, 0.0f, 1.0f);
    targetRightMotor = std::clamp(targetRightMotor, 0.0f, 1.0f);
    targetLeftTrigger = std::clamp(targetLeftTrigger, 0.0f, 1.0f);
    targetRightTrigger = std::clamp(targetRightTrigger, 0.0f, 1.0f);

    // Apply smooth transitions
    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - gamepad.lastUpdate).count();
    gamepad.lastUpdate = now;

    gamepad.currentLeftMotor = SmoothTransition(gamepad.currentLeftMotor, targetLeftMotor, deltaTime);
    gamepad.currentRightMotor = SmoothTransition(gamepad.currentRightMotor, targetRightMotor, deltaTime);
    gamepad.currentLeftTrigger = SmoothTransition(gamepad.currentLeftTrigger, targetLeftTrigger, deltaTime);
    gamepad.currentRightTrigger = SmoothTransition(gamepad.currentRightTrigger, targetRightTrigger, deltaTime);

    // Apply haptic feedback using GameInput 2.0 API
    GameInputRumbleParams params = {};
    params.lowFrequency = (gamepad.currentLeftMotor <= 0.15f) ? 0 : gamepad.currentLeftMotor;
    params.highFrequency = (gamepad.currentRightMotor <= 0.15f) ? 0 : gamepad.currentRightMotor;
    params.leftTrigger = (gamepad.currentLeftTrigger <= 0.15f) ? 0 : gamepad.currentLeftTrigger;
    params.rightTrigger = (gamepad.currentRightTrigger <= 0.15f) ? 0 : gamepad.currentRightTrigger;
    
    gamepad.device->SetRumbleState(&params);
}

float HapticController::SmoothTransition(float current, float target, float deltaTime) {
    if (m_settings.fadeTimeMs == 0) {
        return target;
    }
    
    float fadeRate = 1000.0f / static_cast<float>(m_settings.fadeTimeMs); // Convert ms to rate per second
    float maxChange = fadeRate * deltaTime;
    
    float difference = target - current;
    if (std::abs(difference) <= maxChange) {
        return target;
    }
    
    return current + (difference > 0 ? maxChange : -maxChange);
}

void HapticController::SetRumble(float leftMotor, float rightMotor, float leftTrigger, float rightTrigger) {
    leftMotor = std::clamp(leftMotor, 0.0f, 1.0f);
    rightMotor = std::clamp(rightMotor, 0.0f, 1.0f);
    leftTrigger = std::clamp(leftTrigger, 0.0f, 1.0f);
    rightTrigger = std::clamp(rightTrigger, 0.0f, 1.0f);

    // Handle haptic emulation mode
    if (m_activeMode == HapticMode::HapticEmulation) {
        ProcessHapticEmulation(leftMotor, rightMotor, leftTrigger, rightTrigger);
        return;
    }

    // Standard rumble mode
    for (auto& gamepad : m_gamepads) {
        if (gamepad.device) {
            GameInputRumbleParams params = {};
            params.lowFrequency = leftMotor;
            params.highFrequency = rightMotor;
            params.leftTrigger = leftTrigger;
            params.rightTrigger = rightTrigger;
            
            gamepad.device->SetRumbleState(&params);
            
            gamepad.currentLeftMotor = leftMotor;
            gamepad.currentRightMotor = rightMotor;
            gamepad.currentLeftTrigger = leftTrigger;
            gamepad.currentRightTrigger = rightTrigger;
        }
    }
}

void HapticController::StopAllHaptics() {
    for (auto& gamepad : m_gamepads) {
        if (gamepad.device) {
            GameInputRumbleParams params = {};
            params.lowFrequency = 0.0f;
            params.highFrequency = 0.0f;
            params.leftTrigger = 0.0f;
            params.rightTrigger = 0.0f;
            
            gamepad.device->SetRumbleState(&params);
            
            gamepad.currentLeftMotor = 0.0f;
            gamepad.currentRightMotor = 0.0f;
            gamepad.currentLeftTrigger = 0.0f;
            gamepad.currentRightTrigger = 0.0f;
        }
    }
}

void HapticController::CleanupDevices() {
    for (auto& gamepad : m_gamepads) {
        if (gamepad.device) {
            // Stop all haptic feedback before cleanup
            GameInputRumbleParams params = {};
            gamepad.device->SetRumbleState(&params);
            gamepad.device->Release();
        }
    }
    m_gamepads.clear();
}

const char* HapticController::GetHapticModeString() const {
    switch (m_activeMode) {
        case HapticMode::Auto: return "Auto";
        case HapticMode::Rumble: return "Rumble (GameInput 1.0)";
        case HapticMode::Haptic: return "Haptic (GameInput 2.0)";
        case HapticMode::Hybrid: return "Hybrid (Both APIs)";
        case HapticMode::HapticEmulation: return "Haptic Emulation (Strong Bursts)";
        default: return "Unknown";
    }
}

void HapticController::ProcessHapticEmulation(float leftMotor, float rightMotor, float leftTrigger, float rightTrigger) {
    auto now = std::chrono::steady_clock::now();
    
    // Calculate overall intensity from all inputs
    float totalIntensity = (leftMotor + rightMotor + leftTrigger + rightTrigger) / 4.0f;
    totalIntensity *= m_settings.emulationIntensity;
    
    // Check if we should trigger a new haptic burst
    bool shouldTriggerBurst = false;
    
    // Only trigger if there's significant intensity AND volume is above threshold
    if (totalIntensity > 0.1f && totalIntensity >= m_settings.emulationVolumeThreshold) {
        auto timeSinceLastBurst = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastHapticBurst).count() / 1000.0f;
        
        if (timeSinceLastBurst >= m_settings.emulationMinInterval) {
            shouldTriggerBurst = true;
            m_lastHapticBurst = now;
            m_hapticBurstActive = true;
            m_hapticBurstStart = now;
            
            // Alternate between left and right motor
            m_leftMotorTurn = !m_leftMotorTurn;
        }
    }
    
    // Check if current burst should end
    if (m_hapticBurstActive) {
        auto burstDuration = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_hapticBurstStart).count() / 1000.0f;
        if (burstDuration >= m_settings.emulationBurstDuration) {
            m_hapticBurstActive = false;
        }
    }
    
    // Apply haptic burst or stop
    for (auto& gamepad : m_gamepads) {
        if (gamepad.device) {
            GameInputRumbleParams params = {};
            
            if (m_hapticBurstActive || shouldTriggerBurst) {
                // Strong, short burst - 3x intensity, alternating left/right
                float burstIntensity = std::clamp(totalIntensity, 0.0f, 1.0f);
                
                if (m_leftMotorTurn) {
                    // Left motor burst
                    params.lowFrequency = burstIntensity;    // Strong left motor
                    params.highFrequency = 0.0f;             // Silent right motor
                } else {
                    // Right motor burst  
                    params.lowFrequency = 0.0f;              // Silent left motor
                    params.highFrequency = burstIntensity;   // Strong right motor
                }
                
                // Light trigger feedback for both
                params.leftTrigger = burstIntensity * 0.3f;
                params.rightTrigger = burstIntensity * 0.3f;
            } else {
                // No haptic feedback - complete silence between bursts
                params.lowFrequency = 0.0f;
                params.highFrequency = 0.0f;
                params.leftTrigger = 0.0f;
                params.rightTrigger = 0.0f;
            }
            
            gamepad.device->SetRumbleState(&params);
            
            gamepad.currentLeftMotor = params.lowFrequency;
            gamepad.currentRightMotor = params.highFrequency;
            gamepad.currentLeftTrigger = params.leftTrigger;
            gamepad.currentRightTrigger = params.rightTrigger;
        }
    }
}

void HapticController::DetectDeviceCapabilities(GamepadInfo& gamepad) {
    if (!gamepad.device) return;

    const GameInputDeviceInfo* deviceInfo = nullptr;
    HRESULT hr = gamepad.device->GetDeviceInfo(&deviceInfo);

    if (SUCCEEDED(hr) && deviceInfo) {
        // GameInput 2.0 supports rumble via SetRumbleState
        gamepad.supportsRumble = true;
        gamepad.rumbleMotorCount = 4; // Low/High frequency + Left/Right triggers
        
        // Check for haptic support
        GameInputHapticInfo hapticInfo = {};
        hr = gamepad.device->GetHapticInfo(&hapticInfo);
        if (SUCCEEDED(hr)) {
            gamepad.supportsHaptics = true;
            gamepad.hapticMotorCount = hapticInfo.locationCount;
        } else {
            gamepad.supportsHaptics = false;
            gamepad.hapticMotorCount = 0;
        }
        
        // Device capabilities detected (details shown in device discovery)
    } else {
        std::cerr << "Failed to get device info: " << std::hex << hr << std::endl;
        // Default to basic rumble support
        gamepad.supportsRumble = true;
        gamepad.rumbleMotorCount = 4;
        gamepad.supportsHaptics = false;
        gamepad.hapticMotorCount = 0;
    }
}

