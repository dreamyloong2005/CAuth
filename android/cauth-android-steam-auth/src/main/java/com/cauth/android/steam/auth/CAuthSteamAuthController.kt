package com.cauth.android.steam.auth

import com.cauth.android.CAuthClient
import com.cauth.android.CAuthRouteProbeEntrySnapshot
import com.cauth.android.CAuthRouteSelection
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
    val routeEndpoint: String = "",
    val routeProtocol: String = "",
    val routeRole: String = "",
    val moduleStatus: String = "idle",
    val statusText: String = "Ready",
    val busy: Boolean = false,
    val moduleTask: AuthTaskSnapshot? = null,
    val loginResult: LoginResultSnapshot? = null,
    val savedSession: SavedSessionSnapshot? = null,
    val savedAccounts: List<SavedAccountSnapshot> = emptyList(),
    val cmProbe: CmProbeSnapshot? = null,
    val cmLogon: CmLogonSnapshot? = null,
    val cmRoutes: CmRouteProbeSnapshot? = null,
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
    fun setRouteEndpoint(value: String) = _state.update { it.copy(routeEndpoint = value.trim()) }
    fun setRouteProtocol(value: String) = _state.update { it.copy(routeProtocol = value.trim()) }
    fun setRouteRole(value: String) = _state.update { it.copy(routeRole = value.trim()) }

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
                        routeSelection = snapshot.routeSelection(),
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
        appendTrace("CM Probe clicked route=${describeRouteSelection(_state.value.routeSelection())}")
        beginModuleTask(
            kind = "CM Probe",
            moduleStatus = "probing",
            message = "Probing CM...",
        )
        scope.launch {
            appendTrace("CM Probe coroutine started")
            runCatching { api.probeCm(routeSelection = _state.value.routeSelection()) }
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
        appendTrace("CM Logon clicked steamId=$steamId route=${describeRouteSelection(_state.value.routeSelection())}")
        beginModuleTask(
            kind = "CM Logon",
            moduleStatus = "logging_on",
            message = "CM logon in progress...",
        )
        scope.launch {
            appendTrace("CM Logon coroutine started")
            runCatching { api.logonCm(steamId, routeSelection = _state.value.routeSelection()) }
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

    fun probeCmRoutes() {
        val maxCount = 20
        appendTrace("CM Routes clicked maxCount=$maxCount")
        beginModuleTask(
            kind = "CM Routes",
            moduleStatus = "probing",
            message = "Fetching CM routes...",
        )
        scope.launch {
            appendTrace("CM Routes coroutine started")
            runCatching { api.probeCmRoutes(maxCount = maxCount) }
                .onSuccess { result ->
                    finishModuleTask(
                        kind = "CM Routes",
                        moduleStatus = result.moduleStatus.ifBlank { if (result.ok) "succeeded" else "failed" },
                        message = result.message.ifBlank {
                            if (result.ok) "Fetched ${result.routes.size} route(s)" else "CM route probe failed"
                        },
                    ) {
                        it.copy(cmRoutes = result)
                    }
                    appendTrace(
                        "CM Routes returned ok=${result.ok} backend=${result.backend.ifBlank { "(none)" }} routes=${result.routes.size}",
                    )
                }
                .onFailure { failure ->
                    finishModuleTask(
                        kind = "CM Routes",
                        moduleStatus = "failed",
                        message = failure.message ?: "CM route probe failed",
                    )
                    appendTrace("CM Routes failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
                }
        }
    }

    fun useCmRoute(route: CAuthRouteProbeEntrySnapshot) {
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
            )
        }
        appendTrace(
            "Route selected endpoint=${route.endpoint} protocol=${route.protocol.ifBlank { "(none)" }} role=${route.role.ifBlank { "(none)" }}",
        )
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

    private fun CAuthSteamAuthState.routeSelection(): CAuthRouteSelection? {
        val selection = CAuthRouteSelection(
            endpoint = routeEndpoint.ifBlank { null },
            protocol = routeProtocol.ifBlank { null },
            role = routeRole.ifBlank { null },
        )
        return selection.takeUnless { it.isEmpty() }
    }

    private fun describeRouteSelection(routeSelection: CAuthRouteSelection?): String = when {
        routeSelection == null -> "auto"
        else -> buildString {
            append(routeSelection.endpoint ?: "*")
            if (!routeSelection.protocol.isNullOrBlank()) {
                append(" protocol=${routeSelection.protocol}")
            }
            if (!routeSelection.role.isNullOrBlank()) {
                append(" role=${routeSelection.role}")
            }
        }
    }

    private companion object {
        const val IDLE_RESET_DELAY_MS = 2500L
    }
}
