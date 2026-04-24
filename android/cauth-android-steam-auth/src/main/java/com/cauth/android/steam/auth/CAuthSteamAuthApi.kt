package com.cauth.android.steam.auth

import com.cauth.android.CAuthRouteProbeSnapshot
import com.cauth.android.CAuthRouteSelection
import com.cauth.android.CAuthClient
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class CAuthSteamAuthApi(
    private val client: CAuthClient,
) {
    fun createController(scope: CoroutineScope): CAuthSteamAuthController {
        return CAuthSteamAuthController(client, scope)
    }

    suspend fun loginPassword(request: LoginRequest): LoginResultSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamAuth.nativeLoginPassword(
            handle = client.requireNativeHandle(),
            accountName = request.accountName,
            password = request.password,
            steamGuardCode = request.steamGuardCode,
            deviceName = request.deviceName,
            rememberSession = request.rememberSession,
            platformType = request.platform.nativeValue,
            routeEndpoint = request.routeSelection?.endpoint,
            routeProtocol = request.routeSelection?.protocol,
            routeRole = request.routeSelection?.role,
        )
    }

    suspend fun getSavedSession(steamId: Long): SavedSessionSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamAuth.nativeGetSavedSession(client.requireNativeHandle(), steamId)
    }

    suspend fun listSavedAccounts(): List<SavedAccountSnapshot> = withContext(Dispatchers.IO) {
        CAuthNativeSteamAuth.nativeListSavedAccounts(client.requireNativeHandle()).toList()
    }

    suspend fun clearSavedSession(steamId: Long) = withContext(Dispatchers.IO) {
        CAuthNativeSteamAuth.nativeClearSavedSession(client.requireNativeHandle(), steamId)
    }

    suspend fun clearSavedAccount(steamId: Long) = withContext(Dispatchers.IO) {
        CAuthNativeSteamAuth.nativeClearSavedAccount(client.requireNativeHandle(), steamId)
    }

    suspend fun clearAllSavedAccounts() = withContext(Dispatchers.IO) {
        CAuthNativeSteamAuth.nativeClearAllSavedAccounts(client.requireNativeHandle())
    }

    suspend fun probeCm(
        routeSelection: CAuthRouteSelection? = null,
    ): CmProbeSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamAuth.nativeCmProbe(
            routeEndpoint = routeSelection?.endpoint,
            routeProtocol = routeSelection?.protocol,
            routeRole = routeSelection?.role,
        )
    }

    suspend fun logonCm(
        steamId: Long,
        routeSelection: CAuthRouteSelection? = null,
    ): CmLogonSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamAuth.nativeCmLogon(
            handle = client.requireNativeHandle(),
            steamId = steamId,
            routeEndpoint = routeSelection?.endpoint,
            routeProtocol = routeSelection?.protocol,
            routeRole = routeSelection?.role,
        )
    }

    suspend fun probeCmRoutes(maxCount: Int = 20): CAuthRouteProbeSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamAuth.nativeProbeCmRoutes(maxCount = maxCount)
    }
}

fun CAuthClient.steamAuth(): CAuthSteamAuthApi = CAuthSteamAuthApi(this)
