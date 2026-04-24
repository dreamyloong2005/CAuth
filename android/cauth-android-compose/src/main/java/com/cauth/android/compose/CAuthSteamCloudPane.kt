package com.cauth.android.compose

import android.content.ClipData
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.toClipEntry
import androidx.compose.ui.unit.dp
import com.cauth.android.steam.cloud.CAuthSteamCloudController
import kotlinx.coroutines.launch

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CAuthSteamCloudPane(
    modifier: Modifier = Modifier,
    controller: CAuthSteamCloudController,
) {
    val state by controller.state.collectAsState()
    val clipboard = LocalClipboard.current
    val scope = rememberCoroutineScope()

    Card(modifier = modifier) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text("Steam Cloud", style = MaterialTheme.typography.titleMedium)
            Text(state.statusText, style = MaterialTheme.typography.bodySmall)
            Text("Module status: ${state.moduleStatus}", style = MaterialTheme.typography.bodySmall)
            state.moduleTask?.let { task ->
                Text(
                    "Task: ${task.label} [${task.moduleStatus}] ${task.message}",
                    style = MaterialTheme.typography.bodySmall,
                )
            }

            state.transferTask?.let { task ->
                val progressFraction = task.progressFraction
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        text = "${task.kindLabel} Progress",
                        style = MaterialTheme.typography.labelLarge,
                    )
                    if (progressFraction != null) {
                        LinearProgressIndicator(
                            progress = { progressFraction },
                            modifier = Modifier.fillMaxWidth(),
                        )
                    } else {
                        LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                    }
                    Text(
                        text = "${task.moduleStatus.ifBlank { "idle" }}: ${task.phase.ifBlank { task.kindLabel }}",
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    Text(
                        text = task.progressSummary,
                        style = MaterialTheme.typography.bodySmall,
                    )
                    if (task.resumable || task.resumed || task.kindLabel == "Push") {
                        Text(
                            text = task.resumeSummary,
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                    if (task.target.isNotBlank()) {
                        Text(
                            text = "Target: ${task.target}",
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                    if (task.finished && task.message.isNotBlank()) {
                        Text(
                            text = task.message,
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                    if (task.active) {
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            Button(onClick = controller::pauseActiveTransfer) {
                                Text("Pause Task")
                            }
                            Button(onClick = controller::cancelActiveTransfer) {
                                Text("Cancel Task")
                            }
                        }
                    }
                }
            }

            CAuthSteamCloudRequestSection(
                state = state,
                controller = controller,
            )
            CAuthSteamCloudOptionSection(
                state = state,
                controller = controller,
            )
            CAuthSteamCloudActionSection(
                state = state,
                controller = controller,
            )
            CAuthSteamCloudResultsSection(
                state = state,
                controller = controller,
            )

            if (state.traceLines.isNotEmpty()) {
                val traceText = state.traceLines.joinToString(separator = "\n")
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Text("Trace", style = MaterialTheme.typography.labelLarge)
                        Button(
                            onClick = {
                                scope.launch {
                                    clipboard.setClipEntry(
                                        ClipData.newPlainText("Cloud Trace", traceText).toClipEntry(),
                                    )
                                }
                            },
                        ) {
                            Text("Copy Trace")
                        }
                    }
                    SelectionContainer {
                        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            state.traceLines.forEach { line ->
                                Text(line, style = MaterialTheme.typography.bodySmall)
                            }
                        }
                    }
                }
            }
        }
    }
}
