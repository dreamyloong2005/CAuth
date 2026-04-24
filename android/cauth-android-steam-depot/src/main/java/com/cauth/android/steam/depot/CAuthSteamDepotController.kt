package com.cauth.android.steam.depot

import com.cauth.android.CAuthClient
import com.cauth.android.CAuthRouteProbeEntrySnapshot
import com.cauth.android.CAuthRouteProbeSnapshot
import com.cauth.android.CAuthRouteSelection
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
    val routeEndpoint: String = "",
    val routeProtocol: String = "",
    val routeRole: String = "",
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
    val moduleStatus: String = "idle",
    val statusText: String = "Ready",
    val busy: Boolean = false,
    val branches: AppBranchListSnapshot? = null,
    val manifests: DepotManifestListSnapshot? = null,
    val preflight: DepotPreflightSnapshot? = null,
    val depotKey: DepotKeySnapshot? = null,
    val routeProbe: CAuthRouteProbeSnapshot? = null,
    val manifestRequestCode: ManifestRequestCodeSnapshot? = null,
    val manifestInfo: ManifestInfoSnapshot? = null,
    val manifestFiles: ManifestFileListSnapshot? = null,
    val localVerify: DepotLocalVerifySnapshot? = null,
    val moduleTask: DepotModuleTaskSnapshot? = null,
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
    private var idleResetJob: Job? = null

    fun setAppIdText(value: String) = _state.update { it.copy(appIdText = sanitizeCompact(value)) }
    fun setSteamIdText(value: String) = _state.update { it.copy(steamIdText = sanitizeCompact(value)) }
    fun setBranch(value: String) = _state.update { it.copy(branch = sanitizeTrimmed(value)) }
    fun setMaxCountText(value: String) = _state.update { it.copy(maxCountText = sanitizeCompact(value)) }
    fun setDepotIdText(value: String) = _state.update { it.copy(depotIdText = sanitizeCompact(value)) }
    fun setManifestGidText(value: String) = _state.update { it.copy(manifestGidText = sanitizeCompact(value)) }
    fun setRequestCodeText(value: String) = _state.update { it.copy(requestCodeText = sanitizeCompact(value)) }
    fun setBranchPasswordHash(value: String) = _state.update { it.copy(branchPasswordHash = sanitizeCompact(value)) }
    fun setRouteEndpoint(value: String) = _state.update { it.copy(routeEndpoint = sanitizeTrimmed(value)) }
    fun setRouteProtocol(value: String) = _state.update { it.copy(routeProtocol = sanitizeTrimmed(value)) }
    fun setRouteRole(value: String) = _state.update { it.copy(routeRole = sanitizeTrimmed(value)) }
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
            cancelIdleReset()
            _state.update { current ->
                val currentTask = current.downloadTask
                current.copy(
                    statusText = "Cancel requested...",
                    moduleStatus = "canceling",
                    busy = true,
                    moduleTask = DepotModuleTaskSnapshot(
                        label = current.moduleTask?.label ?: currentTask?.kindLabel ?: "Depot Task",
                        active = true,
                        moduleStatus = "canceling",
                        message = "Cancel requested...",
                        downloadTask = currentTask?.copy(moduleStatus = "canceling"),
                    ),
                    downloadTask = currentTask?.copy(moduleStatus = "canceling"),
                )
            }
            appendTrace("Download cancel requested handle=$handle")
        }
    }

    fun pauseActiveDownload() {
        val handle = _state.value.downloadTask?.handle ?: return
        scope.launch {
            runCatching { api.pauseDownloadTask(handle) }
            cancelIdleReset()
            _state.update { current ->
                val currentTask = current.downloadTask
                current.copy(
                    statusText = "Pause requested...",
                    moduleStatus = "pausing",
                    busy = true,
                    moduleTask = DepotModuleTaskSnapshot(
                        label = current.moduleTask?.label ?: currentTask?.kindLabel ?: "Depot Task",
                        active = true,
                        moduleStatus = "pausing",
                        message = "Pause requested...",
                        downloadTask = currentTask?.copy(moduleStatus = "pausing"),
                    ),
                    downloadTask = currentTask?.copy(moduleStatus = "pausing"),
                )
            }
            appendTrace("Download pause requested handle=$handle")
        }
    }

    fun useManifestSelection(depotId: Int, manifestGid: Long) {
        cancelIdleReset()
        _state.update {
            it.copy(
                depotIdText = depotId.toString(),
                manifestGidText = manifestGid.toString(),
                requestCodeText = "",
                manifestRequestCode = null,
                statusText = "Selected depot=$depotId manifest=$manifestGid",
                moduleStatus = "idle",
                busy = false,
                moduleTask = null,
                downloadTask = null,
            )
        }
        appendTrace("Manifest selected depotId=$depotId manifestGid=$manifestGid")
    }

    fun prepareKeyAndCodeSelection(depotId: Int, manifestGid: Long) {
        cancelIdleReset()
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
                moduleStatus = "idle",
                busy = false,
                moduleTask = null,
            )
        }
        appendTrace("Prepared key/code selection depotId=$depotId manifestGid=$manifestGid")
    }

    fun probeDownloadRoutes() {
        val maxCount = _state.value.maxCountText.toIntOrNull()?.coerceIn(1, 100) ?: 20
        appendTrace("Depot Routes clicked maxCount=$maxCount")
        beginModuleTask(
            label = "Probe Download Routes",
            moduleStatus = "probing",
            message = "Fetching depot routes...",
        ) {
            it.copy(downloadTask = null)
        }
        scope.launch {
            runCatching { api.probeDownloadRoutes(maxCount = maxCount) }
                .onSuccess { result ->
                    finishModuleTask(
                        label = "Probe Download Routes",
                        moduleStatus = result.moduleStatus.ifBlank { if (result.ok) "succeeded" else "failed" },
                        message = result.message.ifBlank {
                            if (result.ok) "Fetched ${result.routes.size} route(s)" else "Depot route probe failed"
                        },
                    ) {
                        it.copy(routeProbe = result)
                    }
                    appendTrace(
                        "Depot Routes returned ok=${result.ok} backend=${result.backend.ifBlank { "(none)" }} routes=${result.routes.size}",
                    )
                }
                .onFailure { failure ->
                    finishModuleTask(
                        label = "Probe Download Routes",
                        moduleStatus = "failed",
                        message = failure.message ?: "Depot route probe failed",
                    )
                    appendTrace("Depot Routes failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
                }
        }
    }

    fun useRoute(route: CAuthRouteProbeEntrySnapshot) {
        cancelIdleReset()
        _state.update {
            it.copy(
                routeEndpoint = route.endpoint,
                routeProtocol = route.protocol,
                routeRole = route.role,
                moduleStatus = "idle",
                statusText = "Route selected: ${route.endpoint}",
                busy = false,
                moduleTask = null,
                downloadTask = null,
            )
        }
        appendTrace(
            "Depot route selected endpoint=${route.endpoint} protocol=${route.protocol.ifBlank { "(none)" }} role=${route.role.ifBlank { "(none)" }}",
        )
    }

    fun useManifestFile(path: String) {
        val suggestedOutputPath = suggestFileOutputPath(
            manifestPath = _state.value.manifestPath,
            selectedFilePath = path,
        )
        cancelIdleReset()
        _state.update {
            it.copy(
                selectedFilePath = path,
                fileOutputPath = suggestedOutputPath ?: it.fileOutputPath,
                moduleStatus = "idle",
                statusText = "Selected file $path",
                busy = false,
                moduleTask = null,
                downloadTask = null,
            )
        }
        appendTrace(
            "Manifest file selected path=$path" +
                (suggestedOutputPath?.let { " autoOutput=$it" } ?: ""),
        )
    }

    fun fetchBranches() {
        runAction("Fetch Branches") { steamId, appId, maxCount, _ ->
            val routeSelection = _state.value.routeSelection()
            val result = api.fetchBranches(
                steamId = steamId,
                appId = appId,
                maxCount = maxCount,
                routeSelection = routeSelection,
            )
            finishModuleTask(
                label = "Fetch Branches",
                moduleStatus = if (result.present) "succeeded" else "failed",
                message = when {
                    !result.present -> "No branch list was returned for app $appId"
                    result.branches.isEmpty() -> "App $appId returned 0 branches"
                    else -> "Fetched ${result.branches.size} branch(es)"
                },
            ) {
                it.copy(branches = result)
            }
            appendTrace("Branches returned count=${result.branches.size} present=${result.present}")
        }
    }

    fun fetchManifests() {
        runAction("Fetch Manifests") { steamId, appId, maxCount, branch ->
            val routeSelection = _state.value.routeSelection()
            val result = api.fetchManifests(
                steamId = steamId,
                appId = appId,
                branch = branch,
                maxCount = maxCount,
                routeSelection = routeSelection,
            )
            finishModuleTask(
                label = "Fetch Manifests",
                moduleStatus = if (result.present) "succeeded" else "failed",
                message = when {
                    !result.present -> "No manifests were returned for branch $branch"
                    result.manifests.isEmpty() -> "Branch $branch returned 0 manifests"
                    else -> "Fetched ${result.manifests.size} manifest(s) for $branch"
                },
            ) {
                it.copy(manifests = result)
            }
            appendTrace("Manifests returned count=${result.manifests.size} branch=$branch present=${result.present}")
        }
    }

    fun fetchPreflight() {
        _state.update { it.copy(preflight = null) }
        runAction("Fetch Preflight") { steamId, appId, maxCount, branch ->
            val routeSelection = _state.value.routeSelection()
            val result = api.fetchPreflight(
                steamId = steamId,
                appId = appId,
                branch = branch,
                maxCount = maxCount,
                routeSelection = routeSelection,
            )
            finishModuleTask(
                label = "Fetch Preflight",
                moduleStatus = if (result.present) "succeeded" else "failed",
                message = when {
                    !result.present -> "No preflight result was returned for branch $branch"
                    result.depots.isEmpty() -> "Preflight returned 0 depots for branch $branch"
                    else -> "Fetched ${result.depots.size} depot preflight entr${if (result.depots.size == 1) "y" else "ies"}"
                },
            ) {
                it.copy(preflight = result)
            }
            appendTrace("Preflight returned count=${result.depots.size} branch=$branch build=${result.buildId}")
        }
    }

    fun fetchDepotKey() {
        _state.update { it.copy(depotKey = null, depotKeyHex = "") }
        runAction("Fetch Depot Key") { steamId, appId, maxCount, _ ->
            val depotId = requirePositiveInt(_state.value.depotIdText, "Depot ID")
            val routeSelection = _state.value.routeSelection()
            val result = api.fetchDepotKey(
                steamId = steamId,
                appId = appId,
                depotId = depotId,
                maxCount = maxCount,
                routeSelection = routeSelection,
            )
            finishModuleTask(
                label = "Fetch Depot Key",
                moduleStatus = if (result.present && result.keyHex.isNotBlank()) "succeeded" else "failed",
                message = if (result.present && result.keyHex.isNotBlank()) {
                    "Fetched depot key for $depotId"
                } else {
                    "Depot key was not returned for $depotId"
                },
            ) {
                it.copy(
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
            val routeSelection = _state.value.routeSelection()
            val result = api.fetchManifestRequestCode(
                steamId = steamId,
                appId = appId,
                depotId = depotId,
                manifestGid = manifestGid,
                branch = branch,
                branchPasswordHash = _state.value.branchPasswordHash.ifBlank { null },
                maxCount = maxCount,
                routeSelection = routeSelection,
            )
            finishModuleTask(
                label = "Fetch Manifest Code",
                moduleStatus = if (result.present && result.requestCode.toULong() > 0uL) "succeeded" else "failed",
                message = if (result.present && result.requestCode.toULong() > 0uL) {
                    "Fetched manifest request code"
                } else {
                    "Manifest request code was not returned; check depot, manifest, branch, and ownership"
                },
            ) {
                it.copy(
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
                    routeSelection = _state.value.routeSelection(),
                )
            },
            onSuccess = { _ -> onManifestDownloadCompleted(outputPath) },
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
        beginModuleTask(
            label = "Manifest Info",
            moduleStatus = "reading",
            message = "Manifest info in progress...",
        ) {
            it.copy(manifestInfo = null, downloadTask = null)
        }
        scope.launch {
            runCatching {
                api.loadManifestInfo(
                    inputPath = manifestPath,
                    depotKeyHex = snapshot.depotKeyHex.ifBlank { null },
                )
            }.onSuccess { result ->
                finishModuleTask(
                    label = "Manifest Info",
                    moduleStatus = if (result.present) "succeeded" else "failed",
                    message = if (result.present) {
                        "Manifest info loaded for depot ${result.depotId}"
                    } else {
                        "Manifest info was not returned"
                    },
                ) {
                    it.copy(manifestInfo = result)
                }
                appendTrace("Manifest info returned present=${result.present} files=${result.fileCount} chunks=${result.chunkCount}")
            }.onFailure { failure ->
                finishModuleTask(
                    label = "Manifest Info",
                    moduleStatus = "failed",
                    message = failure.message ?: "Manifest info failed",
                )
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
        beginModuleTask(
            label = "Manifest Files",
            moduleStatus = "reading",
            message = "Manifest file list in progress...",
        ) {
            it.copy(manifestFiles = null, downloadTask = null)
        }
        scope.launch {
            runCatching {
                api.listManifestFiles(
                    inputPath = manifestPath,
                    depotKeyHex = snapshot.depotKeyHex.ifBlank { null },
                    filterText = snapshot.filterText.ifBlank { null },
                    limit = limit,
                )
            }.onSuccess { result ->
                finishModuleTask(
                    label = "Manifest Files",
                    moduleStatus = if (result.present) "succeeded" else "failed",
                    message = when {
                        !result.present -> "Manifest file list was not returned"
                        result.matchedCount == 0L -> "Manifest file list returned 0 matches"
                        else -> "Listed ${result.printedCount} manifest file(s)"
                    },
                ) {
                    it.copy(manifestFiles = result)
                }
                appendTrace("Manifest files returned printed=${result.printedCount} matched=${result.matchedCount} total=${result.totalCount}")
            }.onFailure { failure ->
                finishModuleTask(
                    label = "Manifest Files",
                    moduleStatus = "failed",
                    message = failure.message ?: "Manifest file list failed",
                )
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
        _state.update { it.copy(localVerify = null) }
        startDownloadAction(
            label = "Verify Local Files",
            start = { _, _, _ ->
                api.startVerifyLocalFiles(
                    inputPath = manifestPath,
                    localRoot = localRoot,
                    depotKeyHex = snapshot.depotKeyHex.ifBlank { null },
                    filterText = snapshot.filterText.ifBlank { null },
                )
            },
        )
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
                    routeSelection = snapshot.routeSelection(),
                )
            },
            onSuccess = { _ ->
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
                    routeSelection = snapshot.routeSelection(),
                )
            },
            onSuccess = { _ -> onFileDownloadCompleted(outputPath, selectedFilePath) },
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
                    routeSelection = snapshot.routeSelection(),
                )
            },
            onSuccess = { _ -> onAllFilesDownloadCompleted(outputRoot) },
        )
    }

    private fun startDownloadAction(
        label: String,
        start: suspend (appId: Int, maxCount: Int, branch: String) -> Long,
        onSuccess: ((DepotDownloadTaskSnapshot) -> Unit)? = null,
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
        beginModuleTask(
            label = label,
            moduleStatus = "queued",
            message = "$label in progress...",
        ) {
            it.copy(downloadTask = null)
        }
        scope.launch {
            runCatching {
                val handle = start(appId, maxCount, branch)
                appendTrace("$label started handle=$handle")
                pollDownloadTask(handle, label, onSuccess)
            }.onFailure { failure ->
                finishModuleTask(
                    label = label,
                    moduleStatus = "failed",
                    message = failure.message ?: "$label failed",
                ) {
                    it.copy(downloadTask = null)
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
        beginModuleTask(
            label = label,
            moduleStatus = "reading",
            message = "$label in progress...",
        ) {
            it.copy(downloadTask = null)
        }
        scope.launch {
            runCatching {
                action(steamId, appId, maxCount, branch)
            }.onFailure { failure ->
                finishModuleTask(
                    label = label,
                    moduleStatus = "failed",
                    message = failure.message ?: "$label failed",
                )
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

    private fun CAuthSteamDepotState.routeSelection(): CAuthRouteSelection? {
        val selection = CAuthRouteSelection(
            endpoint = routeEndpoint.ifBlank { null },
            protocol = routeProtocol.ifBlank { null },
            role = routeRole.ifBlank { null },
        )
        return selection.takeUnless { it.isEmpty() }
    }

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
        onSuccess: ((DepotDownloadTaskSnapshot) -> Unit)?,
    ) {
        downloadPollingJob?.cancel()
        downloadPollingJob = scope.launch {
            while (true) {
                val snapshot = api.pollDownloadTask(handle)
                val progressMessage = buildDownloadStatusText(label, snapshot)
                val snapshotStatus = snapshot.moduleStatus.ifBlank {
                    if (snapshot.active) "running" else "idle"
                }
                _state.update {
                    it.copy(
                        busy = snapshot.active,
                        moduleStatus = snapshotStatus,
                        statusText = progressMessage,
                        moduleTask = DepotModuleTaskSnapshot(
                            label = label,
                            active = snapshot.active,
                            moduleStatus = snapshotStatus,
                            message = progressMessage,
                            downloadTask = snapshot,
                        ),
                        downloadTask = snapshot,
                    )
                }
                if (snapshot.finished) {
                    try {
                        when {
                            snapshot.succeeded -> {
                                snapshot.verifyResult?.let { verifyResult ->
                                    finishModuleTask(
                                        label = label,
                                        moduleStatus = verifyResult.moduleStatus.ifBlank { if (verifyResult.clean) "succeeded" else "failed" },
                                        message = if (verifyResult.clean) {
                                            "Local verify clean: ${verifyResult.okCount}/${verifyResult.checkedCount}"
                                        } else {
                                            "Local verify found ${verifyResult.missingCount} missing and ${verifyResult.mismatchedCount} mismatched"
                                        },
                                        downloadTask = snapshot,
                                    ) {
                                        it.copy(localVerify = verifyResult)
                                    }
                                    appendTrace(
                                        "$label returned clean=${verifyResult.clean} checked=${verifyResult.checkedCount} ok=${verifyResult.okCount} missing=${verifyResult.missingCount} mismatched=${verifyResult.mismatchedCount} sizeOnly=${verifyResult.sizeOnlyCount}",
                                    )
                                }
                                if (snapshot.verifyResult == null) {
                                    onSuccess?.invoke(snapshot)
                                    finishModuleTask(
                                        label = label,
                                        moduleStatus = snapshot.moduleStatus.ifBlank { "succeeded" },
                                        message = _state.value.statusText,
                                        downloadTask = snapshot,
                                    )
                                }
                            }
                            snapshot.paused -> {
                                finishModuleTask(
                                    label = label,
                                    moduleStatus = "paused",
                                    message = snapshot.message.ifBlank { "${snapshot.kindLabel} paused" },
                                    downloadTask = snapshot,
                                )
                                appendTrace("$label paused")
                            }
                            snapshot.canceled -> {
                                finishModuleTask(
                                    label = label,
                                    moduleStatus = "canceled",
                                    message = "${snapshot.kindLabel} canceled",
                                    downloadTask = snapshot,
                                )
                                appendTrace("$label canceled")
                            }
                            else -> {
                                finishModuleTask(
                                    label = label,
                                    moduleStatus = snapshot.moduleStatus.ifBlank { "failed" },
                                    message = snapshot.message.ifBlank { "$label failed" },
                                    downloadTask = snapshot,
                                )
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
                snapshot.paused -> "${snapshot.kindLabel} paused"
                snapshot.canceled -> "${snapshot.kindLabel} canceled"
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

    private fun beginModuleTask(
        label: String,
        moduleStatus: String,
        message: String,
        transform: (CAuthSteamDepotState) -> CAuthSteamDepotState = { it },
    ) {
        cancelIdleReset()
        _state.update { current ->
            transform(
                current.copy(
                    statusText = message,
                    moduleStatus = moduleStatus,
                    busy = true,
                    moduleTask = DepotModuleTaskSnapshot(
                        label = label,
                        active = true,
                        moduleStatus = moduleStatus,
                        message = message,
                        downloadTask = current.downloadTask,
                    ),
                ),
            )
        }
    }

    private fun finishModuleTask(
        label: String,
        moduleStatus: String,
        message: String,
        downloadTask: DepotDownloadTaskSnapshot? = _state.value.downloadTask,
        transform: (CAuthSteamDepotState) -> CAuthSteamDepotState = { it },
    ) {
        cancelIdleReset()
        _state.update { current ->
            transform(
                current.copy(
                    statusText = message,
                    moduleStatus = moduleStatus,
                    busy = false,
                    moduleTask = DepotModuleTaskSnapshot(
                        label = label,
                        active = false,
                        moduleStatus = moduleStatus,
                        message = message,
                        downloadTask = downloadTask,
                    ),
                    downloadTask = downloadTask,
                ),
            )
        }
        if (shouldAutoResetToIdle(moduleStatus)) {
            scheduleIdleReset()
        }
    }

    private fun shouldAutoResetToIdle(moduleStatus: String): Boolean = moduleStatus.lowercase() !in setOf(
        "",
        "idle",
        "queued",
        "downloading",
        "reading",
        "writing",
        "verifying",
        "canceling",
        "running",
    )

    private fun cancelIdleReset() {
        idleResetJob?.cancel()
        idleResetJob = null
    }

    private fun scheduleIdleReset() {
        cancelIdleReset()
        idleResetJob = scope.launch {
            delay(IDLE_RESET_DELAY_MS)
            _state.update { current ->
                current.copy(
                    moduleStatus = "idle",
                    busy = false,
                    moduleTask = null,
                    downloadTask = null,
                )
            }
            idleResetJob = null
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

    private companion object {
        const val IDLE_RESET_DELAY_MS = 2500L
    }
}
