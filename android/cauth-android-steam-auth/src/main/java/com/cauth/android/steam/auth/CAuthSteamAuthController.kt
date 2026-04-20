package com.cauth.android.steam.auth

import com.cauth.android.CAuthClient
import kotlinx.coroutines.CoroutineScope
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
    val statusText: String = "Ready",
    val busy: Boolean = false,
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

    fun setAccountName(value: String) = _state.update { it.copy(accountName = value.trim()) }
    fun setPassword(value: String) = _state.update { it.copy(password = value) }
    fun setGuardCode(value: String) = _state.update { it.copy(guardCode = value.filterNot { ch -> ch.isWhitespace() }) }
    fun setSteamId(value: String) = _state.update { it.copy(steamId = value.filter { ch -> ch.isDigit() }) }
    fun setDeviceName(value: String) = _state.update { it.copy(deviceName = value.trim()) }
    fun setLoginPlatform(value: LoginPlatform) = _state.update { it.copy(loginPlatform = value) }

    fun login() {
        val snapshot = _state.value
        appendTrace("Login clicked platform=${snapshot.loginPlatform.name} accountEmpty=${snapshot.accountName.isBlank()}")
        _state.update { it.copy(statusText = "Login in progress...", busy = true) }
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
                _state.update {
                    it.copy(
                        statusText = "${result.status}: ${result.message}",
                        busy = false,
                        loginResult = result,
                        steamId = if (result.steamId != 0L) result.steamId.toString() else it.steamId,
                    )
                }
                appendTrace("Login returned status=${result.status} steamId=${result.steamId} message=${result.message}")
            }.onFailure { failure ->
                _state.update {
                    it.copy(
                        statusText = failure.message ?: "Login failed",
                        busy = false,
                    )
                }
                appendTrace("Login failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
            }
        }
    }

    fun loadSavedSession() {
        val steamId = _state.value.steamId.toLongOrNull() ?: 0L
        appendTrace("Saved Session clicked steamId=$steamId")
        _state.update { it.copy(statusText = "Loading saved session...", busy = true) }
        scope.launch {
            appendTrace("Saved Session coroutine started")
            runCatching { api.getSavedSession(steamId) }
                .onSuccess { session ->
                    _state.update {
                        it.copy(
                            statusText = if (session.present) {
                                "Saved session for ${session.accountName ?: session.steamId}"
                            } else {
                                "No saved session"
                            },
                            busy = false,
                            savedSession = session,
                        )
                    }
                    appendTrace("Saved Session returned present=${session.present} steamId=${session.steamId}")
                }
                .onFailure { failure ->
                    _state.update {
                        it.copy(
                            statusText = failure.message ?: "Session load failed",
                            busy = false,
                        )
                    }
                    appendTrace("Saved Session failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
                }
        }
    }

    fun loadSavedAccounts() {
        appendTrace("Saved Accounts clicked")
        _state.update { it.copy(statusText = "Loading saved accounts...", busy = true) }
        scope.launch {
            appendTrace("Saved Accounts coroutine started")
            runCatching { api.listSavedAccounts() }
                .onSuccess { accounts ->
                    _state.update {
                        it.copy(
                            statusText = "Saved accounts: ${accounts.size}",
                            busy = false,
                            savedAccounts = accounts,
                        )
                    }
                    appendTrace("Saved Accounts returned count=${accounts.size}")
                }
                .onFailure { failure ->
                    _state.update {
                        it.copy(
                            statusText = failure.message ?: "Accounts load failed",
                            busy = false,
                        )
                    }
                    appendTrace("Saved Accounts failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
                }
        }
    }

    fun selectSavedAccount(steamId: Long) {
        appendTrace("Select Saved Account clicked steamId=$steamId")
        _state.update { it.copy(steamId = steamId.toString(), statusText = "SteamID selected") }
    }

    fun clearSavedSession() {
        val steamId = _state.value.steamId.toLongOrNull() ?: 0L
        appendTrace("Clear Session clicked steamId=$steamId")
        _state.update { it.copy(statusText = "Clearing saved session...", busy = true) }
        scope.launch {
            appendTrace("Clear Session coroutine started")
            runCatching { api.clearSavedSession(steamId) }
                .onSuccess {
                    _state.update {
                        it.copy(
                            statusText = "Saved session cleared",
                            busy = false,
                            savedSession = null,
                            savedAccounts = emptyList(),
                        )
                    }
                    appendTrace("Clear Session completed")
                }
                .onFailure { failure ->
                    _state.update {
                        it.copy(
                            statusText = failure.message ?: "Session clear failed",
                            busy = false,
                        )
                    }
                    appendTrace("Clear Session failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
                }
        }
    }

    fun probeCm() {
        appendTrace("CM Probe clicked")
        _state.update { it.copy(statusText = "Probing CM...", busy = true) }
        scope.launch {
            appendTrace("CM Probe coroutine started")
            runCatching { api.probeCm() }
                .onSuccess { result ->
                    _state.update {
                        it.copy(
                            statusText = result.status ?: if (result.ok) "CM connected" else "CM not connected",
                            busy = false,
                            cmProbe = result,
                        )
                    }
                    appendTrace("CM Probe returned ok=${result.ok} endpoint=${result.endpoint ?: "(none)"} status=${result.status ?: "(none)"}")
                }
                .onFailure { failure ->
                    _state.update {
                        it.copy(
                            statusText = failure.message ?: "CM probe failed",
                            busy = false,
                        )
                    }
                    appendTrace("CM Probe failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
                }
        }
    }

    fun logonCm() {
        val steamId = _state.value.steamId.toLongOrNull() ?: 0L
        appendTrace("CM Logon clicked steamId=$steamId")
        _state.update { it.copy(statusText = "CM logon in progress...", busy = true) }
        scope.launch {
            appendTrace("CM Logon coroutine started")
            runCatching { api.logonCm(steamId) }
                .onSuccess { result ->
                    _state.update {
                        it.copy(
                            statusText = result.status ?: if (result.ok) "CM logon ok" else "CM logon failed",
                            busy = false,
                            cmLogon = result,
                        )
                    }
                    appendTrace(
                        "CM Logon returned ok=${result.ok} endpoint=${result.endpoint ?: "(none)"} " +
                            "eresult=${result.eresult} extended=${result.eresultExtended}",
                    )
                }
                .onFailure { failure ->
                    _state.update {
                        it.copy(
                            statusText = failure.message ?: "CM logon failed",
                            busy = false,
                        )
                    }
                    appendTrace("CM Logon failed: ${failure::class.simpleName}: ${failure.message ?: "(no message)"}")
                }
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
}
