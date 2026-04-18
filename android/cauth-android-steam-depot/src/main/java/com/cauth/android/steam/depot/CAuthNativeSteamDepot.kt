package com.cauth.android.steam.depot

internal object CAuthNativeSteamDepot {
    init {
        System.loadLibrary("cauth_steam_depot_ffi")
        System.loadLibrary("cauth_android_steam_depot_jni")
    }

    @JvmStatic
    external fun nativeFetchDepotBranches(
        handle: Long,
        appId: Int,
        maxCount: Int,
    ): AppBranchListSnapshot

    @JvmStatic
    external fun nativeFetchDepotManifests(
        handle: Long,
        appId: Int,
        branch: String?,
        maxCount: Int,
    ): DepotManifestListSnapshot

    @JvmStatic
    external fun nativeFetchDepotPreflight(
        handle: Long,
        appId: Int,
        branch: String?,
        maxCount: Int,
    ): DepotPreflightSnapshot

    @JvmStatic
    external fun nativeFetchDepotKey(
        handle: Long,
        appId: Int,
        depotId: Int,
        maxCount: Int,
    ): DepotKeySnapshot

    @JvmStatic
    external fun nativeFetchManifestRequestCode(
        handle: Long,
        appId: Int,
        depotId: Int,
        manifestGid: Long,
        branch: String?,
        branchPasswordHash: String?,
        maxCount: Int,
    ): ManifestRequestCodeSnapshot

    @JvmStatic
    external fun nativeDownloadDepotManifest(
        depotId: Int,
        manifestGid: Long,
        requestCode: Long,
        maxCount: Int,
        outputPath: String,
    )

    @JvmStatic
    external fun nativeLoadManifestInfo(
        inputPath: String,
        depotKeyHex: String?,
    ): ManifestInfoSnapshot

    @JvmStatic
    external fun nativeListManifestFiles(
        inputPath: String,
        depotKeyHex: String?,
        filterText: String?,
        limit: Int,
    ): ManifestFileListSnapshot

    @JvmStatic
    external fun nativeVerifyLocalFiles(
        inputPath: String,
        depotKeyHex: String?,
        localRoot: String,
        filterText: String?,
    ): DepotLocalVerifySnapshot

    @JvmStatic
    external fun nativeDownloadDepotChunk(
        inputPath: String,
        depotKeyHex: String?,
        filePath: String?,
        fileIndex: Long,
        hasFileIndex: Boolean,
        chunkIndex: Long,
        processChunk: Boolean,
        maxCount: Int,
        outputPath: String,
    )

    @JvmStatic
    external fun nativeDownloadDepotFile(
        inputPath: String,
        depotKeyHex: String,
        filePath: String?,
        fileIndex: Long,
        hasFileIndex: Boolean,
        maxCount: Int,
        outputPath: String,
    )

    @JvmStatic
    external fun nativeDownloadDepotAllFiles(
        inputPath: String,
        depotKeyHex: String,
        maxCount: Int,
        outputRoot: String,
    )

    @JvmStatic
    external fun nativeStartDepotManifestDownload(
        depotId: Int,
        manifestGid: Long,
        requestCode: Long,
        maxCount: Int,
        outputPath: String,
    ): Long

    @JvmStatic
    external fun nativeStartDepotChunkDownload(
        inputPath: String,
        depotKeyHex: String?,
        filePath: String?,
        fileIndex: Long,
        hasFileIndex: Boolean,
        chunkIndex: Long,
        processChunk: Boolean,
        maxCount: Int,
        outputPath: String,
    ): Long

    @JvmStatic
    external fun nativeStartDepotFileDownload(
        inputPath: String,
        depotKeyHex: String,
        filePath: String?,
        fileIndex: Long,
        hasFileIndex: Boolean,
        maxCount: Int,
        outputPath: String,
    ): Long

    @JvmStatic
    external fun nativeStartDepotAllFilesDownload(
        inputPath: String,
        depotKeyHex: String,
        maxCount: Int,
        outputRoot: String,
    ): Long

    @JvmStatic
    external fun nativePollDepotDownloadTask(handle: Long): DepotDownloadTaskSnapshot

    @JvmStatic
    external fun nativeCancelDepotDownloadTask(handle: Long)

    @JvmStatic
    external fun nativeDisposeDepotDownloadTask(handle: Long)
}
