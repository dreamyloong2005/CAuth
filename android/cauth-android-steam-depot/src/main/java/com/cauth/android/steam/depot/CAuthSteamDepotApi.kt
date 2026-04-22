package com.cauth.android.steam.depot

import com.cauth.android.CAuthFileWriteOptions
import com.cauth.android.CAuthClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class CAuthSteamDepotApi(
    private val client: CAuthClient,
) {
    suspend fun fetchBranches(
        steamId: Long,
        appId: Int,
        maxCount: Int = 20,
    ): AppBranchListSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeFetchDepotBranches(
            handle = client.requireNativeHandle(),
            steamId = steamId,
            appId = appId,
            maxCount = maxCount,
        )
    }

    suspend fun fetchManifests(
        steamId: Long,
        appId: Int,
        branch: String = "public",
        maxCount: Int = 20,
    ): DepotManifestListSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeFetchDepotManifests(
            handle = client.requireNativeHandle(),
            steamId = steamId,
            appId = appId,
            branch = branch,
            maxCount = maxCount,
        )
    }

    suspend fun fetchPreflight(
        steamId: Long,
        appId: Int,
        branch: String = "public",
        maxCount: Int = 20,
    ): DepotPreflightSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeFetchDepotPreflight(
            handle = client.requireNativeHandle(),
            steamId = steamId,
            appId = appId,
            branch = branch,
            maxCount = maxCount,
        )
    }

    suspend fun fetchDepotKey(
        steamId: Long,
        appId: Int,
        depotId: Int,
        maxCount: Int = 20,
    ): DepotKeySnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeFetchDepotKey(
            handle = client.requireNativeHandle(),
            steamId = steamId,
            appId = appId,
            depotId = depotId,
            maxCount = maxCount,
        )
    }

    suspend fun fetchManifestRequestCode(
        steamId: Long,
        appId: Int,
        depotId: Int,
        manifestGid: Long,
        branch: String = "public",
        branchPasswordHash: String? = null,
        maxCount: Int = 20,
    ): ManifestRequestCodeSnapshot = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeFetchManifestRequestCode(
            handle = client.requireNativeHandle(),
            steamId = steamId,
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
        writeOptions: CAuthFileWriteOptions = CAuthFileWriteOptions(),
    ) = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeDownloadDepotManifest(
            depotId = depotId,
            manifestGid = manifestGid,
            requestCode = requestCode,
            maxCount = maxCount,
            outputPath = outputPath,
            writeMode = writeOptions.mode.nativeValue,
            atomicWrite = writeOptions.atomicWrite,
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

    suspend fun startVerifyLocalFiles(
        inputPath: String,
        localRoot: String,
        depotKeyHex: String? = null,
        filterText: String? = null,
    ): Long = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeStartDepotVerifyLocal(
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
        writeOptions: CAuthFileWriteOptions = CAuthFileWriteOptions(),
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
            writeMode = writeOptions.mode.nativeValue,
            atomicWrite = writeOptions.atomicWrite,
        )
    }

    suspend fun downloadFile(
        inputPath: String,
        outputPath: String,
        depotKeyHex: String,
        filePath: String? = null,
        fileIndex: Long? = null,
        maxCount: Int = 20,
        writeOptions: CAuthFileWriteOptions = CAuthFileWriteOptions(),
    ) = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeDownloadDepotFile(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
            filePath = filePath,
            fileIndex = fileIndex ?: 0L,
            hasFileIndex = fileIndex != null,
            maxCount = maxCount,
            outputPath = outputPath,
            writeMode = writeOptions.mode.nativeValue,
            atomicWrite = writeOptions.atomicWrite,
        )
    }

    suspend fun startManifestDownload(
        depotId: Int,
        manifestGid: Long,
        requestCode: Long,
        outputPath: String,
        maxCount: Int = 20,
        writeOptions: CAuthFileWriteOptions = CAuthFileWriteOptions(),
    ): Long = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeStartDepotManifestDownload(
            depotId = depotId,
            manifestGid = manifestGid,
            requestCode = requestCode,
            maxCount = maxCount,
            outputPath = outputPath,
            writeMode = writeOptions.mode.nativeValue,
            atomicWrite = writeOptions.atomicWrite,
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
        writeOptions: CAuthFileWriteOptions = CAuthFileWriteOptions(),
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
            writeMode = writeOptions.mode.nativeValue,
            atomicWrite = writeOptions.atomicWrite,
        )
    }

    suspend fun startFileDownload(
        inputPath: String,
        outputPath: String,
        depotKeyHex: String,
        filePath: String? = null,
        fileIndex: Long? = null,
        maxCount: Int = 20,
        writeOptions: CAuthFileWriteOptions = CAuthFileWriteOptions(),
    ): Long = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeStartDepotFileDownload(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
            filePath = filePath,
            fileIndex = fileIndex ?: 0L,
            hasFileIndex = fileIndex != null,
            maxCount = maxCount,
            outputPath = outputPath,
            writeMode = writeOptions.mode.nativeValue,
            atomicWrite = writeOptions.atomicWrite,
        )
    }

    suspend fun downloadAllFiles(
        inputPath: String,
        outputRoot: String,
        depotKeyHex: String,
        maxCount: Int = 20,
        writeOptions: CAuthFileWriteOptions = CAuthFileWriteOptions(),
    ) = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeDownloadDepotAllFiles(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
            maxCount = maxCount,
            outputRoot = outputRoot,
            writeMode = writeOptions.mode.nativeValue,
            atomicWrite = writeOptions.atomicWrite,
        )
    }

    suspend fun startAllFilesDownload(
        inputPath: String,
        outputRoot: String,
        depotKeyHex: String,
        maxCount: Int = 20,
        writeOptions: CAuthFileWriteOptions = CAuthFileWriteOptions(),
    ): Long = withContext(Dispatchers.IO) {
        CAuthNativeSteamDepot.nativeStartDepotAllFilesDownload(
            inputPath = inputPath,
            depotKeyHex = depotKeyHex,
            maxCount = maxCount,
            outputRoot = outputRoot,
            writeMode = writeOptions.mode.nativeValue,
            atomicWrite = writeOptions.atomicWrite,
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
