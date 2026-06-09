package com.example.audihaptic

import android.content.Context
import android.hardware.input.InputManager
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.view.InputDevice
import kotlin.math.min

object NativeLib {
    init {
        try {
            System.loadLibrary("audihaptic_native")
        } catch (e: Throwable) {
            android.util.Log.e("AudioHaptic", "Failed to load native library: ${e.message}")
        }
    }

    external fun nativeProcessAudio(data: ShortArray, length: Int, sampleRate: Int): DoubleArray
    external fun nativeSetAnalysisMode(mode: Int)
    external fun nativeSetSensitivity(sensitivity: Float)

    private var m_lastUpdate: Long = System.nanoTime()
    private var m_currentIntensity = 0f
    private var m_currentGamepadIntensity = 0f
    private var m_useSmoothing = true

    fun setSmoothing(enabled: Boolean) {
        m_useSmoothing = enabled
    }

    fun step(context: Context, features: DoubleArray, sensitivity: Float = 1.0f) {
        val volume = features.getOrNull(0) ?: 0.0
        val bass = features.getOrNull(1) ?: 0.0
        val treble = features.getOrNull(3) ?: 0.0

        val now = System.nanoTime()
        val duration = (now - m_lastUpdate)
        val deltaTime = duration / 1_000_000_000f
        m_lastUpdate = now

        // Use bass for phone vibrator, treble for gamepad
        val targetIntensity = (bass * sensitivity).coerceIn(0.0, 1.0).toFloat()
        val targetGamepadIntensity = (treble * sensitivity).coerceIn(0.0, 1.0).toFloat()

        // Apply smooth transitions like HapticController.cpp
        if (m_useSmoothing) {
            m_currentIntensity = smoothTransition(m_currentIntensity, targetIntensity, deltaTime)
            m_currentGamepadIntensity = smoothTransition(m_currentGamepadIntensity, targetGamepadIntensity, deltaTime)
        } else {
            m_currentIntensity = targetIntensity
            m_currentGamepadIntensity = targetGamepadIntensity
        }

        val amp = (m_currentIntensity * 255).toInt().coerceIn(0, 255)
        val gamepadAmp = (m_currentGamepadIntensity * 255).toInt().coerceIn(0, 255)




        if (amp > 40) {
            // Vibrate phone
            val vibrator = context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
            vibrator?.vibrateCompat(duration / 1_000_000, amp)

            // Try to vibrate gamepads
            val inputManager = context.getSystemService(Context.INPUT_SERVICE) as? InputManager
            if (inputManager != null) {
                for (id in InputDevice.getDeviceIds()) {
                    val device = InputDevice.getDevice(id)
                    if (device != null && (device.sources and InputDevice.SOURCE_GAMEPAD) != 0) {
                        device.vibrator.vibrateCompat(duration / 1_000_000, gamepadAmp)
                    }
                }
            }
        }
    }

    private fun smoothTransition(current: Float, target: Float, deltaTime: Float): Float {
        val fadeTimeMs = 100f
        val fadeRate = 1.0f / (fadeTimeMs / 1000f) // rate per second
        val maxChange = fadeRate * deltaTime
        
        val difference = target - current
        return if (Math.abs(difference) <= maxChange) {
            target
        } else {
            current + if (difference > 0) maxChange else -maxChange
        }
    }

    private fun Vibrator.vibrateCompat(durationMs: Long, amplitude: Int) {
        if (durationMs <= 0 || amplitude <= 0 || !hasVibrator()) return

        // Amplitude must be between 1 and 255 for createOneShot
        val safeAmplitude = amplitude.coerceIn(1, 255)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            try {
                vibrate(VibrationEffect.createOneShot(durationMs, safeAmplitude))
            } catch (e: Exception) {
                // Log exception to help with debugging
                android.util.Log.e("AudioHaptic", "Vibration failed: ${e.message}")
            }
        } else {
            @Suppress("DEPRECATION")
            vibrate(durationMs)
        }
    }
}
