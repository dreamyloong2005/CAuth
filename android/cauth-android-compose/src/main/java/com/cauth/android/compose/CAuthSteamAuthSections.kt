package com.cauth.android.compose

import android.content.ClipData
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.toClipEntry
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.cauth.android.steam.auth.AuthTaskSnapshot
import com.cauth.android.steam.auth.CAuthSteamAuthController
import com.cauth.android.steam.auth.CAuthSteamAuthState
import com.cauth.android.steam.auth.LoginPlatform
import kotlinx.coroutines.launch

@Composable
fun CAuthSteamAuthHeader(
    nativeVersion: String,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text("CAuth Android Compose", style = MaterialTheme.typography.titleLarge)
        Text("Native version: $nativeVersion", style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
fun CAuthSteamAuthForm(
    state: CAuthSteamAuthState,
    controller: CAuthSteamAuthController,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        OutlinedTextField(
            value = state.accountName,
            onValueChange = controller::setAccountName,
            label = { Text("Steam account") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
        )
        OutlinedTextField(
            value = state.password,
            onValueChange = controller::setPassword,
            label = { Text("Password") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
        )
        OutlinedTextField(
            value = state.guardCode,
            onValueChange = controller::setGuardCode,
            label = { Text("Guard code (optional)") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
        )
        OutlinedTextField(
            value = state.steamId,
            onValueChange = controller::setSteamId,
            label = { Text("SteamID for saved operations") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
        )
        OutlinedTextField(
            value = state.deviceName,
            onValueChange = controller::setDeviceName,
            label = { Text("Device name") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
        )
    }
}

@Composable
fun CAuthSteamAuthPlatformSelector(
    selectedPlatform: LoginPlatform,
    onPlatformSelected: (LoginPlatform) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.selectableGroup(),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text("Login platform", style = MaterialTheme.typography.labelLarge)
        listOf(
            LoginPlatform.SteamClient to "Steam Client",
            LoginPlatform.WebBrowser to "Web Browser",
            LoginPlatform.MobileApp to "Mobile App",
        ).forEach { (platform, label) ->
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .selectable(
                        selected = selectedPlatform == platform,
                        onClick = { onPlatformSelected(platform) },
                    ),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                RadioButton(
                    selected = selectedPlatform == platform,
                    onClick = { onPlatformSelected(platform) },
                )
                Text(label)
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CAuthSteamAuthRouteSection(
    state: CAuthSteamAuthState,
    controller: CAuthSteamAuthController,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("CM Route", style = MaterialTheme.typography.labelLarge)
        OutlinedTextField(
            value = state.routeEndpoint,
            onValueChange = controller::setRouteEndpoint,
            label = { Text("Route endpoint (optional)") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.routeProtocol,
            onValueChange = controller::setRouteProtocol,
            label = { Text("Route protocol (optional)") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.routeRole,
            onValueChange = controller::setRouteRole,
            label = { Text("Route role (optional)") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Button(enabled = !state.busy, onClick = controller::probeCmRoutes) {
                Text("Probe Routes")
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CAuthSteamAuthActionButtons(
    state: CAuthSteamAuthState,
    controller: CAuthSteamAuthController,
    modifier: Modifier = Modifier,
) {
    FlowRow(
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        maxItemsInEachRow = 2,
    ) {
        Button(
            onClick = controller::login,
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
        ) {
            Text("Login")
        }
        if (state.busy) {
            Button(
                onClick = controller::cancelLogin,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Cancel Login")
            }
        }

        Button(
            onClick = controller::loadSavedSession,
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
        ) {
            Text("Saved Session")
        }

        Button(
            onClick = controller::loadSavedAccounts,
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
        ) {
            Text("Saved Accounts")
        }

        Button(
            onClick = controller::clearSavedSession,
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
        ) {
            Text("Clear Session")
        }

        Button(
            onClick = controller::probeCm,
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
        ) {
            Text("CM Probe")
        }

        Button(
            onClick = controller::logonCm,
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
        ) {
            Text("CM Logon")
        }
    }
}

@Composable
fun CAuthSteamAuthStatus(
    statusText: String,
    moduleStatus: String,
    moduleTask: AuthTaskSnapshot?,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            text = statusText,
            style = MaterialTheme.typography.bodyMedium,
        )
        Text(
            text = "Module status: ${moduleStatus.ifBlank { "idle" }}",
            style = MaterialTheme.typography.bodySmall,
        )
        moduleTask?.let { task ->
            Text(
                text = "Task: ${task.kind} [${task.moduleStatus}] ${task.message}",
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
}

@Composable
fun CAuthSteamAuthTrace(
    traceLines: List<String>,
    modifier: Modifier = Modifier,
) {
    if (traceLines.isEmpty()) {
        return
    }
    val clipboard = LocalClipboard.current
    val scope = rememberCoroutineScope()
    val traceText = traceLines.joinToString(separator = "\n")
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text("Trace", style = MaterialTheme.typography.labelLarge)
            Button(
                onClick = {
                    scope.launch {
                        clipboard.setClipEntry(
                            ClipData.newPlainText("Auth Trace", traceText).toClipEntry(),
                        )
                    }
                },
            ) {
                Text("Copy Trace")
            }
        }
        SelectionContainer {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                traceLines.forEach { line ->
                    Text(line, style = MaterialTheme.typography.bodySmall)
                }
            }
        }
    }
}

@Composable
fun CAuthSteamAuthResults(
    state: CAuthSteamAuthState,
    controller: CAuthSteamAuthController,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        state.loginResult?.let {
            Text(
                "Login: ${it.status} steamId=${it.steamId} account=${it.accountName ?: "(none)"}",
                style = MaterialTheme.typography.bodySmall,
            )
        }

        state.savedSession?.let {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    "Saved: present=${it.present} account=${it.accountName ?: "(none)"} steamId=${it.steamId}",
                    style = MaterialTheme.typography.bodySmall,
                )
                Text(
                    "Saved tokens: refresh=${it.hasRefreshToken} access=${it.hasAccessToken} createdAt=${it.createdAtUnixSeconds}",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }

        if (state.savedAccounts.isNotEmpty()) {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Saved accounts", style = MaterialTheme.typography.labelLarge)
                state.savedAccounts.forEach { account ->
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        Text(
                            "${account.accountName ?: "(none)"} steamId=${account.steamId}",
                            style = MaterialTheme.typography.bodySmall,
                        )
                        Button(onClick = { controller.selectSavedAccount(account.steamId) }) {
                            Text("Select")
                        }
                    }
                }
            }
        }

        state.cmProbe?.let {
            Text(
                "CM: ok=${it.ok} endpoint=${it.endpoint ?: "(none)"}",
                style = MaterialTheme.typography.bodySmall,
            )
        }

        state.cmRoutes?.let { snapshot ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    "CM Routes (${snapshot.routes.size}) backend=${snapshot.backend.ifBlank { "(none)" }}",
                    style = MaterialTheme.typography.labelLarge,
                )
                if (snapshot.message.isNotBlank()) {
                    Text(snapshot.message, style = MaterialTheme.typography.bodySmall)
                }
                snapshot.routes.take(8).forEach { route ->
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Text(
                            "${route.endpoint} protocol=${route.protocol.ifBlank { "(none)" }} role=${route.role.ifBlank { "(none)" }} latency=${route.latencyLabel}",
                            style = MaterialTheme.typography.bodySmall,
                        )
                        Button(onClick = { controller.useCmRoute(route) }) {
                            Text("Use")
                        }
                    }
                }
            }
        }

        state.cmLogon?.let {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    "CM logon: ok=${it.ok} endpoint=${it.endpoint ?: "(none)"} steamId=${it.steamId}",
                    style = MaterialTheme.typography.bodySmall,
                )
                Text(
                    "CM logon detail: eresult=${it.eresult} extended=${it.eresultExtended} heartbeat=${it.heartbeatSeconds}s",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }
    }
}
