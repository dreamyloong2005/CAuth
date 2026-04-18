package com.cauth.example

import android.app.Application
import com.cauth.android.CAuthAndroidRuntime

class ExampleApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        CAuthAndroidRuntime.attach(this)
    }
}
