package com.cauth.android.steam.cloud

internal object CAuthNativeSteamCloud {
    init {
        System.loadLibrary("cauth_steam_cloud_ffi")
        System.loadLibrary("cauth_android_steam_cloud_jni")
    }

    @JvmStatic
    external fun nativeListRemoteFiles(
        handle: Long,
        appId: Int,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
        count: Int,
        startIndex: Int,
        extendedDetails: Boolean,
    ): SteamCloudFileListSnapshot

    @JvmStatic
    external fun nativePull(
        handle: Long,
        appId: Int,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
    ): SteamCloudResultSnapshot

    @JvmStatic
    external fun nativePush(
        handle: Long,
        appId: Int,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
    ): SteamCloudResultSnapshot

    @JvmStatic
    external fun nativeVerifyLocalFiles(
        handle: Long,
        appId: Int,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
        includeExtraLocal: Boolean,
    ): SteamCloudVerifySnapshot

    @JvmStatic
    external fun nativeStartPull(
        handle: Long,
        appId: Int,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
    ): Long

    @JvmStatic
    external fun nativeStartPush(
        handle: Long,
        appId: Int,
        accessToken: String?,
        localRoot: String?,
        remoteRoot: String?,
        dryRun: Boolean,
        deleteRemoteOrphans: Boolean,
        conflictPolicy: Int,
    ): Long

    @JvmStatic
    external fun nativePollTransferTask(taskHandle: Long): SteamCloudTransferTaskSnapshot

    @JvmStatic
    external fun nativeCancelTransferTask(taskHandle: Long)

    @JvmStatic
    external fun nativeDisposeTransferTask(taskHandle: Long)
}
