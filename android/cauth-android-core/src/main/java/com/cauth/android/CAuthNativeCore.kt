package com.cauth.android

internal object CAuthNativeCore {
    init {
        System.loadLibrary("cauth_core_ffi")
        System.loadLibrary("cauth_android_core_jni")
    }

    @JvmStatic
    external fun nativeGetVersionString(): String

    @JvmStatic
    external fun nativeCreateClient(): Long

    @JvmStatic
    external fun nativeDestroyClient(handle: Long)
}
