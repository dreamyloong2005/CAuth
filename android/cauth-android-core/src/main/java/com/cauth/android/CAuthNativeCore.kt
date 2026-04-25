package com.cauth.android

internal object CAuthNativeCore {
    init {
        System.loadLibrary("cauth_core_ffi")
        System.loadLibrary("cauth_android_core_jni")
    }

    @JvmStatic
    external fun nativeGetVersionString(): String

    @JvmStatic
    external fun nativeCreateClientWithOptions(
        sessionStorageKind: Int,
        sessionStoragePath: String?,
        sessionStorageNamespace: String?,
        sessionStorageKey: String?,
    ): Long

    @JvmStatic
    external fun nativeDestroyClient(handle: Long)

    @JvmStatic
    external fun nativeIsOperationCanceled(): Boolean
}
