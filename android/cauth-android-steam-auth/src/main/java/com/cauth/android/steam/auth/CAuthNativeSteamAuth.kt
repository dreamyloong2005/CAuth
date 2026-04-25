package com.cauth.android.steam.auth

import com.cauth.android.CAuthRouteProbeSnapshot

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
        routeEndpoint: String?,
        routeProtocol: String?,
        routeRole: String?,
    ): LoginResultSnapshot

    @JvmStatic
    external fun nativeRequestLoginCancel(handle: Long)

    @JvmStatic
    external fun nativeGetSavedSession(handle: Long, steamId: Long): SavedSessionSnapshot

    @JvmStatic
    external fun nativeListSavedAccounts(handle: Long): Array<SavedAccountSnapshot>

    @JvmStatic
    external fun nativeClearSavedSession(handle: Long, steamId: Long)

    @JvmStatic
    external fun nativeClearSavedAccount(handle: Long, steamId: Long)

    @JvmStatic
    external fun nativeClearAllSavedAccounts(handle: Long)

    @JvmStatic
    external fun nativeCmProbe(
        routeEndpoint: String?,
        routeProtocol: String?,
        routeRole: String?,
    ): CmProbeSnapshot

    @JvmStatic
    external fun nativeCmLogon(
        handle: Long,
        steamId: Long,
        routeEndpoint: String?,
        routeProtocol: String?,
        routeRole: String?,
    ): CmLogonSnapshot

    @JvmStatic
    external fun nativeProbeCmRoutes(maxCount: Int): CAuthRouteProbeSnapshot
}
