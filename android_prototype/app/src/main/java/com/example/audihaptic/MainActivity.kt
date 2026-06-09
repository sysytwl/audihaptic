package com.example.audihaptic

import android.Manifest
import android.app.Activity
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.*
import androidx.annotation.RequiresApi
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import com.google.android.material.button.MaterialButton
import com.google.android.material.progressindicator.LinearProgressIndicator
import com.google.android.material.slider.Slider

class MainActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "AudioHaptic"
        const val REQUEST_MEDIA_PROJECTION = 1001
        const val REQUEST_AUDIO_PERMISSION = 1002
    }

    private lateinit var startBtn: MaterialButton
    private lateinit var statusText: TextView
    private lateinit var powerInBar: LinearProgressIndicator
    private lateinit var powerOutBar: LinearProgressIndicator
    private lateinit var sensitivitySlider: Slider
    
    private var currentSensitivity = 1.0f
    private var currentMode = 0 // 0: Time, 1: Freq
    private var useSmoothing = true

    private val statsReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            val pIn = intent?.getFloatExtra(CaptureService.EXTRA_POWER_IN, 0f) ?: 0f
            val pOut = intent?.getFloatExtra(CaptureService.EXTRA_POWER_OUT, 0f) ?: 0f
            
            powerInBar.progress = (pIn * 100).toInt()
            powerOutBar.progress = (pOut * 100).toInt()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val root = ScrollView(this).apply {
            layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
            isFillViewport = true
        }

        val container = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(48, 48, 48, 48)
        }
        root.addView(container)

        statusText = TextView(this).apply {
            text = "Status: Idle"
            textSize = 18f
            setPadding(0, 0, 0, 32)
        }
        container.addView(statusText)

        // Mode Selection
        container.addView(TextView(this).apply { text = "Analysis Mode" })
        val modeGroup = RadioGroup(this).apply {
            orientation = RadioGroup.HORIZONTAL
            gravity = Gravity.CENTER
        }
        val rbLPF = RadioButton(this).apply { text = "LPF (Time)"; id = View.generateViewId(); isChecked = true }
        val rbFFT = RadioButton(this).apply { text = "FFT (Freq)"; id = View.generateViewId() }
        val fftId = rbFFT.id
        modeGroup.addView(rbLPF)
        modeGroup.addView(rbFFT)
        modeGroup.setOnCheckedChangeListener { _, checkedId ->
            currentMode = if (checkedId == fftId) 1 else 0
            updateServiceSettings()
        }
        container.addView(modeGroup)

        // Sensitivity (Ratio)
        container.addView(TextView(this).apply { text = "Haptic Ratio / Sensitivity" })
        sensitivitySlider = Slider(this).apply {
            valueFrom = 0.1f
            valueTo = 5.0f
            value = 1.0f
            addOnChangeListener { _, value, _ ->
                currentSensitivity = value
                updateServiceSettings()
            }
        }
        container.addView(sensitivitySlider)

        // Smoothing Switch
        container.addView(CheckBox(this).apply {
            text = "Smooth Transitions"
            isChecked = true
            setOnCheckedChangeListener { _, isChecked ->
                useSmoothing = isChecked
                updateServiceSettings()
            }
        })

        // Bars
        container.addView(TextView(this).apply { text = "Audio Power In" })
        powerInBar = LinearProgressIndicator(this).apply {
            trackCornerRadius = 8
            setPadding(0, 0, 0, 24)
        }
        container.addView(powerInBar)

        container.addView(TextView(this).apply { text = "Audio Power Out" })
        powerOutBar = LinearProgressIndicator(this).apply {
            trackCornerRadius = 8
            setPadding(0, 0, 0, 48)
        }
        container.addView(powerOutBar)

        startBtn = MaterialButton(this).apply {
            text = "Start Capture"
            setOnClickListener {
                if (isServiceRunning()) stopAudioCapture() else requestCapture()
            }
        }
        container.addView(startBtn)

        setContentView(root)
    }

    private fun updateServiceSettings() {
        if (isServiceRunning()) {
            val intent = Intent(this, CaptureService::class.java).apply {
                action = CaptureService.ACTION_UPDATE_SETTINGS
                putExtra(CaptureService.EXTRA_SENSITIVITY, currentSensitivity)
                putExtra(CaptureService.EXTRA_MODE, currentMode)
                putExtra(CaptureService.EXTRA_SMOOTHING, useSmoothing)
            }
            startService(intent)
        }
    }

    private fun isServiceRunning(): Boolean = startBtn.text == "Stop Capture"

    private fun requestCapture() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.RECORD_AUDIO), REQUEST_AUDIO_PERMISSION)
        } else {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                val mgr = getSystemService(MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
                startActivityForResult(mgr.createScreenCaptureIntent(), REQUEST_MEDIA_PROJECTION)
            }
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_MEDIA_PROJECTION && resultCode == RESULT_OK && data != null) {
            startAudioCapture(resultCode, data)
        }
    }

    private fun startAudioCapture(resultCode: Int, data: Intent) {
        val intent = Intent(this, CaptureService::class.java).apply {
            putExtra(CaptureService.EXTRA_RESULT_CODE, resultCode)
            putExtra(CaptureService.EXTRA_DATA, data)
            putExtra(CaptureService.EXTRA_SENSITIVITY, currentSensitivity)
            putExtra(CaptureService.EXTRA_MODE, currentMode)
            putExtra(CaptureService.EXTRA_SMOOTHING, useSmoothing)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(intent) else startService(intent)
        startBtn.text = "Stop Capture"
        statusText.text = "Status: Capturing..."
    }

    private fun stopAudioCapture() {
        startService(Intent(this, CaptureService::class.java).apply { action = CaptureService.ACTION_STOP })
        startBtn.text = "Start Capture"
        statusText.text = "Status: Stopped"
    }

    override fun onResume() {
        super.onResume()
        LocalBroadcastManager.getInstance(this).registerReceiver(statsReceiver, IntentFilter(CaptureService.ACTION_STATS_UPDATE))
    }

    override fun onPause() {
        super.onPause()
        LocalBroadcastManager.getInstance(this).unregisterReceiver(statsReceiver)
    }
}
