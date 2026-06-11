package com.example.audihaptic

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import com.google.android.material.button.MaterialButton
import com.google.android.material.progressindicator.LinearProgressIndicator
import com.google.android.material.slider.Slider
import com.google.android.material.switchmaterial.SwitchMaterial

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
    
    private var currentSensitivity = 2.0f
    private var currentMode = 0 // 0: Time, 1: Freq
    private var useSmoothing = true
    private var ampCutoff = 0.15f
    private var bassCutoff = 220f
    private var trebleCutoff = 3000f

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
            text = getString(R.string.status_idle)
            textSize = 18f
            setPadding(0, 0, 0, 32)
        }
        container.addView(statusText)

        // Mode Selection
        container.addView(TextView(this).apply { text = getString(R.string.analysis_mode) })
        val modeGroup = RadioGroup(this).apply {
            orientation = RadioGroup.HORIZONTAL
            gravity = Gravity.CENTER
        }
        val rbLPF = RadioButton(this).apply { text = getString(R.string.mode_lpf); id = View.generateViewId(); isChecked = true }
        val rbFFT = RadioButton(this).apply { text = getString(R.string.mode_fft); id = View.generateViewId() }
        val fftId = rbFFT.id
        modeGroup.addView(rbLPF)
        modeGroup.addView(rbFFT)
        modeGroup.setOnCheckedChangeListener { _, checkedId ->
            currentMode = if (checkedId == fftId) 1 else 0
            updateServiceSettings()
        }
        container.addView(modeGroup)

        // Smoothing Switch
        val smoothingSwitch = SwitchMaterial(this).apply {
            text = getString(R.string.use_smoothing)
            isChecked = useSmoothing
            setOnCheckedChangeListener { _, isChecked ->
                useSmoothing = isChecked
                updateServiceSettings()
            }
        }
        container.addView(smoothingSwitch)

        // Sensitivity (Ratio)
        container.addView(TextView(this).apply { text = getString(R.string.haptic_ratio) })
        val sensitivitySlider = Slider(this).apply {
            valueFrom = 0.1f
            valueTo = 6.0f
            value = currentSensitivity
            addOnChangeListener { _, value, _ ->
                currentSensitivity = value
                updateServiceSettings()
            }
        }
        container.addView(sensitivitySlider)

        // Amp Cutoff
        container.addView(TextView(this).apply { text = getString(R.string.amp_cutoff) })
        val ampCutoffSlider = Slider(this).apply {
            valueFrom = 0.0f
            valueTo = 0.5f
            value = ampCutoff
            addOnChangeListener { _, value, _ ->
                ampCutoff = value
                updateServiceSettings()
            }
        }
        container.addView(ampCutoffSlider)

        // Bass Cutoff
        container.addView(TextView(this).apply { text = getString(R.string.bass_cutoff) })
        val bassSlider = Slider(this).apply {
            valueFrom = 20f
            valueTo = 1000f
            value = bassCutoff
            addOnChangeListener { _, value, _ ->
                bassCutoff = value
                updateServiceSettings()
            }
        }
        container.addView(bassSlider)

        // Treble Cutoff
        container.addView(TextView(this).apply { text = getString(R.string.treble_cutoff) })
        val trebleSlider = Slider(this).apply {
            valueFrom = 1000f
            valueTo = 15000f
            value = trebleCutoff
            addOnChangeListener { _, value, _ ->
                trebleCutoff = value
                updateServiceSettings()
            }
        }
        container.addView(trebleSlider)

        // Bars
        container.addView(TextView(this).apply { text = getString(R.string.audio_power_in) })
        powerInBar = LinearProgressIndicator(this).apply {
            trackCornerRadius = 8
            setPadding(0, 0, 0, 24)
        }
        container.addView(powerInBar)

        container.addView(TextView(this).apply { text = getString(R.string.audio_power_out) })
        powerOutBar = LinearProgressIndicator(this).apply {
            trackCornerRadius = 8
            setPadding(0, 0, 0, 48)
        }
        container.addView(powerOutBar)

        startBtn = MaterialButton(this).apply {
            text = getString(R.string.start_capture)
            setOnClickListener {
                if (isServiceRunning()) stopAudioCapture() else requestCapture()
            }
        }
        container.addView(startBtn)

        setContentView(root)
    }

    private fun updateServiceSettings() {
        val intent = Intent(this, CaptureService::class.java).apply {
            action = CaptureService.ACTION_UPDATE_SETTINGS
            putExtra(CaptureService.EXTRA_SENSITIVITY, currentSensitivity)
            putExtra(CaptureService.EXTRA_MODE, currentMode)
            putExtra(CaptureService.EXTRA_SMOOTHING, useSmoothing)
            putExtra(CaptureService.EXTRA_AMP_CUTOFF, ampCutoff)
            putExtra(CaptureService.EXTRA_BASS_CUTOFF, bassCutoff)
            putExtra(CaptureService.EXTRA_TREBLE_CUTOFF, trebleCutoff)
        }
        startService(intent)
    }

    private fun isServiceRunning(): Boolean = startBtn.text == getString(R.string.stop_capture)

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
            putExtra(CaptureService.EXTRA_AMP_CUTOFF, ampCutoff)
            putExtra(CaptureService.EXTRA_BASS_CUTOFF, bassCutoff)
            putExtra(CaptureService.EXTRA_TREBLE_CUTOFF, trebleCutoff)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(intent) else startService(intent)
        startBtn.text = getString(R.string.stop_capture)
        statusText.text = getString(R.string.status_capturing)
    }

    private fun stopAudioCapture() {
        startService(Intent(this, CaptureService::class.java).apply { action = CaptureService.ACTION_STOP })
        startBtn.text = getString(R.string.start_capture)
        statusText.text = getString(R.string.status_stopped)
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
