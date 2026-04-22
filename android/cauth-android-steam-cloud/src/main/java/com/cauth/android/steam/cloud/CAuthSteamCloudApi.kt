package com.cauth.android.steam.cloud

import com.cauth.android.CAuthClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class CAuthSteamCloudApi(
    private val client: CAuthClient,
) {
    suspend fun listRemoteFiles(
        request: SteamCloudRequest,
        count: Int = 100,
        startIndex: Int = 0,
        extendedDetails: Boolean = true,
    ): SteamCloudFileListSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamCloud.nativeListRemoteFiles(
            handle = client.requireNativeHandle(),
            appId = request.appId,
            steamId = request.steamId,
            accessToken = request.accessToken,
            localRoot = request.localRoot,
            remoteRoot = request.remoteRoot,
            dryRun = request.dryRun,
            deleteRemoteOrphans = request.deleteRemoteOrphans,
            conflictPolicy = request.conflictPolicy.nativeValue,
            localWriteMode = request.localWriteOptions.mode.nativeValue,
            atomicWrite = request.localWriteOptions.atomicWrite,
            count = count,
            startIndex = startIndex,
            extendedDetails = extendedDetails,
        )
    }

    suspend fun pull(request: SteamCloudRequest): SteamCloudResultSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamCloud.nativePull(
            handle = client.requireNativeHandle(),
            appId = request.appId,
            steamId = request.steamId,
            accessToken = request.accessToken,
            localRoot = request.localRoot,
            remoteRoot = request.remoteRoot,
            dryRun = request.dryRun,
            deleteRemoteOrphans = request.deleteRemoteOrphans,
            conflictPolicy = request.conflictPolicy.nativeValue,
            localWriteMode = request.localWriteOptions.mode.nativeValue,
            atomicWrite = request.localWriteOptions.atomicWrite,
        )
    }

    suspend fun push(request: SteamCloudRequest): SteamCloudResultSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamCloud.nativePush(
            handle = client.requireNativeHandle(),
            appId = request.appId,
            steamId = request.steamId,
            accessToken = request.accessToken,
            localRoot = request.localRoot,
            remoteRoot = request.remoteRoot,
            dryRun = request.dryRun,
            deleteRemoteOrphans = request.deleteRemoteOrphans,
            conflictPolicy = request.conflictPolicy.nativeValue,
            localWriteMode = request.localWriteOptions.mode.nativeValue,
            atomicWrite = request.localWriteOptions.atomicWrite,
        )
    }

    suspend fun verifyLocalFiles(
        request: SteamCloudRequest,
        includeExtraLocal: Boolean = false,
    ): SteamCloudVerifySnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamCloud.nativeVerifyLocalFiles(
            handle = client.requireNativeHandle(),
            appId = request.appId,
            steamId = request.steamId,
            accessToken = request.accessToken,
            localRoot = request.localRoot,
            remoteRoot = request.remoteRoot,
            dryRun = request.dryRun,
            deleteRemoteOrphans = request.deleteRemoteOrphans,
            conflictPolicy = request.conflictPolicy.nativeValue,
            localWriteMode = request.localWriteOptions.mode.nativeValue,
            atomicWrite = request.localWriteOptions.atomicWrite,
            includeExtraLocal = includeExtraLocal,
        )
    }

    suspend fun startPull(request: SteamCloudRequest): Long = withContext(Dispatchers.IO) {
        CAuthNativeSteamCloud.nativeStartPull(
            handle = client.requireNativeHandle(),
            appId = request.appId,
            steamId = request.steamId,
            accessToken = request.accessToken,
            localRoot = request.localRoot,
            remoteRoot = request.remoteRoot,
            dryRun = request.dryRun,
            deleteRemoteOrphans = request.deleteRemoteOrphans,
            conflictPolicy = request.conflictPolicy.nativeValue,
            localWriteMode = request.localWriteOptions.mode.nativeValue,
            atomicWrite = request.localWriteOptions.atomicWrite,
        )
    }

    suspend fun startPush(request: SteamCloudRequest): Long = withContext(Dispatchers.IO) {
        CAuthNativeSteamCloud.nativeStartPush(
            handle = client.requireNativeHandle(),
            appId = request.appId,
            steamId = request.steamId,
            accessToken = request.accessToken,
            localRoot = request.localRoot,
            remoteRoot = request.remoteRoot,
            dryRun = request.dryRun,
            deleteRemoteOrphans = request.deleteRemoteOrphans,
            conflictPolicy = request.conflictPolicy.nativeValue,
            localWriteMode = request.localWriteOptions.mode.nativeValue,
            atomicWrite = request.localWriteOptions.atomicWrite,
        )
    }

    suspend fun startVerifyLocalFiles(
        request: SteamCloudRequest,
        includeExtraLocal: Boolean = false,
    ): Long = withContext(Dispatchers.IO) {
        CAuthNativeSteamCloud.nativeStartVerifyLocalFiles(
            handle = client.requireNativeHandle(),
            appId = request.appId,
            steamId = request.steamId,
            accessToken = request.accessToken,
            localRoot = request.localRoot,
            remoteRoot = request.remoteRoot,
            dryRun = request.dryRun,
            deleteRemoteOrphans = request.deleteRemoteOrphans,
            conflictPolicy = request.conflictPolicy.nativeValue,
            localWriteMode = request.localWriteOptions.mode.nativeValue,
            atomicWrite = request.localWriteOptions.atomicWrite,
            includeExtraLocal = includeExtraLocal,
        )
    }

    suspend fun pollTransferTask(taskHandle: Long): SteamCloudTransferTaskSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamCloud.nativePollTransferTask(taskHandle)
    }

    suspend fun cancelTransferTask(taskHandle: Long) = withContext(Dispatchers.IO) {
        CAuthNativeSteamCloud.nativeCancelTransferTask(taskHandle)
    }

    suspend fun disposeTransferTask(taskHandle: Long) = withContext(Dispatchers.IO) {
        CAuthNativeSteamCloud.nativeDisposeTransferTask(taskHandle)
    }
}

fun CAuthClient.steamCloud(): CAuthSteamCloudApi = CAuthSteamCloudApi(this)
