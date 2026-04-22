package com.cauth.android.steam.cloud

import com.cauth.android.CAuthClient
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class CAuthSteamCloudState(
    val appIdText: String = "",
    val steamIdText: String = "",
    val localRoot: String = "",
    val remoteRoot: String = "",
    val accessToken: String = "",
    val countText: String = "20",
    val startIndexText: String = "0",
    val dryRun: Boolean = false,
    val deleteRemoteOrphans: Boolean = false,
    val verifyIncludeExtraLocal: Boolean = false,
    val conflictPolicy: SteamCloudConflictPolicy = SteamCloudConflictPolicy.Default,
    val moduleStatus: String = "idle",
    val statusText: String = "Ready",
    val busy: Boolean = false,
    val fileList: SteamCloudFileListSnapshot? = null,
    val verifyResult: SteamCloudVerifySnapshot? = null,
    val operationResult: SteamCloudResultSnapshot? = null,
    val moduleTask: SteamCloudModuleTaskSnapshot? = null,
    val transferTask: SteamCloudTransferTaskSnapshot? = null,
    val traceLines: List<String> = emptyList(),
)

class CAuthSteamCloudController(
    private val client: CAuthClient,
    private val scope: CoroutineScope,
) {
    private val api = CAuthSteamCloudApi(client)
    private val _state = MutableStateFlow(CAuthSteamCloudState())
    val state: StateFlow<CAuthSteamCloudState> = _state
    private var transferPollingJob: Job? = null
    private var idleResetJob: Job? = null

    fun setAppIdText(value: String) = _state.update { it.copy(appIdText = sanitizeCompact(value)) }
    fun setSteamIdText(value: String) = _state.update { it.copy(steamIdText = sanitizeCompact(value)) }
    fun setLocalRoot(value: String) = _state.update { it.copy(localRoot = sanitizeTrimmed(value)) }
    fun setRemoteRoot(value: String) = _state.update { it.copy(remoteRoot = sanitizeTrimmed(value)) }
    fun setAccessToken(value: String) = _state.update { it.copy(accessToken = sanitizeCompact(value)) }
    fun setCountText(value: String) = _state.update { it.copy(countText = sanitizeCompact(value)) }
    fun setStartIndexText(value: String) = _state.update { it.copy(startIndexText = sanitizeCompact(value)) }
    fun setDryRun(value: Boolean) = _state.update { it.copy(dryRun = value) }
    fun setDeleteRemoteOrphans(value: Boolean) = _state.update { it.copy(deleteRemoteOrphans = value) }
    fun setVerifyIncludeExtraLocal(value: Boolean) = _state.update { it.copy(verifyIncludeExtraLocal = value) }
    fun setConflictPolicy(value: SteamCloudConflictPolicy) = _state.update { it.copy(conflictPolicy = value) }

    fun cancelActiveTransfer() {
        val handle = _state.value.transferTask?.handle ?: return
        scope.launch {
            runCatching { api.cancelTransferTask(handle) }
            cancelIdleReset()
            _state.update { current ->
                val currentTask = current.transferTask
                current.copy(
                    statusText = "Cancel requested...",
                    moduleStatus = "canceling",
                    busy = true,
                    moduleTask = SteamCloudModuleTaskSnapshot(
                        label = current.moduleTask?.label ?: currentTask?.kindLabel ?: "Cloud Task",
                        active = true,
                        moduleStatus = "canceling",
                        message = "Cancel requested...",
                        transferTask = currentTask?.copy(moduleStatus = "canceling"),
                    ),
                    transferTask = currentTask?.copy(moduleStatus = "canceling"),
                )
            }
            appendTrace("Cloud transfer cancel requested handle=$handle")
        }
    }

    fun listRemoteFiles() {
        runAction("Cloud List") { request, count, startIndex ->
            val result = api.listRemoteFiles(
                request = request,
                count = count,
                startIndex = startIndex,
                extendedDetails = true,
            )
            finishModuleTask(
                label = "Cloud List",
                moduleStatus = result.moduleStatus.ifBlank { if (result.ok) "succeeded" else "failed" },
                message = result.message.ifBlank { "Listed ${result.files.size} file(s)" },
            ) {
                it.copy(fileList = result)
            }
            appendTrace("Cloud List returned ok=${result.ok} files=${result.files.size} total=${result.totalFiles} eresult=${result.eresult}")
        }
    }

    fun pull() {
        startTransferAction("Cloud Pull") { request -> api.startPull(request) }
    }

    fun push() {
        startTransferAction("Cloud Push") { request -> api.startPush(request) }
    }

    fun verifyLocalFiles() {
        val includeExtraLocal = _state.value.verifyIncludeExtraLocal
        startTransferAction("Cloud Verify") { request ->
            api.startVerifyLocalFiles(
                request = request,
                includeExtraLocal = includeExtraLocal,
            )
        }
    }

    private fun runAction(
        label: String,
        action: suspend (request: SteamCloudRequest, count: Int, startIndex: Int) -> Unit,
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

        val request = SteamCloudRequest(
            appId = appId,
            steamId = steamId,
            accessToken = snapshot.accessToken.ifBlank { null },
            localRoot = snapshot.localRoot.ifBlank { null },
            remoteRoot = snapshot.remoteRoot.ifBlank { null },
            dryRun = snapshot.dryRun,
            deleteRemoteOrphans = snapshot.deleteRemoteOrphans,
            conflictPolicy = snapshot.conflictPolicy,
        )
        val count = snapshot.countText.toIntOrNull()?.coerceIn(1, 200) ?: 20
        val startIndex = snapshot.startIndexText.toIntOrNull()?.coerceAtLeast(0) ?: 0

        appendTrace(
            "$label clicked steamId=$steamId appId=$appId localRoot=${request.localRoot ?: "(none)"} " +
                "remoteRoot=${request.remoteRoot ?: "(none)"} dryRun=${request.dryRun}",
        )
        beginModuleTask(
            label = label,
            moduleStatus = "reading",
            message = "$label in progress...",
        ) {
            it.copy(transferTask = null)
        }
        scope.launch {
            runCatching {
                action(request, count, startIndex)
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

    private fun startTransferAction(
        label: String,
        start: suspend (SteamCloudRequest) -> Long,
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

        val request = SteamCloudRequest(
            appId = appId,
            steamId = steamId,
            accessToken = snapshot.accessToken.ifBlank { null },
            localRoot = snapshot.localRoot.ifBlank { null },
            remoteRoot = snapshot.remoteRoot.ifBlank { null },
            dryRun = snapshot.dryRun,
            deleteRemoteOrphans = snapshot.deleteRemoteOrphans,
            conflictPolicy = snapshot.conflictPolicy,
        )

        appendTrace(
            "$label clicked steamId=$steamId appId=$appId localRoot=${request.localRoot ?: "(none)"} " +
                "remoteRoot=${request.remoteRoot ?: "(none)"} dryRun=${request.dryRun}",
        )
        transferPollingJob?.cancel()
        beginModuleTask(
            label = label,
            moduleStatus = "queued",
            message = "$label in progress...",
        ) {
            it.copy(
                verifyResult = null,
                operationResult = null,
                transferTask = null,
            )
        }
        scope.launch {
            runCatching {
                val handle = start(request)
                appendTrace("$label started handle=$handle")
                pollTransferTask(handle, label)
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

    private fun pollTransferTask(handle: Long, label: String) {
        transferPollingJob?.cancel()
        transferPollingJob = scope.launch {
            try {
                while (true) {
                    val snapshot = api.pollTransferTask(handle)
                    val message = snapshot.message.ifBlank {
                        snapshot.phase.ifBlank { "$label in progress..." }
                    }
                    val snapshotStatus = snapshot.moduleStatus.ifBlank {
                        if (snapshot.active) "running" else "idle"
                    }
                    _state.update {
                        it.copy(
                            statusText = message,
                            moduleStatus = snapshotStatus,
                            busy = snapshot.active,
                            moduleTask = SteamCloudModuleTaskSnapshot(
                                label = label,
                                active = snapshot.active,
                                moduleStatus = snapshotStatus,
                                message = message,
                                transferTask = snapshot,
                            ),
                            transferTask = snapshot,
                            operationResult = snapshot.result ?: it.operationResult,
                            verifyResult = snapshot.verifyResult ?: it.verifyResult,
                        )
                    }
                    if (!snapshot.active) {
                        val result = snapshot.result
                        val verifyResult = snapshot.verifyResult
                        if (result != null) {
                            appendTrace(
                                "$label returned ok=${result.ok} transferred=${result.transferredCount} conflicts=${result.conflictCount}",
                            )
                        } else if (verifyResult != null) {
                            appendTrace(
                                "$label returned clean=${verifyResult.clean} checked=${verifyResult.checkedCount} ok=${verifyResult.okCount} missing=${verifyResult.missingCount} mismatched=${verifyResult.mismatchedCount} extraLocal=${verifyResult.extraLocalCount}",
                            )
                        } else {
                            appendTrace(
                                "$label finished active=${snapshot.active} succeeded=${snapshot.succeeded} canceled=${snapshot.canceled}",
                            )
                        }
                        finishModuleTask(
                            label = label,
                            moduleStatus = snapshot.moduleStatus.ifBlank {
                                when {
                                    snapshot.canceled -> "canceled"
                                    snapshot.succeeded -> "succeeded"
                                    else -> "failed"
                                }
                            },
                            message = when {
                                snapshot.canceled -> "${snapshot.kindLabel} canceled"
                                snapshot.message.isNotBlank() -> snapshot.message
                                snapshot.succeeded -> "$label complete"
                                else -> "$label failed"
                            },
                            transferTask = snapshot,
                        ) { state ->
                            state.copy(
                                operationResult = snapshot.result ?: state.operationResult,
                                verifyResult = snapshot.verifyResult ?: state.verifyResult,
                            )
                        }
                        break
                    }
                    delay(250)
                }
            } finally {
                runCatching { api.disposeTransferTask(handle) }
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

    private fun beginModuleTask(
        label: String,
        moduleStatus: String,
        message: String,
        transform: (CAuthSteamCloudState) -> CAuthSteamCloudState = { it },
    ) {
        cancelIdleReset()
        _state.update { current ->
            transform(
                current.copy(
                    statusText = message,
                    moduleStatus = moduleStatus,
                    busy = true,
                    moduleTask = SteamCloudModuleTaskSnapshot(
                        label = label,
                        active = true,
                        moduleStatus = moduleStatus,
                        message = message,
                        transferTask = current.transferTask,
                    ),
                ),
            )
        }
    }

    private fun finishModuleTask(
        label: String,
        moduleStatus: String,
        message: String,
        transferTask: SteamCloudTransferTaskSnapshot? = _state.value.transferTask,
        transform: (CAuthSteamCloudState) -> CAuthSteamCloudState = { it },
    ) {
        cancelIdleReset()
        _state.update { current ->
            transform(
                current.copy(
                    statusText = message,
                    moduleStatus = moduleStatus,
                    busy = false,
                    moduleTask = SteamCloudModuleTaskSnapshot(
                        label = label,
                        active = false,
                        moduleStatus = moduleStatus,
                        message = message,
                        transferTask = transferTask,
                    ),
                    transferTask = transferTask,
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
        "listing",
        "reading",
        "downloading",
        "uploading",
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
                    transferTask = null,
                )
            }
            idleResetJob = null
        }
    }

    private fun sanitizeCompact(value: String): String = value.filterNot { it.isWhitespace() }

    private fun sanitizeTrimmed(value: String): String = value.trim()

    private companion object {
        const val IDLE_RESET_DELAY_MS = 2500L
    }
}
