package com.cauth.android.steam.depot

import com.cauth.android.CAuthClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class CAuthSteamDepotApi(
    private val client: CAuthClient,
) {
    suspend fun fetchBranches(
        appId: Int,
        maxCount: Int = 20,
    ): AppBranchListSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeFetchDepotBranches(
            handle = client.requireNativeHandle(),
            appId = appId,
            maxCount = maxCount,
        )
    }

    suspend fun fetchManifests(
        appId: Int,
        branch: String = "public",
        maxCount: Int = 20,
    ): DepotManifestListSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeFetchDepotManifests(
            handle = client.requireNativeHandle(),
            appId = appId,
            branch = branch,
            maxCount = maxCount,
        )
    }

    suspend fun fetchPreflight(
        appId: Int,
        branch: String = "public",
        maxCount: Int = 20,
    ): DepotPreflightSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeFetchDepotPreflight(
            handle = client.requireNativeHandle(),
            appId = appId,
            branch = branch,
            maxCount = maxCount,
        )
    }

    suspend fun fetchDepotKey(
        appId: Int,
        depotId: Int,
        maxCount: Int = 20,
    ): DepotKeySnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeFetchDepotKey(
            handle = client.requireNativeHandle(),
            appId = appId,
            depotId = depotId,
            maxCount = maxCount,
        )
    }

    suspend fun fetchManifestRequestCode(
        appId: Int,
        depotId: Int,
        manifestGid: Long,
        branch: String = "public",
        branchPasswordHash: String? = null,
        maxCount: Int = 20,
    ): ManifestRequestCodeSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeFetchManifestRequestCode(
            handle = client.requireNativeHandle(),
            appId = appId,
            depotId = depotId,
            manifestGid = manifestGid,
            branch = branch,
            branchPasswordHash = branchPasswordHash,
            maxCount = maxCount,
        )
    }

    suspend fun downloadManifest(
        depotId: Int,
        manifestGid: Long,
        requestCode: Long,
        outputPath: String,
        maxCount: Int = 20,
    ) = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeDownloadDepotManifest(
            depotId = depotId,
            manifestGid = manifestGid,
            requestCode = requestCode,
            maxCount = maxCount,
            outputPath = outputPath,
        )
    }

    suspend fun loadManifestInfo(
        inputPath: String,
        depotKeyHex: String? = null,
    ): ManifestInfoSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeLoadManifestInfo(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
        )
    }

    suspend fun listManifestFiles(
        inputPath: String,
        depotKeyHex: String? = null,
        filterText: String? = null,
        limit: Int = 50,
    ): ManifestFileListSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeListManifestFiles(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
            filterText = filterText,
            limit = limit,
        )
    }

    suspend fun verifyLocalFiles(
        inputPath: String,
        localRoot: String,
        depotKeyHex: String? = null,
        filterText: String? = null,
    ): DepotLocalVerifySnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeVerifyLocalFiles(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
            localRoot = localRoot,
            filterText = filterText,
        )
    }

    suspend fun downloadChunk(
        inputPath: String,
        outputPath: String,
        filePath: String? = null,
        fileIndex: Long? = null,
        chunkIndex: Long,
        depotKeyHex: String? = null,
        processChunk: Boolean = true,
        maxCount: Int = 20,
    ) = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeDownloadDepotChunk(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
            filePath = filePath,
            fileIndex = fileIndex ?: 0L,
            hasFileIndex = fileIndex != null,
            chunkIndex = chunkIndex,
            processChunk = processChunk,
            maxCount = maxCount,
            outputPath = outputPath,
        )
    }

    suspend fun downloadFile(
        inputPath: String,
        outputPath: String,
        depotKeyHex: String,
        filePath: String? = null,
        fileIndex: Long? = null,
        maxCount: Int = 20,
    ) = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeDownloadDepotFile(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
            filePath = filePath,
            fileIndex = fileIndex ?: 0L,
            hasFileIndex = fileIndex != null,
            maxCount = maxCount,
            outputPath = outputPath,
        )
    }

    suspend fun startManifestDownload(
        depotId: Int,
        manifestGid: Long,
        requestCode: Long,
        outputPath: String,
        maxCount: Int = 20,
    ): Long = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeStartDepotManifestDownload(
            depotId = depotId,
            manifestGid = manifestGid,
            requestCode = requestCode,
            maxCount = maxCount,
            outputPath = outputPath,
        )
    }

    suspend fun startChunkDownload(
        inputPath: String,
        outputPath: String,
        filePath: String? = null,
        fileIndex: Long? = null,
        chunkIndex: Long,
        depotKeyHex: String? = null,
        processChunk: Boolean = true,
        maxCount: Int = 20,
    ): Long = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeStartDepotChunkDownload(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
            filePath = filePath,
            fileIndex = fileIndex ?: 0L,
            hasFileIndex = fileIndex != null,
            chunkIndex = chunkIndex,
            processChunk = processChunk,
            maxCount = maxCount,
            outputPath = outputPath,
        )
    }

    suspend fun startFileDownload(
        inputPath: String,
        outputPath: String,
        depotKeyHex: String,
        filePath: String? = null,
        fileIndex: Long? = null,
        maxCount: Int = 20,
    ): Long = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeStartDepotFileDownload(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
            filePath = filePath,
            fileIndex = fileIndex ?: 0L,
            hasFileIndex = fileIndex != null,
            maxCount = maxCount,
            outputPath = outputPath,
        )
    }

    suspend fun downloadAllFiles(
        inputPath: String,
        outputRoot: String,
        depotKeyHex: String,
        maxCount: Int = 20,
    ) = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeDownloadDepotAllFiles(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
            maxCount = maxCount,
            outputRoot = outputRoot,
        )
    }

    suspend fun startAllFilesDownload(
        inputPath: String,
        outputRoot: String,
        depotKeyHex: String,
        maxCount: Int = 20,
    ): Long = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeStartDepotAllFilesDownload(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
            maxCount = maxCount,
            outputRoot = outputRoot,
        )
    }

    suspend fun pollDownloadTask(handle: Long): DepotDownloadTaskSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativePollDepotDownloadTask(handle)
    }

    suspend fun cancelDownloadTask(handle: Long) = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeCancelDepotDownloadTask(handle)
    }

    suspend fun disposeDownloadTask(handle: Long) = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeDisposeDepotDownloadTask(handle)
    }
}

fun CAuthClient.steamDepot(): CAuthSteamDepotApi = CAuthSteamDepotApi(this)
