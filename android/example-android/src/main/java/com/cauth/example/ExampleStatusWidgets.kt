package com.cauth.example

import android.content.ClipData
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.toClipEntry
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch

data class ExampleStatusEntry(
    val label: String,
    val value: String,
)

enum class ExampleStatusTone {
    Neutral,
    Success,
    Warning,
    Failure,
}

enum class ExampleWorkflowStepState {
    Pending,
    Running,
    Done,
    Failed,
}

data class ExampleWorkflowStep(
    val label: String,
    val state: ExampleWorkflowStepState,
)

data class ExampleWorkflowState(
    val title: String,
    val phaseLabel: String,
    val message: String,
    val steps: List<ExampleWorkflowStep>,
    val running: Boolean,
    val tone: ExampleStatusTone,
    val failedStepIndex: Int? = null,
)

data class ExampleWorkflowSnapshot(
    val label: String,
    val status: String,
    val summary: String,
    val trace: String,
)

@Composable
fun ExampleStatusCard(
    title: String,
    entries: List<ExampleStatusEntry>,
    modifier: Modifier = Modifier,
    subtitle: String? = null,
    tone: ExampleStatusTone = ExampleStatusTone.Neutral,
) {
    if (entries.isEmpty()) return

    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = tone.containerColor(),
        ),
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            subtitle?.let {
                Text(it, style = MaterialTheme.typography.bodySmall)
            }
            entries.forEach { entry ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Text(entry.label, style = MaterialTheme.typography.bodySmall)
                    Text(entry.value, style = MaterialTheme.typography.bodySmall)
                }
            }
        }
    }
}

@Composable
fun ExampleWorkflowCard(
    state: ExampleWorkflowState,
    modifier: Modifier = Modifier,
    onRetryFailedStep: (() -> Unit)? = null,
) {
    val completedCount = state.steps.count { it.state == ExampleWorkflowStepState.Done }
    val progress = if (state.steps.isEmpty()) {
        0f
    } else {
        completedCount.toFloat() / state.steps.size.toFloat()
    }

    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = state.tone.containerColor(),
        ),
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text(state.title, style = MaterialTheme.typography.titleMedium)
                Text(state.phaseLabel, style = MaterialTheme.typography.labelLarge)
            }
            if (state.message.isNotBlank()) {
                Text(state.message, style = MaterialTheme.typography.bodySmall)
            }
            LinearProgressIndicator(
                progress = { progress },
                modifier = Modifier.fillMaxWidth(),
            )
            state.steps.forEach { step ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Text(step.label, style = MaterialTheme.typography.bodySmall)
                    Text(step.state.label(), style = MaterialTheme.typography.bodySmall)
                }
            }
            if (!state.running && state.failedStepIndex != null && onRetryFailedStep != null) {
                Button(onClick = onRetryFailedStep) {
                    Text("Retry ${state.steps[state.failedStepIndex].label}")
                }
            }
        }
    }
}

