package com.cauth.android.steam.auth

import com.cauth.android.CAuthClient
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class CAuthSteamAuthState(
    val accountName: String = "",
    val password: String = "",
    val guardCode: String = "",
    val steamId: String = "",
    val deviceName: String = "ComposeDemo",
    val loginPlatform: LoginPlatform = LoginPlatform.SteamClient,
    val moduleStatus: String = "idle",
    val statusText: String = "Ready",
    val busy: Boolean = false,
    val moduleTask: AuthTaskSnapshot? = null,
    val loginResult: LoginResultSnapshot? = null,
    val savedSession: SavedSessionSnapshot? = null,
    val savedAccounts: List<SavedAccountSnapshot> = emptyList(),
    val cmProbe: CmProbeSnapshot? = null,
    val cmLogon: CmLogonSnapshot? = null,
    val traceLines: List<String> = emptyList(),
    val nativeVersion: String = "",
)

class CAuthSteamAuthController(
    private val client: CAuthClient,
    private val scope: CoroutineScope,
) {
    private val api = CAuthSteamAuthApi(client)
    private val _state = MutableStateFlow(
        CAuthSteamAuthState(nativeVersion = client.version()),
    )
    val state: StateFlow<CAuthSteamAuthState> = _state
    private var idleResetJob: Job? = null

    fun setAccountName(value: String) = _state.update { it.copy(accountName = value.trim()) }
    fun setPassword(value: String) = _state.update { it.copy(password = value) }
    fun setGuardCode(value: String) = _state.update { it.copy(guardCode = value.filterNot { ch -> ch.isWhitespace() }) }
    fun setSteamId(value: String) = _state.update { it.copy(steamId = value.filter { ch -> ch.isDigit() }) }
    fun setDeviceName(value: String) = _state.update { it.copy(deviceName = value.trim()) }
    fun setLoginPlatform(value: LoginPlatform) = _state.update { it.copy(loginPlatform = value) }

    fun login() {
        val snapshot = _state.value
        appendTrace("Login clicked platform=${snapshot.loginPlatform.name} accountEmpty=${snapshot.accountName.isBlank()}")
        beginModuleTask(
            kind = "Login",
            moduleStatus = "authenticating",
            message = "Login in progress...",
        )
        scope.launch {
            appendTrace("Login coroutine started")
            runCatching {
                api.loginPassword(
                    LoginRequest(
                        accountName = snapshot.accountName,
                        password = snapshot.password,
                        steamGuardCode = snapshot.guardCode.ifBlank { null },
                        deviceName = snapshot.deviceName.ifBlank { null },
                        platform = snapshot.loginPlatform,
                    ),
                )
            }.onSuccess { result ->
                val message = "${result.status}: ${result.message}"
                finishModuleTask(
                    kind = "Login",
                    moduleStatus = result.moduleStatus.ifBlank { "succeeded" },
                    message = message,
                ) {
                    it.copy(
                        loginResult = result,
                        steamId = if (result.steamId != 0L) result.steamId.toString() else it.steamId,
                    )
                }
                appendTrace("Login returned status=${result.status} steamId=${result.steamId} message=${result.message}")
            }.onFailure { failure ->
                finishModuleTask(
                    kind = "Login",
                    moduleStatus = "failed",
                    message = failure.message ?: "Login failed",
                )
                appendTrace("Login failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
            }
        }
    }

    fun loadSavedSession() {
        val steamId = _state.value.steamId.toLongOrNull() ?: 0L
        appendTrace("Saved Session clicked steamId=$steamId")
        beginModuleTask(
            kind = "Saved Session",
            moduleStatus = "reading",
            message = "Loading saved session...",
        )
        scope.launch {
            appendTrace("Saved Session coroutine started")
            runCatching { api.getSavedSession(steamId) }
                .onSuccess { session ->
                    finishModuleTask(
                        kind = "Saved Session",
                        moduleStatus = "succeeded",
                        message = if (session.present) {
                            "Saved session for ${session.accountName ?: session.steamId}"
                        } else {
                            "No saved session"
                        },
                    ) {
                        it.copy(savedSession = session)
                    }
                    appendTrace("Saved Session returned present=${session.present} steamId=${session.steamId}")
                }
                .onFailure { failure ->
                    finishModuleTask(
                        kind = "Saved Session",
                        moduleStatus = "failed",
                        message = failure.message ?: "Session load failed",
                    )
                    appendTrace("Saved Session failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
                }
        }
    }

    fun loadSavedAccounts() {
        appendTrace("Saved Accounts clicked")
        beginModuleTask(
            kind = "Saved Accounts",
            moduleStatus = "reading",
            message = "Loading saved accounts...",
        )
        scope.launch {
            appendTrace("Saved Accounts coroutine started")
            runCatching { api.listSavedAccounts() }
                .onSuccess { accounts ->
                    finishModuleTask(
                        kind = "Saved Accounts",
                        moduleStatus = "succeeded",
                        message = "Saved accounts: ${accounts.size}",
                    ) {
                        it.copy(savedAccounts = accounts)
                    }
                    appendTrace("Saved Accounts returned count=${accounts.size}")
                }
                .onFailure { failure ->
                    finishModuleTask(
                        kind = "Saved Accounts",
                        moduleStatus = "failed",
                        message = failure.message ?: "Accounts load failed",
                    )
                    appendTrace("Saved Accounts failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
                }
        }
    }

    fun selectSavedAccount(steamId: Long) {
        appendTrace("Select Saved Account clicked steamId=$steamId")
        cancelIdleReset()
        _state.update {
            it.copy(
                steamId = steamId.toString(),
                moduleStatus = "idle",
                statusText = "SteamID selected",
                busy = false,
                moduleTask = null,
            )
        }
    }

    fun clearSavedSession() {
        val steamId = _state.value.steamId.toLongOrNull() ?: 0L
        appendTrace("Clear Session clicked steamId=$steamId")
        beginModuleTask(
            kind = "Clear Session",
            moduleStatus = "writing",
            message = "Clearing saved session...",
        )
        scope.launch {
            appendTrace("Clear Session coroutine started")
            runCatching { api.clearSavedSession(steamId) }
                .onSuccess {
                    finishModuleTask(
                        kind = "Clear Session",
                        moduleStatus = "succeeded",
                        message = "Saved session cleared",
                    ) {
                        it.copy(
                            savedSession = null,
                            savedAccounts = emptyList(),
                        )
                    }
                    appendTrace("Clear Session completed")
                }
                .onFailure { failure ->
                    finishModuleTask(
                        kind = "Clear Session",
                        moduleStatus = "failed",
                        message = failure.message ?: "Session clear failed",
                    )
                    appendTrace("Clear Session failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
                }
        }
    }

    fun probeCm() {
        appendTrace("CM Probe clicked")
        beginModuleTask(
            kind = "CM Probe",
            moduleStatus = "probing",
            message = "Probing CM...",
        )
        scope.launch {
            appendTrace("CM Probe coroutine started")
            runCatching { api.probeCm() }
                .onSuccess { result ->
                    finishModuleTask(
                        kind = "CM Probe",
                        moduleStatus = result.moduleStatus.ifBlank { if (result.ok) "succeeded" else "failed" },
                        message = result.status ?: if (result.ok) "CM connected" else "CM not connected",
                    ) {
                        it.copy(cmProbe = result)
                    }
                    appendTrace("CM Probe returned ok=${result.ok} endpoint=${result.endpoint ?: "(none)"} status=${result.status ?: "(none)"}")
                }
                .onFailure { failure ->
                    finishModuleTask(
                        kind = "CM Probe",
                        moduleStatus = "failed",
                        message = failure.message ?: "CM probe failed",
                    )
                    appendTrace("CM Probe failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
                }
        }
    }

    fun logonCm() {
        val steamId = _state.value.steamId.toLongOrNull() ?: 0L
        appendTrace("CM Logon clicked steamId=$steamId")
        beginModuleTask(
            kind = "CM Logon",
            moduleStatus = "logging_on",
            message = "CM logon in progress...",
        )
        scope.launch {
            appendTrace("CM Logon coroutine started")
            runCatching { api.logonCm(steamId) }
                .onSuccess { result ->
                    finishModuleTask(
                        kind = "CM Logon",
                        moduleStatus = result.moduleStatus.ifBlank { if (result.ok) "succeeded" else "failed" },
                        message = result.status ?: if (result.ok) "CM logon ok" else "CM logon failed",
                    ) {
                        it.copy(cmLogon = result)
                    }
                    appendTrace(
                        "CM Logon returned ok=${result.ok} endpoint=${result.endpoint ?: "(none)"} " +
                            "eresult=${result.eresult} extended=${result.eresultExtended}",
                    )
                }
                .onFailure { failure ->
                    finishModuleTask(
                        kind = "CM Logon",
                        moduleStatus = "failed",
                        message = failure.message ?: "CM logon failed",
                    )
                    appendTrace("CM Logon failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
                }
        }
    }

    private fun beginModuleTask(
        kind: String,
        moduleStatus: String,
        message: String,
    ) {
        cancelIdleReset()
        _state.update {
            it.copy(
                statusText = message,
                moduleStatus = moduleStatus,
                busy = true,
                moduleTask = AuthTaskSnapshot(
                    kind = kind,
                    active = true,
                    moduleStatus = moduleStatus,
                    message = message,
                ),
            )
        }
    }

    private fun finishModuleTask(
        kind: String,
        moduleStatus: String,
        message: String,
        transform: (CAuthSteamAuthState) -> CAuthSteamAuthState = { it },
    ) {
        cancelIdleReset()
        _state.update { current ->
            transform(
                current.copy(
                    statusText = message,
                    moduleStatus = moduleStatus,
                    busy = false,
                    moduleTask = AuthTaskSnapshot(
                        kind = kind,
                        active = false,
                        moduleStatus = moduleStatus,
                        message = message,
                    ),
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
        "authenticating",
        "reading",
        "writing",
        "probing",
        "logging_on",
        "polling",
        "canceling",
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
                )
            }
            idleResetJob = null
        }
    }

    private fun appendTrace(message: String) {
        _state.update { current ->
            val next = buildList {
                add(message)
                addAll(current.traceLines.take(13))
            }
            current.copy(traceLines = next)
        }
    }

    private companion object {
        const val IDLE_RESET_DELAY_MS = 2500L
    }
}
