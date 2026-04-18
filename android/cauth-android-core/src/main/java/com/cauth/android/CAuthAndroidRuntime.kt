package com.cauth.android

import android.content.Context

object CAuthAndroidRuntime {
    init {
        CAuthNativeCore
    }

    fun version(): String = CAuthNativeCore.nativeGetVersionString()

    fun attach(context: Context) {
        nativeOnAttachedToRuntime(context.applicationContext)
    }

    fun detach() {
        nativeOnDetachedFromRuntime()
    }

    @JvmStatic
    private external fun nativeOnAttachedToRuntime(context: Context)
    @JvmStatic
    private external fun nativeOnDetachedFromRuntime()
}
