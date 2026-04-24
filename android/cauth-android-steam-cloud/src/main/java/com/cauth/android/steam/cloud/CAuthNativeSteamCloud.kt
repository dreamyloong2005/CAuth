package com.cauth.android.steam.cloud

import com.cauth.android.CAuthRouteProbeSnapshot

internal object CAuthNativeSteamCloud {
    init {
        System.loadLibrary("cauth_steam_cloud_ffi")
        System.loadLibrary("cauth_android_steam_cloud_jni")
    }

    @JvmStatic
    external fun nativeListRemoteFiles(
        handle: Long,
        appId: Int,
        steamId: Long,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
        backend: Int,
        routeEndpoint: String?,
        routeProtocol: String?,
        routeRole: String?,
        localWriteMode: Int,
        atomicWrite: Boolean,
        count: Int,
        startIndex: Int,
        extendedDetails: Boolean,
    ): SteamCloudFileListSnapshot

    @JvmStatic
    external fun nativeListRemoteFilesViaWebPage(
        handle: Long,
        appId: Int,
        steamId: Long,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
        backend: Int,
        routeEndpoint: String?,
        routeProtocol: String?,
        routeRole: String?,
        localWriteMode: Int,
        atomicWrite: Boolean,
        count: Int,
        startIndex: Int,
    ): SteamCloudFileListSnapshot

    @JvmStatic
    external fun nativeProbeRoutes(
        handle: Long,
        appId: Int,
        steamId: Long,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
        backend: Int,
        routeEndpoint: String?,
        routeProtocol: String?,
        routeRole: String?,
        localWriteMode: Int,
        atomicWrite: Boolean,
        task: Int,
        maxCount: Int,
    ): CAuthRouteProbeSnapshot

    @JvmStatic
    external fun nativePull(
        handle: Long,
        appId: Int,
        steamId: Long,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
        backend: Int,
        routeEndpoint: String?,
        routeProtocol: String?,
        routeRole: String?,
        localWriteMode: Int,
        atomicWrite: Boolean,
    ): SteamCloudResultSnapshot

    @JvmStatic
    external fun nativePush(
        handle: Long,
        appId: Int,
        steamId: Long,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
        backend: Int,
        routeEndpoint: String?,
        routeProtocol: String?,
        routeRole: String?,
        localWriteMode: Int,
        atomicWrite: Boolean,
    ): SteamCloudResultSnapshot

    @JvmStatic
    external fun nativeVerifyLocalFiles(
        handle: Long,
        appId: Int,
        steamId: Long,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
        backend: Int,
        routeEndpoint: String?,
        routeProtocol: String?,
        routeRole: String?,
        localWriteMode: Int,
        atomicWrite: Boolean,
        includeExtraLocal: Boolean,
    ): SteamCloudVerifySnapshot

    @JvmStatic
    external fun nativeStartPull(
        handle: Long,
        appId: Int,
        steamId: Long,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
        backend: Int,
        routeEndpoint: String?,
        routeProtocol: String?,
        routeRole: String?,
        localWriteMode: Int,
        atomicWrite: Boolean,
    ): Long

    @JvmStatic
    external fun nativeStartPush(
        handle: Long,
        appId: Int,
        steamId: Long,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
        backend: Int,
        routeEndpoint: String?,
        routeProtocol: String?,
        routeRole: String?,
        localWriteMode: Int,
        atomicWrite: Boolean,
    ): Long

    @JvmStatic
    external fun nativeStartVerifyLocalFiles(
        handle: Long,
        appId: Int,
        steamId: Long,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
        backend: Int,
        routeEndpoint: String?,
        routeProtocol: String?,
        routeRole: String?,
        localWriteMode: Int,
        atomicWrite: Boolean,
        includeExtraLocal: Boolean,
    ): Long

    @JvmStatic
    external fun nativePollTransferTask(taskHandle: Long): SteamCloudTransferTaskSnapshot

    @JvmStatic
    external fun nativePauseTransferTask(taskHandle: Long)

    @JvmStatic
    external fun nativeCancelTransferTask(taskHandle: Long)

    @JvmStatic
    external fun nativeDisposeTransferTask(taskHandle: Long)
}
