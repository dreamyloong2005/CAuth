package com.cauth.android.steam.depot

import com.cauth.android.CAuthClient
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class CAuthSteamDepotState(
    val appIdText: String = "",
    val steamIdText: String = "",
    val branch: String = "public",
    val maxCountText: String = "20",
    val depotIdText: String = "",
    val manifestGidText: String = "",
    val requestCodeText: String = "",
    val branchPasswordHash: String = "",
    val outputPath: String = "",
    val manifestPath: String = "",
    val depotKeyHex: String = "",
    val filterText: String = "",
    val fileLimitText: String = "50",
    val selectedFilePath: String = "",
    val chunkIndexText: String = "0",
    val chunkOutputPath: String = "",
    val fileOutputPath: String = "",
    val allFilesOutputRoot: String = "",
    val verifyLocalRoot: String = "",
    val processChunk: Boolean = true,
    val statusText: String = "Ready",
    val busy: Boolean = false,
    val branches: AppBranchListSnapshot? = null,
    val manifests: DepotManifestListSnapshot? = null,
    val preflight: DepotPreflightSnapshot? = null,
    val depotKey: DepotKeySnapshot? = null,
    val manifestRequestCode: ManifestRequestCodeSnapshot? = null,
    val manifestInfo: ManifestInfoSnapshot? = null,
    val manifestFiles: ManifestFileListSnapshot? = null,
    val localVerify: DepotLocalVerifySnapshot? = null,
    val downloadTask: DepotDownloadTaskSnapshot? = null,
    val traceLines: List<String> = emptyList(),
)

