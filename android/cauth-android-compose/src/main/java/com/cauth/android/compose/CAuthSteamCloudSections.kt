package com.cauth.android.compose

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
import androidx.compose.material3.Checkbox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.cauth.android.steam.cloud.CAuthSteamCloudController
import com.cauth.android.steam.cloud.CAuthSteamCloudState
import com.cauth.android.steam.cloud.SteamCloudBackend
import com.cauth.android.steam.cloud.SteamCloudConflictPolicy
import com.cauth.android.steam.cloud.SteamCloudRouteTask

@Composable
fun CAuthSteamCloudRequestSection(
    state: CAuthSteamCloudState,
    controller: CAuthSteamCloudController,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        OutlinedTextField(
            value = state.appIdText,
            onValueChange = controller::setAppIdText,
            label = { Text("App ID") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.steamIdText,
            onValueChange = controller::setSteamIdText,
            label = { Text("SteamID for saved auth") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.localRoot,
            onValueChange = controller::setLocalRoot,
            label = { Text("Local root") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.remoteRoot,
            onValueChange = controller::setRemoteRoot,
            label = { Text("Remote root") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.accessToken,
            onValueChange = controller::setAccessToken,
            label = { Text("Access token (optional)") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.countText,
            onValueChange = controller::setCountText,
            label = { Text("Count") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.startIndexText,
            onValueChange = controller::setStartIndexText,
            label = { Text("Start index") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        Column(
            modifier = Modifier.selectableGroup(),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Text("Backend", style = MaterialTheme.typography.labelLarge)
            SteamCloudBackend.entries.forEach { backend ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .selectable(
                            selected = state.backend == backend,
                            onClick = { if (!state.busy) controller.setBackend(backend) },
                        ),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    RadioButton(
                        selected = state.backend == backend,
                        onClick = { if (!state.busy) controller.setBackend(backend) },
                        enabled = !state.busy,
                    )
                    Text(backend.name)
                }
            }
        }
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
    }
}

@Composable
fun CAuthSteamCloudOptionSection(
    state: CAuthSteamCloudState,
    controller: CAuthSteamCloudController,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Checkbox(
                    checked = state.dryRun,
                    onCheckedChange = controller::setDryRun,
                    enabled = !state.busy,
                )
                Text("Dry run")
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Checkbox(
                    checked = state.deleteRemoteOrphans,
                    onCheckedChange = controller::setDeleteRemoteOrphans,
                    enabled = !state.busy,
                )
                Text("Delete remote orphans")
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Checkbox(
                    checked = state.verifyIncludeExtraLocal,
                    onCheckedChange = controller::setVerifyIncludeExtraLocal,
                    enabled = !state.busy,
                )
                Text("Include extra local")
            }
        }

        Column(
            modifier = Modifier.selectableGroup(),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Text("Conflict policy", style = MaterialTheme.typography.labelLarge)
            SteamCloudConflictPolicy.entries.forEach { policy ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .selectable(
                            selected = state.conflictPolicy == policy,
                            onClick = { if (!state.busy) controller.setConflictPolicy(policy) },
                        ),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    RadioButton(
                        selected = state.conflictPolicy == policy,
                        onClick = { if (!state.busy) controller.setConflictPolicy(policy) },
                        enabled = !state.busy,
                    )
                    Text(policy.name)
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CAuthSteamCloudActionSection(
    state: CAuthSteamCloudState,
    controller: CAuthSteamCloudController,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Button(enabled = !state.busy, onClick = { controller.probeRoutes(SteamCloudRouteTask.List) }) {
                Text("Probe List Routes")
            }
            Button(enabled = !state.busy, onClick = { controller.probeRoutes(SteamCloudRouteTask.Verify) }) {
                Text("Probe Verify Routes")
            }
            Button(enabled = !state.busy, onClick = { controller.probeRoutes(SteamCloudRouteTask.Pull) }) {
                Text("Probe Pull Routes")
            }
            Button(enabled = !state.busy, onClick = { controller.probeRoutes(SteamCloudRouteTask.Push) }) {
                Text("Probe Push Routes")
            }
            Button(enabled = !state.busy, onClick = controller::listRemoteFiles) { Text("List") }
            Button(enabled = !state.busy, onClick = controller::listRemoteFilesViaWebPage) {
                Text("Web Page List (Diagnostic)")
            }
            Button(enabled = !state.busy, onClick = controller::verifyLocalFiles) { Text("Verify") }
            Button(enabled = !state.busy, onClick = controller::pull) { Text("Pull") }
            Button(enabled = !state.busy, onClick = controller::push) { Text("Push") }
        }
        Text(
            text = "Web Page List only scrapes the store page for diagnostics. It can help inspect what the web session can see, but it does not mean Steam Cloud web pull or push is supported.",
            style = MaterialTheme.typography.bodySmall,
        )
    }
}

@Composable
fun CAuthSteamCloudResultsSection(
    state: CAuthSteamCloudState,
    controller: CAuthSteamCloudController,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        state.routeProbe?.let { snapshot ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    "Routes (${snapshot.routes.size}) backend=${snapshot.backend.ifBlank { "(none)" }}",
                    style = MaterialTheme.typography.labelLarge,
                )
                if (snapshot.message.isNotBlank()) {
                    SelectableCloudResultText(snapshot.message)
                }
                snapshot.routes.take(8).forEach { route ->
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        SelectableCloudResultText(
                            "${route.endpoint} protocol=${route.protocol.ifBlank { "(none)" }} role=${route.role.ifBlank { "(none)" }} latency=${route.latencyLabel}",
                        )
                        Button(enabled = !state.busy, onClick = { controller.useRoute(route) }) { Text("Use") }
                    }
                }
            }
        }
        state.fileList?.let { snapshot ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    "Files (${snapshot.files.size}/${snapshot.totalFiles}) eresult=${snapshot.eresult}",
                    style = MaterialTheme.typography.labelLarge,
                )
                if (snapshot.message.isNotBlank()) {
                    SelectableCloudResultText(snapshot.message)
                }
                snapshot.files.take(8).forEach { entry ->
                    SelectableCloudResultText(
                        buildString {
                            append(entry.filename)
                            append(" size=${entry.fileSize} ts=${entry.timestamp}")
                            if (entry.url.isNotBlank()) {
                                append(" url=${entry.url}")
                            }
                        },
                    )
                }
            }
        }

        state.operationResult?.let { result ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    "${result.direction.name} transferred=${result.transferredCount} skipped=${result.skippedCount}",
                    style = MaterialTheme.typography.labelLarge,
                )
                SelectableCloudResultText(
                    "conflicts=${result.conflictCount} deleted=${result.deletedCount} bytes=${result.transferredBytes}",
                )
                if (result.direction == com.cauth.android.steam.cloud.SteamCloudDirection.Push ||
                    result.resumable ||
                    result.resumed
                ) {
                    SelectableCloudResultText(result.resumeSummary)
                }
                SelectableCloudResultText(result.message)
            }
        }

        state.verifyResult?.let { result ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    "Verify clean=${result.clean} checked=${result.checkedCount}",
                    style = MaterialTheme.typography.labelLarge,
                )
                SelectableCloudResultText(
                    "ok=${result.okCount} missing=${result.missingCount} mismatched=${result.mismatchedCount} sizeOnly=${result.sizeOnlyCount}",
                )
                SelectableCloudResultText(
                    "filtered=${result.filteredOutCount} extraLocal=${result.extraLocalCount} total=${result.totalCount}",
                )
                SelectableCloudResultText(result.message)
                result.entries.take(6).forEach { entry ->
                    SelectableCloudResultText(
                        "${entry.statusLabel} remote=${entry.remoteFilename.ifBlank { "(none)" }} local=${entry.localPath.ifBlank { "(none)" }} remoteSize=${entry.remoteSize} localSize=${entry.localSize} reason=${entry.reason.ifBlank { "(none)" }}",
                    )
                }
            }
        }
    }
}

@Composable
private fun SelectableCloudResultText(text: String) {
    SelectionContainer {
        Text(
            text,
            style = MaterialTheme.typography.bodySmall,
        )
    }
}
