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
    val localRoot: String = "",
    val remoteRoot: String = "",
    val accessToken: String = "",
    val countText: String = "20",
    val startIndexText: String = "0",
    val dryRun: Boolean = false,
    val deleteRemoteOrphans: Boolean = false,
    val verifyIncludeExtraLocal: Boolean = false,
    val conflictPolicy: SteamCloudConflictPolicy = SteamCloudConflictPolicy.Default,
    val statusText: String = "Ready",
    val busy: Boolean = false,
    val fileList: SteamCloudFileListSnapshot? = null,
    val verifyResult: SteamCloudVerifySnapshot? = null,
    val operationResult: SteamCloudResultSnapshot? = null,
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

    fun setAppIdText(value: String) = _state.update { it.copy(appIdText = sanitizeCompact(value)) }
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
            _state.update {
                it.copy(statusText = "Cancel requested...", busy = true)
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
            _state.update {
                it.copy(
                    statusText = result.message.ifBlank { "Listed ${result.files.size} file(s)" },
                    busy = false,
                    fileList = result,
                )
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
        runAction("Cloud Verify") { request, _, _ ->
            val includeExtraLocal = _state.value.verifyIncludeExtraLocal
            val result = api.verifyLocalFiles(
                request = request,
                includeExtraLocal = includeExtraLocal,
            )
            _state.update {
                it.copy(
                    statusText = if (result.clean) {
                        "Cloud verify clean: ${result.okCount}/${result.checkedCount}"
                    } else {
                        "Cloud verify found ${result.missingCount} missing and ${result.mismatchedCount} mismatched"
                    },
                    busy = false,
                    verifyResult = result,
                )
            }
            appendTrace(
                "Cloud Verify returned clean=${result.clean} checked=${result.checkedCount} ok=${result.okCount} missing=${result.missingCount} mismatched=${result.mismatchedCount} extraLocal=${result.extraLocalCount}",
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

        val request = SteamCloudRequest(
            appId = appId,
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
            "$label clicked appId=$appId localRoot=${request.localRoot ?: "(none)"} " +
                "remoteRoot=${request.remoteRoot ?: "(none)"} dryRun=${request.dryRun}",
        )
        _state.update { it.copy(statusText = "$label in progress...", busy = true) }
        scope.launch {
            runCatching {
                action(request, count, startIndex)
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

        val request = SteamCloudRequest(
            appId = appId,
            accessToken = snapshot.accessToken.ifBlank { null },
            localRoot = snapshot.localRoot.ifBlank { null },
            remoteRoot = snapshot.remoteRoot.ifBlank { null },
            dryRun = snapshot.dryRun,
            deleteRemoteOrphans = snapshot.deleteRemoteOrphans,
            conflictPolicy = snapshot.conflictPolicy,
        )

        appendTrace(
            "$label clicked appId=$appId localRoot=${request.localRoot ?: "(none)"} " +
                "remoteRoot=${request.remoteRoot ?: "(none)"} dryRun=${request.dryRun}",
        )
        transferPollingJob?.cancel()
        _state.update {
            it.copy(
                statusText = "$label in progress...",
                busy = true,
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

    private fun pollTransferTask(handle: Long, label: String) {
        transferPollingJob?.cancel()
        transferPollingJob = scope.launch {
            try {
                while (true) {
                    val snapshot = api.pollTransferTask(handle)
                    _state.update {
                        it.copy(
                            statusText = snapshot.message.ifBlank {
                                snapshot.phase.ifBlank { "$label in progress..." }
                            },
                            busy = snapshot.active,
                            transferTask = snapshot,
                            operationResult = snapshot.result ?: it.operationResult,
                        )
                    }
                    if (!snapshot.active) {
                        val result = snapshot.result
                        if (result != null) {
                            appendTrace(
                                "$label returned ok=${result.ok} transferred=${result.transferredCount} conflicts=${result.conflictCount}",
                            )
                        } else {
                            appendTrace(
                                "$label finished active=${snapshot.active} succeeded=${snapshot.succeeded} canceled=${snapshot.canceled}",
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

    private fun sanitizeCompact(value: String): String = value.filterNot { it.isWhitespace() }

    private fun sanitizeTrimmed(value: String): String = value.trim()
}