class CAuthSteamDepotController(
    private val client: CAuthClient,
    private val scope: CoroutineScope,
) {
    private val api = CAuthSteamDepotApi(client)
    private val _state = MutableStateFlow(CAuthSteamDepotState())
    val state: StateFlow<CAuthSteamDepotState> = _state
    private var downloadPollingJob: Job? = null

    fun setAppIdText(value: String) = _state.update { it.copy(appIdText = sanitizeCompact(value)) }
    fun setSteamIdText(value: String) = _state.update { it.copy(steamIdText = sanitizeCompact(value)) }
    fun setBranch(value: String) = _state.update { it.copy(branch = sanitizeTrimmed(value)) }
    fun setMaxCountText(value: String) = _state.update { it.copy(maxCountText = sanitizeCompact(value)) }
    fun setDepotIdText(value: String) = _state.update { it.copy(depotIdText = sanitizeCompact(value)) }
    fun setManifestGidText(value: String) = _state.update { it.copy(manifestGidText = sanitizeCompact(value)) }
    fun setRequestCodeText(value: String) = _state.update { it.copy(requestCodeText = sanitizeCompact(value)) }
    fun setBranchPasswordHash(value: String) = _state.update { it.copy(branchPasswordHash = sanitizeCompact(value)) }
    fun setOutputPath(value: String) = _state.update { it.copy(outputPath = sanitizeTrimmed(value)) }
    fun setManifestPath(value: String) = _state.update { it.copy(manifestPath = sanitizeTrimmed(value)) }
    fun setDepotKeyHex(value: String) = _state.update { it.copy(depotKeyHex = sanitizeCompact(value)) }
    fun setFilterText(value: String) = _state.update { it.copy(filterText = sanitizeTrimmed(value)) }
    fun setFileLimitText(value: String) = _state.update { it.copy(fileLimitText = sanitizeCompact(value)) }
    fun setSelectedFilePath(value: String) = _state.update { it.copy(selectedFilePath = sanitizeTrimmed(value)) }
    fun setChunkIndexText(value: String) = _state.update { it.copy(chunkIndexText = sanitizeCompact(value)) }
    fun setChunkOutputPath(value: String) = _state.update { it.copy(chunkOutputPath = sanitizeTrimmed(value)) }
    fun setFileOutputPath(value: String) = _state.update { it.copy(fileOutputPath = sanitizeTrimmed(value)) }
    fun setAllFilesOutputRoot(value: String) = _state.update { it.copy(allFilesOutputRoot = sanitizeTrimmed(value)) }
    fun setVerifyLocalRoot(value: String) = _state.update { it.copy(verifyLocalRoot = sanitizeTrimmed(value)) }
    fun setProcessChunk(value: Boolean) = _state.update { it.copy(processChunk = value) }

    fun cancelActiveDownload() {
        val handle = _state.value.downloadTask?.handle ?: return
        scope.launch {
            runCatching { api.cancelDownloadTask(handle) }
            _state.update {
                it.copy(statusText = "Cancel requested...", busy = true)
            }
            appendTrace("Download cancel requested handle=$handle")
        }
    }

    fun useManifestSelection(depotId: Int, manifestGid: Long) {
        _state.update {
            it.copy(
                depotIdText = depotId.toString(),
                manifestGidText = manifestGid.toString(),
                requestCodeText = "",
                manifestRequestCode = null,
                statusText = "Selected depot=$depotId manifest=$manifestGid",
            )
        }
        appendTrace("Manifest selected depotId=$depotId manifestGid=$manifestGid")
    }

    fun prepareKeyAndCodeSelection(depotId: Int, manifestGid: Long) {
        _state.update {
            it.copy(
                depotIdText = depotId.toString(),
                manifestGidText = manifestGid.toString(),
                requestCodeText = "",
                depotKeyHex = "",
                depotKey = null,
                manifestRequestCode = null,
                manifestInfo = null,
                manifestFiles = null,
                localVerify = null,
                downloadTask = null,
                statusText = "Prepared depot=$depotId manifest=$manifestGid for key/code fetch",
            )
        }
        appendTrace("Prepared key/code selection depotId=$depotId manifestGid=$manifestGid")
    }

    fun useManifestFile(path: String) {
        val suggestedOutputPath = suggestFileOutputPath(
            manifestPath = _state.value.manifestPath,
            selectedFilePath = path,
        )
        _state.update {
            it.copy(
                selectedFilePath = path,
                fileOutputPath = suggestedOutputPath ?: it.fileOutputPath,
                statusText = "Selected file $path",
            )
        }
        appendTrace(
            "Manifest file selected path=$path" +
                (suggestedOutputPath?.let { " autoOutput=$it" } ?: ""),
        )
    }

    fun fetchBranches() {
        runAction("Fetch Branches") { steamId, appId, maxCount, _ ->
            val result = api.fetchBranches(steamId = steamId, appId = appId, maxCount = maxCount)
            _state.update {
                it.copy(
                    statusText = when {
                        !result.present -> "No branch list was returned for app $appId"
                        result.branches.isEmpty() -> "App $appId returned 0 branches"
                        else -> "Fetched ${result.branches.size} branch(es)"
                    },
                    busy = false,
                    branches = result,
                )
            }
            appendTrace("Branches returned count=${result.branches.size} present=${result.present}")
        }
    }

    fun fetchManifests() {
        runAction("Fetch Manifests") { steamId, appId, maxCount, branch ->
            val result = api.fetchManifests(
                steamId = steamId,
                appId = appId,
                branch = branch,
                maxCount = maxCount,
            )
            _state.update {
                it.copy(
                    statusText = when {
                        !result.present -> "No manifests were returned for branch $branch"
                        result.manifests.isEmpty() -> "Branch $branch returned 0 manifests"
                        else -> "Fetched ${result.manifests.size} manifest(s) for $branch"
                    },
                    busy = false,
                    manifests = result,
                )
            }
            appendTrace("Manifests returned count=${result.manifests.size} branch=$branch present=${result.present}")
        }
    }

    fun fetchPreflight() {
        _state.update { it.copy(preflight = null) }
        runAction("Fetch Preflight") { steamId, appId, maxCount, branch ->
            val result = api.fetchPreflight(
                steamId = steamId,
                appId = appId,
                branch = branch,
                maxCount = maxCount,
            )
            _state.update {
                it.copy(
                    statusText = when {
                        !result.present -> "No preflight result was returned for branch $branch"
                        result.depots.isEmpty() -> "Preflight returned 0 depots for branch $branch"
                        else -> "Fetched ${result.depots.size} depot preflight entr${if (result.depots.size == 1) "y" else "ies"}"
                    },
                    busy = false,
                    preflight = result,
                )
            }
            appendTrace("Preflight returned count=${result.depots.size} branch=$branch build=${result.buildId}")
        }
    }

    fun fetchDepotKey() {
        _state.update { it.copy(depotKey = null, depotKeyHex = "") }
        runAction("Fetch Depot Key") { steamId, appId, maxCount, _ ->
            val depotId = requirePositiveInt(_state.value.depotIdText, "Depot ID")
            val result = api.fetchDepotKey(
                steamId = steamId,
                appId = appId,
                depotId = depotId,
                maxCount = maxCount,
            )
            _state.update {
                it.copy(
                    statusText = if (result.present && result.keyHex.isNotBlank()) {
                        "Fetched depot key for $depotId"
                    } else {
                        "Depot key was not returned for $depotId"
                    },
                    busy = false,
                    depotKey = result,
                    depotKeyHex = if (result.present && result.keyHex.isNotBlank()) result.keyHex else "",
                )
            }
            appendTrace(
                "Depot key returned present=${result.present} depotId=$depotId eresult=${result.eresult} autoApplied=${result.present && result.keyHex.isNotBlank()}",
            )
        }
    }

    fun fetchManifestRequestCode() {
        _state.update { it.copy(manifestRequestCode = null, requestCodeText = "") }
        runAction("Fetch Manifest Code") { steamId, appId, maxCount, branch ->
            val depotId = requirePositiveInt(_state.value.depotIdText, "Depot ID")
            val manifestGid = requirePositiveLong(_state.value.manifestGidText, "Manifest GID")
            val result = api.fetchManifestRequestCode(
                steamId = steamId,
                appId = appId,
                depotId = depotId,
                manifestGid = manifestGid,
                branch = branch,
                branchPasswordHash = _state.value.branchPasswordHash.ifBlank { null },
                maxCount = maxCount,
            )
            _state.update {
                it.copy(
                    statusText = if (result.present && result.requestCode.toULong() > 0uL) {
                        "Fetched manifest request code"
                    } else {
                        "Manifest request code was not returned; check depot, manifest, branch, and ownership"
                    },
                    busy = false,
                    manifestRequestCode = result,
                    requestCodeText = if (result.present && result.requestCode.toULong() > 0uL) {
                        formatUnsignedDecimal(result.requestCode)
                    } else {
                        ""
                    },
                )
            }
            appendTrace(
                "Manifest request code returned present=${result.present} requestCode=${formatUnsignedDecimal(result.requestCode)}",
            )
        }
    }

    fun downloadManifest() {
        val outputPath = _state.value.outputPath.ifBlank {
            _state.update { it.copy(statusText = "Output path is required") }
            appendTrace("Download Manifest blocked: output path is blank")
            return
        }
        startDownloadAction(
            label = "Download Manifest",
            start = { _, maxCount, _ ->
                val depotId = requirePositiveInt(_state.value.depotIdText, "Depot ID")
                val manifestGid = requirePositiveLong(_state.value.manifestGidText, "Manifest GID")
                val requestCode = resolveRequestCode(_state.value)
                appendTrace(
                    "Manifest download requested depotId=$depotId manifestGid=$manifestGid requestCode=${formatUnsignedDecimal(requestCode)} out=$outputPath",
                )
                api.startManifestDownload(
                    depotId = depotId,
                    manifestGid = manifestGid,
                    requestCode = requestCode,
                    outputPath = outputPath,
                    maxCount = maxCount,
                )
            },
            onSuccess = { onManifestDownloadCompleted(outputPath) },
        )
    }

    private fun onManifestDownloadCompleted(outputPath: String) {
        _state.update {
            it.copy(
                statusText = "Manifest downloaded to $outputPath",
                busy = false,
                manifestPath = outputPath,
            )
        }
        appendTrace("Manifest downloaded out=$outputPath")
    }

    private fun onChunkDownloadCompleted(outputPath: String, selectedFilePath: String, chunkIndex: Long, processChunk: Boolean) {
        _state.update {
            it.copy(
                statusText = "Chunk downloaded to $outputPath",
                busy = false,
            )
        }
        appendTrace(
            "Chunk downloaded file=$selectedFilePath chunkIndex=$chunkIndex process=$processChunk out=$outputPath",
        )
    }

    private fun onFileDownloadCompleted(outputPath: String, selectedFilePath: String) {
        _state.update {
            it.copy(
                statusText = "File downloaded to $outputPath",
                busy = false,
            )
        }
        appendTrace("File downloaded file=$selectedFilePath out=$outputPath")
    }

    private fun onAllFilesDownloadCompleted(outputRoot: String) {
        _state.update {
            it.copy(
                statusText = "All manifest files downloaded to $outputRoot",
                busy = false,
            )
        }
        appendTrace("All manifest files downloaded outRoot=$outputRoot")
    }

    fun loadManifestInfo() {
        val snapshot = _state.value
        val manifestPath = snapshot.manifestPath.ifBlank {
            _state.update { it.copy(statusText = "Manifest path is required") }
            appendTrace("Manifest Info blocked: manifest path is blank")
            return
        }
        appendTrace("Manifest Info clicked path=$manifestPath")
        _state.update { it.copy(statusText = "Manifest info in progress...", busy = true, manifestInfo = null) }
        scope.launch {
            runCatching {
                api.loadManifestInfo(
                    inputPath = manifestPath,
                    depotKeyHex = snapshot.depotKeyHex.ifBlank { null },
                )
            }.onSuccess { result ->
                _state.update {
                    it.copy(
                        statusText = if (result.present) {
                            "Manifest info loaded for depot ${result.depotId}"
                        } else {
                            "Manifest info was not returned"
                        },
                        busy = false,
                        manifestInfo = result,
                    )
                }
                appendTrace("Manifest info returned present=${result.present} files=${result.fileCount} chunks=${result.chunkCount}")
            }.onFailure { failure ->
                _state.update {
                    it.copy(
                        statusText = failure.message ?: "Manifest info failed",
                        busy = false,
                    )
                }
                appendTrace("Manifest Info failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
            }
        }
    }

    fun listManifestFiles() {
        val snapshot = _state.value
        val manifestPath = snapshot.manifestPath.ifBlank {
            _state.update { it.copy(statusText = "Manifest path is required") }
            appendTrace("Manifest Files blocked: manifest path is blank")
            return
        }
        val limit = snapshot.fileLimitText.toIntOrNull()?.coerceIn(1, 500) ?: 50
        appendTrace("Manifest Files clicked path=$manifestPath limit=$limit filter=${snapshot.filterText.ifBlank { "(none)" }}")
        _state.update { it.copy(statusText = "Manifest file list in progress...", busy = true, manifestFiles = null) }
        scope.launch {
            runCatching {
                api.listManifestFiles(
                    inputPath = manifestPath,
                    depotKeyHex = snapshot.depotKeyHex.ifBlank { null },
                    filterText = snapshot.filterText.ifBlank { null },
                    limit = limit,
                )
            }.onSuccess { result ->
                _state.update {
                    it.copy(
                        statusText = when {
                            !result.present -> "Manifest file list was not returned"
                            result.matchedCount == 0L -> "Manifest file list returned 0 matches"
                            else -> "Listed ${result.printedCount} manifest file(s)"
                        },
                        busy = false,
                        manifestFiles = result,
                    )
                }
                appendTrace("Manifest files returned printed=${result.printedCount} matched=${result.matchedCount} total=${result.totalCount}")
            }.onFailure { failure ->
                _state.update {
                    it.copy(
                        statusText = failure.message ?: "Manifest file list failed",
                        busy = false,
                    )
                }
                appendTrace("Manifest Files failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
            }
        }
    }

    fun verifyLocalFiles() {
        val snapshot = _state.value
        val manifestPath = snapshot.manifestPath.ifBlank {
            _state.update { it.copy(statusText = "Manifest path is required") }
            appendTrace("Verify Local blocked: manifest path is blank")
            return
        }
        val localRoot = snapshot.verifyLocalRoot.ifBlank {
            _state.update { it.copy(statusText = "Verify local root is required") }
            appendTrace("Verify Local blocked: local root is blank")
            return
        }
        appendTrace(
            "Verify Local clicked path=$manifestPath localRoot=$localRoot filter=${snapshot.filterText.ifBlank { "(none)" }}",
        )
        _state.update {
            it.copy(
                statusText = "Local verify in progress...",
                busy = true,
                localVerify = null,
            )
        }
        scope.launch {
            runCatching {
                api.verifyLocalFiles(
                    inputPath = manifestPath,
                    localRoot = localRoot,
                    depotKeyHex = snapshot.depotKeyHex.ifBlank { null },
                    filterText = snapshot.filterText.ifBlank { null },
                )
            }.onSuccess { result ->
                _state.update {
                    it.copy(
                        statusText = if (result.clean) {
                            "Local verify clean: ${result.okCount}/${result.checkedCount}"
                        } else {
                            "Local verify found ${result.missingCount} missing and ${result.mismatchedCount} mismatched"
                        },
                        busy = false,
                        localVerify = result,
                    )
                }
                appendTrace(
                    "Local verify returned clean=${result.clean} checked=${result.checkedCount} ok=${result.okCount} missing=${result.missingCount} mismatched=${result.mismatchedCount} sizeOnly=${result.sizeOnlyCount}",
                )
            }.onFailure { failure ->
                _state.update {
                    it.copy(
                        statusText = failure.message ?: "Local verify failed",
                        busy = false,
                    )
                }
                appendTrace("Verify Local failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
            }
        }
    }

    fun downloadChunk() {
        val snapshot = _state.value
        val manifestPath = snapshot.manifestPath.ifBlank {
            _state.update { it.copy(statusText = "Manifest path is required") }
            appendTrace("Chunk Download blocked: manifest path is blank")
            return
        }
        val selectedFilePath = snapshot.selectedFilePath.ifBlank {
            _state.update { it.copy(statusText = "Manifest file path is required") }
            appendTrace("Chunk Download blocked: selected file path is blank")
            return
        }
        val outputPath = snapshot.chunkOutputPath.ifBlank {
            _state.update { it.copy(statusText = "Chunk output path is required") }
            appendTrace("Chunk Download blocked: output path is blank")
            return
        }
        val chunkIndex = snapshot.chunkIndexText.toLongOrNull()
        if (chunkIndex == null || chunkIndex < 0L) {
            _state.update { it.copy(statusText = "Chunk index must be >= 0") }
            appendTrace("Chunk Download blocked: invalid chunk index='${snapshot.chunkIndexText}'")
            return
        }

        startDownloadAction(
            label = "Download Chunk",
            start = { _, maxCount, _ ->
                api.startChunkDownload(
                    inputPath = manifestPath,
                    outputPath = outputPath,
                    filePath = selectedFilePath,
                    chunkIndex = chunkIndex,
                    depotKeyHex = snapshot.depotKeyHex.ifBlank { null },
                    processChunk = snapshot.processChunk,
                    maxCount = maxCount,
                )
            },
            onSuccess = {
                onChunkDownloadCompleted(outputPath, selectedFilePath, chunkIndex, snapshot.processChunk)
            },
        )
    }

    fun downloadFile() {
        val snapshot = _state.value
        val manifestPath = snapshot.manifestPath.ifBlank {
            _state.update { it.copy(statusText = "Manifest path is required") }
            appendTrace("File Download blocked: manifest path is blank")
            return
        }
        val selectedFilePath = snapshot.selectedFilePath.ifBlank {
            _state.update { it.copy(statusText = "Manifest file path is required") }
            appendTrace("File Download blocked: selected file path is blank")
            return
        }
        val outputPath = snapshot.fileOutputPath.ifBlank {
            _state.update { it.copy(statusText = "File output path is required") }
            appendTrace("File Download blocked: output path is blank")
            return
        }
        val depotKeyHex = snapshot.depotKeyHex.ifBlank {
            _state.update { it.copy(statusText = "Depot key hex is required for file download") }
            appendTrace("File Download blocked: depot key hex is blank")
            return
        }

        startDownloadAction(
            label = "Download File",
            start = { _, maxCount, _ ->
                api.startFileDownload(
                    inputPath = manifestPath,
                    outputPath = outputPath,
                    depotKeyHex = depotKeyHex,
                    filePath = selectedFilePath,
                    maxCount = maxCount,
                )
            },
            onSuccess = { onFileDownloadCompleted(outputPath, selectedFilePath) },
        )
    }

    fun downloadAllFiles() {
        val snapshot = _state.value
        val manifestPath = snapshot.manifestPath.ifBlank {
            _state.update { it.copy(statusText = "Manifest path is required") }
            appendTrace("All Files Download blocked: manifest path is blank")
            return
        }
        val outputRoot = snapshot.allFilesOutputRoot.ifBlank {
            _state.update { it.copy(statusText = "All files output root is required") }
            appendTrace("All Files Download blocked: output root is blank")
            return
        }
        val depotKeyHex = snapshot.depotKeyHex.ifBlank {
            _state.update { it.copy(statusText = "Depot key hex is required for all-files download") }
            appendTrace("All Files Download blocked: depot key hex is blank")
            return
        }

        startDownloadAction(
            label = "Download All Files",
            start = { _, maxCount, _ ->
                api.startAllFilesDownload(
                    inputPath = manifestPath,
                    outputRoot = outputRoot,
                    depotKeyHex = depotKeyHex,
                    maxCount = maxCount,
                )
            },
            onSuccess = { onAllFilesDownloadCompleted(outputRoot) },
        )
    }

    private fun startDownloadAction(
        label: String,
        start: suspend (appId: Int, maxCount: Int, branch: String) -> Long,
        onSuccess: (() -> Unit)? = null,
    ) {
        val snapshot = _state.value
        val appId = snapshot.appIdText.toIntOrNull()
        if (appId == null || appId <= 0) {
            _state.update { it.copy(statusText = "App ID is required") }
            appendTrace("$label blocked: invalid app id='${snapshot.appIdText}'")
            return
        }

        val maxCount = snapshot.maxCountText.toIntOrNull()?.coerceIn(1, 100) ?: 5
        val branch = snapshot.branch.ifBlank { "public" }

        downloadPollingJob?.cancel()
        appendTrace("$label clicked appId=$appId branch=$branch maxCount=$maxCount")
        _state.update {
            it.copy(
                statusText = "$label in progress...",
                busy = true,
                downloadTask = null,
            )
        }
        scope.launch {
            runCatching {
                val handle = start(appId, maxCount, branch)
                pollDownloadTask(handle, label, onSuccess)
            }.onFailure { failure ->
                _state.update {
                    it.copy(
                        statusText = failure.message ?: "$label failed",
                        busy = false,
                        downloadTask = null,
                    )
                }
                appendTrace("$label failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
            }
        }
    }

    private fun runAction(
        label: String,
        action: suspend (steamId: Long, appId: Int, maxCount: Int, branch: String) -> Unit,
    ) {
        val snapshot = _state.value
        val appId = snapshot.appIdText.toIntOrNull()
        if (appId == null || appId <= 0) {
            _state.update { it.copy(statusText = "App ID is required") }
            appendTrace("$label blocked: invalid app id='${snapshot.appIdText}'")
            return
        }
        val steamId = snapshot.steamIdText.toLongOrNull()
        if (steamId == null || steamId <= 0L) {
            _state.update { it.copy(statusText = "SteamID is required") }
            appendTrace("$label blocked: invalid steam id='${snapshot.steamIdText}'")
            return
        }

        val maxCount = snapshot.maxCountText.toIntOrNull()?.coerceIn(1, 100) ?: 5
        val branch = snapshot.branch.ifBlank { "public" }

        appendTrace("$label clicked steamId=$steamId appId=$appId branch=$branch maxCount=$maxCount")
        _state.update { it.copy(statusText = "$label in progress...", busy = true) }
        scope.launch {
            runCatching {
                action(steamId, appId, maxCount, branch)
            }.onFailure { failure ->
                _state.update {
                    it.copy(
                        statusText = failure.message ?: "$label failed",
                        busy = false,
                    )
                }
                appendTrace("$label failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
            }
        }
    }

    private fun requirePositiveInt(raw: String, fieldName: String): Int {
        val value = raw.toIntOrNull()
        require(value != null && value > 0) { "$fieldName is required" }
        return value
    }

    private fun requirePositiveLong(raw: String, fieldName: String): Long {
        val value = raw.toLongOrNull()
        require(value != null && value > 0L) { "$fieldName is required" }
        return value
    }

    private fun resolveRequestCode(snapshot: CAuthSteamDepotState): Long {
        parseUnsignedDecimal(snapshot.requestCodeText)
            ?.takeIf { it.toULong() > 0uL }
            ?.let { return it }
        snapshot.manifestRequestCode
            ?.takeIf { it.present && it.requestCode.toULong() > 0uL }
            ?.let {
                _state.update { current ->
                    current.copy(requestCodeText = formatUnsignedDecimal(it.requestCode))
                }
                appendTrace("Download Manifest using request code from latest snapshot")
                return it.requestCode
            }
        error("Request code is required")
    }

    private fun sanitizeCompact(value: String): String = value.filterNot { it.isWhitespace() }

    private fun sanitizeTrimmed(value: String): String = value.trim()

    private fun suggestFileOutputPath(manifestPath: String, selectedFilePath: String): String? {
        if (manifestPath.isBlank() || selectedFilePath.isBlank()) {
            return null
        }
        val baseDirectory = File(manifestPath).parentFile ?: return null
        val safeSegments = selectedFilePath
            .replace('\\', '/')
            .split('/')
            .filter { segment ->
                segment.isNotBlank() && segment != "." && segment != ".."
            }
        if (safeSegments.isEmpty()) {
            return null
        }
        return safeSegments
            .fold(baseDirectory) { current, segment -> File(current, segment) }
            .absolutePath
    }

    private suspend fun pollDownloadTask(
        handle: Long,
        label: String,
        onSuccess: (() -> Unit)?,
    ) {
        downloadPollingJob?.cancel()
        downloadPollingJob = scope.launch {
            while (true) {
                val snapshot = api.pollDownloadTask(handle)
                _state.update {
                    it.copy(
                        busy = snapshot.active,
                        statusText = buildDownloadStatusText(label, snapshot),
                        downloadTask = snapshot,
                    )
                }
                if (snapshot.finished) {
                    try {
                        when {
                            snapshot.succeeded -> onSuccess?.invoke()
                            snapshot.canceled -> {
                                _state.update { state ->
                                    state.copy(
                                        statusText = "Download canceled",
                                        busy = false,
                                        downloadTask = snapshot,
                                    )
                                }
                                appendTrace("$label canceled")
                            }
                            else -> {
                                _state.update { state ->
                                    state.copy(
                                        statusText = snapshot.message.ifBlank { "$label failed" },
                                        busy = false,
                                        downloadTask = snapshot,
                                    )
                                }
                                appendTrace("$label failed: ${snapshot.message.ifBlank { "(no detail)" }}")
                            }
                        }
                    } finally {
                        api.disposeDownloadTask(handle)
                    }
                    break
                }
                delay(150)
            }
        }
        downloadPollingJob?.join()
        downloadPollingJob = null
    }

    private fun buildDownloadStatusText(label: String, snapshot: DepotDownloadTaskSnapshot): String {
        if (snapshot.finished) {
            return when {
                snapshot.succeeded -> "$label complete"
                snapshot.canceled -> "Download canceled"
                else -> snapshot.message.ifBlank { "$label failed" }
            }
        }
        return buildString {
            append(snapshot.kindLabel)
            append(": ")
            append(snapshot.phase.ifBlank { label })
            val summary = snapshot.progressSummary
            if (summary.isNotBlank() && summary != snapshot.phase) {
                append(" (")
                append(summary)
                append(')')
            }
        }
    }

    private fun appendTrace(message: String) {
        _state.update { current ->
            current.copy(
                traceLines = buildList {
                    add(message)
                    addAll(current.traceLines.take(13))
                },
            )
        }
    }
}
