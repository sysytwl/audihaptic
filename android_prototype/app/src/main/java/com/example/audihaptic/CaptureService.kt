package com.example.audihaptic

import android.app.Activity
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import kotlin.concurrent.thread
import kotlin.math.max

class CaptureService : Service() {

    companion object {
        const val CHANNEL_ID = "AudioCaptureChannel"
        const val NOTIFICATION_ID = 1
        const val EXTRA_RESULT_CODE = "extra_result_code"
        const val EXTRA_DATA = "extra_data"
        const val ACTION_STOP = "com.example.audihaptic.ACTION_STOP"
        const val ACTION_UPDATE_SETTINGS = "com.example.audihaptic.ACTION_UPDATE_SETTINGS"
        const val EXTRA_SENSITIVITY = "extra_sensitivity"
        const val EXTRA_MODE = "extra_mode"
        const val EXTRA_SMOOTHING = "extra_smoothing"
        const val EXTRA_AMP_CUTOFF = "extra_amp_cutoff"
        const val EXTRA_BASS_CUTOFF = "extra_bass_cutoff"
        const val EXTRA_TREBLE_CUTOFF = "extra_treble_cutoff"

        const val ACTION_STATS_UPDATE = "com.example.audihaptic.ACTION_STATS_UPDATE"
        const val EXTRA_POWER_IN = "extra_power_in"
        const val EXTRA_POWER_OUT = "extra_power_out"
    }

    private var mediaProjection: MediaProjection? = null
    private var audioRecord: AudioRecord? = null
    private var isCapturing = false
    private var sensitivity = 2.0f
    private var analysisMode = 0 // 0: TimeDomain, 1: FrequencyDomain
    private var useSmoothing = true
    private var ampCutoff = 0.15f
    private var bassCutoffHz = 220f
    private var trebleCutoffHz = 3000f

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopSelf()
                return START_NOT_STICKY
            }
            ACTION_UPDATE_SETTINGS -> {
                sensitivity = intent.getFloatExtra(EXTRA_SENSITIVITY, sensitivity)
                analysisMode = intent.getIntExtra(EXTRA_MODE, analysisMode)
                useSmoothing = intent.getBooleanExtra(EXTRA_SMOOTHING, useSmoothing)
                ampCutoff = intent.getFloatExtra(EXTRA_AMP_CUTOFF, ampCutoff)
                bassCutoffHz = intent.getFloatExtra(EXTRA_BASS_CUTOFF, bassCutoffHz)
                trebleCutoffHz = intent.getFloatExtra(EXTRA_TREBLE_CUTOFF, trebleCutoffHz)
                
                NativeLib.nativeSetAnalysisMode(analysisMode)
                NativeLib.nativeSetSensitivity(sensitivity)
                NativeLib.nativeSetFrequencyBands(bassCutoffHz, trebleCutoffHz)
                NativeLib.setSmoothing(useSmoothing)
                NativeLib.setAmpCutoff(ampCutoff)
                return START_NOT_STICKY
            }
        }

        val resultCode = intent?.getIntExtra(EXTRA_RESULT_CODE, Activity.RESULT_CANCELED) ?: Activity.RESULT_CANCELED
        val data = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent?.getParcelableExtra(EXTRA_DATA, Intent::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent?.getParcelableExtra<Intent>(EXTRA_DATA)
        }

        if (resultCode != Activity.RESULT_OK || data == null) {
            stopSelf()
            return START_NOT_STICKY
        }

        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.notification_title))
            .setContentText(getString(R.string.notification_text))
            .setSmallIcon(android.R.drawable.ic_media_play)
            .build()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
            startCapture(resultCode, data)
        } else {
            stopSelf()
        }

        return START_NOT_STICKY
    }

    private fun startCapture(resultCode: Int, data: Intent) {
        if (isCapturing) return
        
        if (androidx.core.content.ContextCompat.checkSelfPermission(this, android.Manifest.permission.RECORD_AUDIO) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            return
        }

        val projectionManager = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        val projection = projectionManager.getMediaProjection(resultCode, data) ?: return
        mediaProjection = projection

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val config = AudioPlaybackCaptureConfiguration.Builder(projection)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                .addMatchingUsage(AudioAttributes.USAGE_GAME)
                .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                .build()

            val sampleRate = 44100
            val channelConfig = AudioFormat.CHANNEL_IN_STEREO
            val audioFormat = AudioFormat.ENCODING_PCM_16BIT
            val minBuf = AudioRecord.getMinBufferSize(sampleRate, channelConfig, audioFormat)
            val bufferSize = max(minBuf, 4096)

            val record = AudioRecord.Builder()
                .setAudioFormat(AudioFormat.Builder()
                    .setEncoding(audioFormat)
                    .setSampleRate(sampleRate)
                    .setChannelMask(channelConfig)
                    .build())
                .setBufferSizeInBytes(bufferSize)
                .setAudioPlaybackCaptureConfig(config)
                .build()

            if (record.state != AudioRecord.STATE_INITIALIZED) return

            record.startRecording()
            audioRecord = record
            isCapturing = true

            NativeLib.nativeSetAnalysisMode(analysisMode)
            NativeLib.nativeSetSensitivity(sensitivity)
            NativeLib.nativeSetFrequencyBands(bassCutoffHz, trebleCutoffHz)
            NativeLib.setSmoothing(useSmoothing)
            NativeLib.setAmpCutoff(ampCutoff)

            thread {
                val shortBuf = ShortArray(2048)
                val broadcastManager = LocalBroadcastManager.getInstance(this)
                try {
                    while (isCapturing) {
                        val read = record.read(shortBuf, 0, shortBuf.size)
                        if (read > 0) {
                            val features = NativeLib.nativeProcessAudio(shortBuf, read, sampleRate)
                            if (features.size >= 6) {
                                NativeLib.step(this, features, sensitivity)
                                
                                // Send stats back to Activity
                                val statsIntent = Intent(ACTION_STATS_UPDATE).apply {
                                    putExtra(EXTRA_POWER_IN, features[0].toFloat()) // volume
                                    putExtra(EXTRA_POWER_OUT, features[1].toFloat()) // bass/processed
                                }
                                broadcastManager.sendBroadcast(statsIntent)
                            }
                        } else if (read < 0) break
                    }
                } catch (e: Exception) {
                } finally {
                    stopSelf()
                }
            }
        }
    }

    override fun onDestroy() {
        isCapturing = false
        audioRecord?.apply {
            try { stop(); release() } catch (e: Exception) {}
        }
        mediaProjection?.stop()
        super.onDestroy()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val serviceChannel = NotificationChannel(
                CHANNEL_ID,
                "Audio Capture Service Channel",
                NotificationManager.IMPORTANCE_LOW
            )
            val manager = getSystemService(NotificationManager::class.java)
            manager?.createNotificationChannel(serviceChannel)
        }
    }
}