@Composable
fun ExampleExpandableRawCard(
    title: String,
    rawText: String,
    modifier: Modifier = Modifier,
    emptyText: String = "No data yet.",
) {
    var expanded by remember(title, rawText) { mutableStateOf(false) }
    val normalized = rawText.trim()
    val lines = normalized.lines().filter { it.isNotBlank() }
    val previewLines = lines.take(6)

    Card(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text(title, style = MaterialTheme.typography.titleMedium)
                Button(
                    enabled = lines.isNotEmpty(),
                    onClick = { expanded = !expanded },
                ) {
                    Text(if (expanded) "Collapse" else "Expand")
                }
            }

            if (lines.isEmpty()) {
                Text(emptyText, style = MaterialTheme.typography.bodySmall)
            } else {
                SelectionContainer {
                    Column(verticalArrangement = Arrangement.spacedBy(0.dp)) {
                        (if (expanded) lines else previewLines).forEach { line ->
                            Text(line, style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }
                if (!expanded && lines.size > previewLines.size) {
                    Text("...${lines.size - previewLines.size} more line(s)", style = MaterialTheme.typography.bodySmall)
                }
            }
        }
    }
}

@Composable
fun ExampleWorkflowComparisonCard(
    title: String,
    previousFailure: ExampleWorkflowSnapshot,
    currentSnapshot: ExampleWorkflowSnapshot,
    modifier: Modifier = Modifier,
) {
    val changedFields = remember(previousFailure.summary, currentSnapshot.summary) {
        buildWorkflowComparisonRows(previousFailure.summary, currentSnapshot.summary)
    }
    var showSummary by remember(title, previousFailure.summary, currentSnapshot.summary) { mutableStateOf(false) }
    var showTrace by remember(title, previousFailure.trace, currentSnapshot.trace) { mutableStateOf(false) }
    val clipboard = LocalClipboard.current
    val scope = rememberCoroutineScope()
    val tone = when {
        currentSnapshot.status.contains("failed", ignoreCase = true) ||
            currentSnapshot.status.contains("401", ignoreCase = true) -> ExampleStatusTone.Failure
        else -> ExampleStatusTone.Success
    }

    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = tone.containerColor(),
        ),
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            Text(
                "${previousFailure.label}: ${previousFailure.status}",
                style = MaterialTheme.typography.bodySmall,
            )
            Text(
                "${currentSnapshot.label}: ${currentSnapshot.status}",
                style = MaterialTheme.typography.bodySmall,
            )
            if (changedFields.isEmpty()) {
                Text("No key summary fields changed yet.", style = MaterialTheme.typography.bodySmall)
            } else {
                changedFields.take(8).forEach { field ->
                    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        Text(field.label, style = MaterialTheme.typography.labelMedium)
                        Text("Before: ${field.before}", style = MaterialTheme.typography.bodySmall)
                        Text("After: ${field.after}", style = MaterialTheme.typography.bodySmall)
                    }
                }
                if (changedFields.size > 8) {
                    Text("...${changedFields.size - 8} more changed field(s)", style = MaterialTheme.typography.bodySmall)
                }
            }
            ComparisonCopyActions(
                showSummary = showSummary,
                showTrace = showTrace,
                onToggleSummary = { showSummary = !showSummary },
                onToggleTrace = { showTrace = !showTrace },
                onCopyDiff = {
                    scope.launch {
                        clipboard.setClipEntry(
                            ClipData.newPlainText(
                                "$title Diff",
                                buildComparisonCopyText(previousFailure, currentSnapshot, changedFields),
                            ).toClipEntry(),
                        )
                    }
                },
                onCopyBeforeSummary = {
                    scope.launch {
                        clipboard.setClipEntry(
                            ClipData.newPlainText(
                                "${previousFailure.label} Summary",
                                previousFailure.summary,
                            ).toClipEntry(),
                        )
                    }
                },
                onCopyAfterSummary = {
                    scope.launch {
                        clipboard.setClipEntry(
                            ClipData.newPlainText(
                                "${currentSnapshot.label} Summary",
                                currentSnapshot.summary,
                            ).toClipEntry(),
                        )
                    }
                },
                onCopyBeforeTrace = {
                    scope.launch {
                        clipboard.setClipEntry(
                            ClipData.newPlainText(
                                "${previousFailure.label} Trace",
                                previousFailure.trace,
                            ).toClipEntry(),
                        )
                    }
                },
                onCopyAfterTrace = {
                    scope.launch {
                        clipboard.setClipEntry(
                            ClipData.newPlainText(
                                "${currentSnapshot.label} Trace",
                                currentSnapshot.trace,
                            ).toClipEntry(),
                        )
                    }
                },
            )
            if (showSummary) {
                ComparisonRawSection(
                    title = "${previousFailure.label} Summary",
                    rawText = previousFailure.summary,
                )
                ComparisonRawSection(
                    title = "${currentSnapshot.label} Summary",
                    rawText = currentSnapshot.summary,
                )
            }
            if (showTrace) {
                ComparisonRawSection(
                    title = "${previousFailure.label} Trace",
                    rawText = previousFailure.trace,
                )
                ComparisonRawSection(
                    title = "${currentSnapshot.label} Trace",
                    rawText = currentSnapshot.trace,
                )
            }
        }
    }
}

