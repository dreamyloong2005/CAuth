package com.cauth.android.steam.auth

internal object CAuthNativeSteamAuth {
    init {
        System.loadLibrary("cauth_steam_auth_ffi")
        System.loadLibrary("cauth_android_steam_auth_jni")
    }

    @JvmStatic
    external fun nativeLoginPassword(
        handle: Long,
        accountName: String,
        password: String,
        steamGuardCode: String?,
        deviceName: String?,
        rememberSession: Boolean,
        platformType: Int,
    ): LoginResultSnapshot

    @JvmStatic
    external fun nativeGetSavedSession(handle: Long): SavedSessionSnapshot

    @JvmStatic
    external fun nativeClearSavedSession(handle: Long)

    @JvmStatic
    external fun nativeCmProbe(): CmProbeSnapshot

    @JvmStatic
    external fun nativeCmLogon(handle: Long): CmLogonSnapshot
}
