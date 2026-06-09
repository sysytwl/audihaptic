package com.example.audihaptic

import android.app.Application
import android.util.Log

class MyApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        Log.i("AudioHaptic", "MyApplication.onCreate() - THE APP HAS STARTED!")
    }
}
