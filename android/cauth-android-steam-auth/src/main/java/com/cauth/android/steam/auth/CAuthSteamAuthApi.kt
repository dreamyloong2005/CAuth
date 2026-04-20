package com.cauth.android.steam.auth

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

    suspend fun probeCm(): CmProbeSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamAuth.nativeCmProbe()
    }

    suspend fun logonCm(steamId: Long): CmLogonSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamAuth.nativeCmLogon(client.requireNativeHandle(), steamId)
    }
}

fun CAuthClient.steamAuth(): CAuthSteamAuthApi = CAuthSteamAuthApi(this)
