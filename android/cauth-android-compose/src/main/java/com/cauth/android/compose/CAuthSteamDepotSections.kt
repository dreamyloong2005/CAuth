package com.cauth.android.compose

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.cauth.android.steam.depot.CAuthSteamDepotController
import com.cauth.android.steam.depot.CAuthSteamDepotState
import com.cauth.android.steam.depot.formatUnsignedDecimal

@Composable
fun CAuthSteamDepotQuerySection(
    state: CAuthSteamDepotState,
    controller: CAuthSteamDepotController,
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
            value = state.branch,
            onValueChange = controller::setBranch,
            label = { Text("Branch") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.maxCountText,
            onValueChange = controller::setMaxCountText,
            label = { Text("Max count") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        CAuthSteamDepotQueryActions(
            busy = state.busy,
            onFetchBranches = controller::fetchBranches,
            onFetchManifests = controller::fetchManifests,
            onFetchPreflight = controller::fetchPreflight,
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CAuthSteamDepotQueryActions(
    busy: Boolean,
    onFetchBranches: () -> Unit,
    onFetchManifests: () -> Unit,
    onFetchPreflight: () -> Unit,
    modifier: Modifier = Modifier,
) {
    FlowRow(
        modifier = modifier,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Button(enabled = !busy, onClick = onFetchBranches) { Text("Branches") }
        Button(enabled = !busy, onClick = onFetchManifests) { Text("Manifests") }
        Button(enabled = !busy, onClick = onFetchPreflight) { Text("Preflight") }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CAuthSteamDepotDownloadSetupSection(
    state: CAuthSteamDepotState,
    controller: CAuthSteamDepotController,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Download Setup", style = MaterialTheme.typography.labelLarge)
        OutlinedTextField(
            value = state.depotIdText,
            onValueChange = controller::setDepotIdText,
            label = { Text("Depot ID") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.manifestGidText,
            onValueChange = controller::setManifestGidText,
            label = { Text("Manifest GID") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.requestCodeText,
            onValueChange = controller::setRequestCodeText,
            label = { Text("Request code") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.branchPasswordHash,
            onValueChange = controller::setBranchPasswordHash,
            label = { Text("Branch password hash (optional)") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.outputPath,
            onValueChange = controller::setOutputPath,
            label = { Text("Manifest output path") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Button(enabled = !state.busy, onClick = controller::fetchDepotKey) { Text("Depot Key") }
            Button(enabled = !state.busy, onClick = controller::fetchManifestRequestCode) { Text("Request Code") }
            Button(enabled = !state.busy, onClick = controller::downloadManifest) { Text("Download Manifest") }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CAuthSteamDepotManifestInspectSection(
    state: CAuthSteamDepotState,
    controller: CAuthSteamDepotController,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Manifest Inspect", style = MaterialTheme.typography.labelLarge)
        OutlinedTextField(
            value = state.manifestPath,
            onValueChange = controller::setManifestPath,
            label = { Text("Manifest path") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.depotKeyHex,
            onValueChange = controller::setDepotKeyHex,
            label = { Text("Depot key hex (optional)") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.filterText,
            onValueChange = controller::setFilterText,
            label = { Text("File filter (optional)") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.fileLimitText,
            onValueChange = controller::setFileLimitText,
            label = { Text("File list limit") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Button(enabled = !state.busy, onClick = controller::loadManifestInfo) { Text("Manifest Info") }
            Button(enabled = !state.busy, onClick = controller::listManifestFiles) { Text("File List") }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CAuthSteamDepotContentSection(
    state: CAuthSteamDepotState,
    controller: CAuthSteamDepotController,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Content Download", style = MaterialTheme.typography.labelLarge)
        OutlinedTextField(
            value = state.selectedFilePath,
            onValueChange = controller::setSelectedFilePath,
            label = { Text("Manifest file path") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.chunkIndexText,
            onValueChange = controller::setChunkIndexText,
            label = { Text("Chunk index") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.chunkOutputPath,
            onValueChange = controller::setChunkOutputPath,
            label = { Text("Chunk output path") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.fileOutputPath,
            onValueChange = controller::setFileOutputPath,
            label = { Text("File output path") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.allFilesOutputRoot,
            onValueChange = controller::setAllFilesOutputRoot,
            label = { Text("All files output root") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        OutlinedTextField(
            value = state.verifyLocalRoot,
            onValueChange = controller::setVerifyLocalRoot,
            label = { Text("Verify local root") },
            modifier = Modifier.fillMaxWidth(),
            enabled = !state.busy,
            singleLine = true,
        )
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Button(
                enabled = !state.busy,
                onClick = { controller.setProcessChunk(!state.processChunk) },
            ) { Text(if (state.processChunk) "Process Chunk: On" else "Process Chunk: Off") }
            Button(enabled = !state.busy, onClick = controller::downloadChunk) { Text("Download Chunk") }
            Button(enabled = !state.busy, onClick = controller::downloadFile) { Text("Download File") }
            Button(enabled = !state.busy, onClick = controller::downloadAllFiles) { Text("Download All Files") }
            Button(enabled = !state.busy, onClick = controller::verifyLocalFiles) { Text("Verify Local Files") }
        }
    }
}

@Composable
fun CAuthSteamDepotResultsSection(
    state: CAuthSteamDepotState,
    controller: CAuthSteamDepotController,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        state.branches?.let { snapshot ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Branches (${snapshot.branches.size})", style = MaterialTheme.typography.labelLarge)
                snapshot.branches.take(8).forEach { entry ->
                    SelectableResultText("${entry.name} build=${entry.buildId} protected=${entry.passwordRequired}")
                }
            }
        }

        state.manifests?.let { snapshot ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Manifests (${snapshot.manifests.size})", style = MaterialTheme.typography.labelLarge)
                snapshot.manifests.take(8).forEach { entry ->
                    SelectableActionResult(
                        text = buildString {
                            append("depot=${entry.depotId} platform=${entry.platformLabel} gid=${entry.manifestGid} encrypted=${entry.encrypted}")
                            if (entry.depotFromApp.isNotBlank()) {
                                append(" fromApp=${entry.depotFromApp}")
                            }
                        },
                        enabled = !state.busy,
                        onAction = { controller.useManifestSelection(entry.depotId, entry.manifestGid) },
                    )
                }
            }
        }

        state.preflight?.let { snapshot ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Preflight build=${snapshot.buildId}", style = MaterialTheme.typography.labelLarge)
                snapshot.depots.take(8).forEach { entry ->
                    SelectableActionResult(
                        text = buildString {
                            append("depot=${entry.depotId} platform=${entry.platformLabel} access=${entry.accessStatus} key=${entry.keyAvailable} gid=${entry.manifestGid}")
                            if (entry.depotFromApp.isNotBlank()) {
                                append(" fromApp=${entry.depotFromApp}")
                            }
                        },
                        enabled = !state.busy,
                        onAction = { controller.useManifestSelection(entry.depotId, entry.manifestGid) },
                    )
                }
            }
        }

        state.depotKey?.let { snapshot ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Depot Key", style = MaterialTheme.typography.labelLarge)
                SelectableResultText(
                    "present=${snapshot.present} depot=${snapshot.depotId} eresult=${snapshot.eresult}",
                )
                if (snapshot.keyHex.isNotBlank()) {
                    SelectableResultText(snapshot.keyHex)
                }
            }
        }

        state.manifestRequestCode?.let { snapshot ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Manifest Request Code", style = MaterialTheme.typography.labelLarge)
                SelectableResultText(
                    "present=${snapshot.present} requestCode=${formatUnsignedDecimal(snapshot.requestCode)}",
                )
            }
        }

        state.manifestInfo?.let { snapshot ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Manifest Info", style = MaterialTheme.typography.labelLarge)
                SelectableResultText(
                    "depot=${snapshot.depotId} gid=${snapshot.manifestGid} files=${snapshot.fileCount} chunks=${snapshot.chunkCount}",
                )
                SelectableResultText(
                    "encrypted=${snapshot.filenamesEncrypted} uniqueChunks=${snapshot.uniqueChunks}",
                )
            }
        }

        state.manifestFiles?.let { snapshot ->
            val pageSize = 5
            val totalItems = snapshot.files.size
            val totalPages = maxOf(1, (totalItems + pageSize - 1) / pageSize)
            var manifestFilePage by remember(
                snapshot.printedCount,
                snapshot.matchedCount,
                snapshot.totalCount,
                totalItems,
            ) {
                mutableStateOf(0)
            }
            val currentPage = manifestFilePage.coerceIn(0, totalPages - 1)
            val pageStart = currentPage * pageSize
            val pageEndExclusive = minOf(pageStart + pageSize, totalItems)
            val pageItems = if (pageStart < pageEndExclusive) {
                snapshot.files.asList().subList(pageStart, pageEndExclusive)
            } else {
                emptyList()
            }
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    "Manifest Files (${snapshot.printedCount}/${snapshot.matchedCount}/${snapshot.totalCount}) page ${currentPage + 1}/$totalPages",
                    style = MaterialTheme.typography.labelLarge,
                )
                if (totalPages > 1) {
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Button(
                            enabled = !state.busy && currentPage > 0,
                            onClick = { manifestFilePage = (currentPage - 1).coerceAtLeast(0) },
                        ) { Text("Prev") }
                        Button(
                            enabled = !state.busy && currentPage + 1 < totalPages,
                            onClick = { manifestFilePage = (currentPage + 1).coerceAtMost(totalPages - 1) },
                        ) { Text("Next") }
                        Text(
                            "Items ${pageStart + 1}-${pageEndExclusive} / $totalItems",
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                }
                pageItems.forEach { entry ->
                    SelectableActionResult(
                        text = "${entry.filename} size=${entry.size} chunks=${entry.chunkCount}",
                        enabled = !state.busy,
                        onAction = { controller.useManifestFile(entry.filename) },
                    )
                }
            }
        }

        state.localVerify?.let { snapshot ->
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Local Verify", style = MaterialTheme.typography.labelLarge)
                SelectableResultText(
                    "present=${snapshot.present} clean=${snapshot.clean} checked=${snapshot.checkedCount} ok=${snapshot.okCount}",
                )
                SelectableResultText(
                    "missing=${snapshot.missingCount} mismatched=${snapshot.mismatchedCount} sizeOnly=${snapshot.sizeOnlyCount} filteredOut=${snapshot.filteredOutCount} total=${snapshot.totalCount}",
                )
                snapshot.entries.take(6).forEach { entry ->
                    SelectableResultText(
                        "${entry.statusLabel} manifest=${entry.manifestFilename} local=${entry.localPath.ifBlank { "(none)" }} expected=${entry.expectedSize} actual=${entry.actualSize} reason=${entry.reason.ifBlank { "(none)" }}",
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SelectableActionResult(
    text: String,
    enabled: Boolean,
    onAction: () -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        SelectableResultText(text)
        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Button(
                modifier = Modifier.fillMaxWidth(),
                enabled = enabled,
                onClick = onAction,
            ) { Text("Use") }
        }
    }
}

@Composable
private fun SelectableResultText(text: String) {
    SelectionContainer {
        Text(
            text,
            style = MaterialTheme.typography.bodySmall,
        )
    }
}