@Composable
private fun ExampleStatusTone.containerColor() = when (this) {
    ExampleStatusTone.Neutral -> MaterialTheme.colorScheme.surfaceVariant
    ExampleStatusTone.Success -> MaterialTheme.colorScheme.secondaryContainer
    ExampleStatusTone.Warning -> MaterialTheme.colorScheme.tertiaryContainer
    ExampleStatusTone.Failure -> MaterialTheme.colorScheme.errorContainer
}

private fun ExampleWorkflowStepState.label(): String = when (this) {
    ExampleWorkflowStepState.Pending -> "Pending"
    ExampleWorkflowStepState.Running -> "Running"
    ExampleWorkflowStepState.Done -> "Done"
    ExampleWorkflowStepState.Failed -> "Failed"
}

private data class ExampleWorkflowComparisonRow(
    val label: String,
    val before: String,
    val after: String,
)

private fun buildWorkflowComparisonRows(
    previousSummary: String,
    currentSummary: String,
): List<ExampleWorkflowComparisonRow> {
    val previous = parseSummaryFields(previousSummary)
    val current = parseSummaryFields(currentSummary)
    val keys = linkedSetOf<String>().apply {
        addAll(previous.keys)
        addAll(current.keys)
    }
    return keys.mapNotNull { key ->
        val before = previous[key]
        val after = current[key]
        if (before == after) {
            null
        } else {
            ExampleWorkflowComparisonRow(
                label = key,
                before = before ?: "(missing)",
                after = after ?: "(missing)",
            )
        }
    }
}

private fun parseSummaryFields(summary: String): Map<String, String> = buildMap {
    summary.lineSequence()
        .map { it.trim() }
        .filter { it.isNotBlank() }
        .forEach { line ->
            val separator = line.indexOf('=')
            if (separator <= 0) return@forEach
            put(
                line.substring(0, separator),
                line.substring(separator + 1),
            )
        }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ComparisonCopyActions(
    showSummary: Boolean,
    showTrace: Boolean,
    onToggleSummary: () -> Unit,
    onToggleTrace: () -> Unit,
    onCopyDiff: () -> Unit,
    onCopyBeforeSummary: () -> Unit,
    onCopyAfterSummary: () -> Unit,
    onCopyBeforeTrace: () -> Unit,
    onCopyAfterTrace: () -> Unit,
) {
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Button(onClick = onToggleSummary) {
            Text(if (showSummary) "Hide Summary" else "Show Summary")
        }
        Button(onClick = onToggleTrace) {
            Text(if (showTrace) "Hide Trace" else "Show Trace")
        }
        Button(onClick = onCopyDiff) {
            Text("Copy Diff")
        }
        Button(onClick = onCopyBeforeSummary) {
            Text("Copy Before Summary")
        }
        Button(onClick = onCopyAfterSummary) {
            Text("Copy After Summary")
        }
        Button(onClick = onCopyBeforeTrace) {
            Text("Copy Before Trace")
        }
        Button(onClick = onCopyAfterTrace) {
            Text("Copy After Trace")
        }
    }
}

@Composable
private fun ComparisonRawSection(
    title: String,
    rawText: String,
) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(title, style = MaterialTheme.typography.labelLarge)
        SelectionContainer {
            Text(
                text = rawText.ifBlank { "(empty)" },
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
}

private fun buildComparisonCopyText(
    previousFailure: ExampleWorkflowSnapshot,
    currentSnapshot: ExampleWorkflowSnapshot,
    changedFields: List<ExampleWorkflowComparisonRow>,
): String = buildString {
    appendLine("${previousFailure.label}: ${previousFailure.status}")
    appendLine("${currentSnapshot.label}: ${currentSnapshot.status}")
    if (changedFields.isEmpty()) {
        appendLine("diff=(none)")
    } else {
        changedFields.forEach { field ->
            appendLine("${field.label}:")
            appendLine("  before=${field.before}")
            appendLine("  after=${field.after}")
        }
    }
}
