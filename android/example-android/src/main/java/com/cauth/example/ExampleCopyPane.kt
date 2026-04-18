package com.cauth.example

import android.content.ClipData
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.toClipEntry
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch

@Composable
fun ExampleCopyPane(
    title: String,
    summaryText: String,
    traceText: String? = null,
    modifier: Modifier = Modifier,
) {
    val clipboard = LocalClipboard.current
    val scope = rememberCoroutineScope()
    val previewLines = summaryText
        .lineSequence()
        .filter { it.isNotBlank() }
        .take(6)
        .toList()

    Card(modifier = modifier.fillMaxWidth()) {
        FlowRow(
            modifier = Modifier.padding(12.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(title, style = MaterialTheme.typography.labelLarge)
            previewLines.forEach { line ->
                Text(
                    text = line,
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
            Button(
                onClick = {
                    scope.launch {
                        clipboard.setClipEntry(
                            ClipData.newPlainText(title, summaryText).toClipEntry(),
                        )
                    }
                },
            ) {
                Text("Copy Summary")
            }
            if (!traceText.isNullOrBlank()) {
                Button(
                    onClick = {
                        scope.launch {
                            clipboard.setClipEntry(
                                ClipData.newPlainText("$title Trace", traceText).toClipEntry(),
                            )
                        }
                    },
                ) {
                    Text("Copy Trace")
                }
            }
        }
    }
}
