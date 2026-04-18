package com.cauth.example

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import java.io.File

data class DebugFileEntry(
    val absolutePath: String,
    val relativePath: String,
    val isDirectory: Boolean,
    val sizeBytes: Long,
)

private fun scanDebugFiles(root: File): List<DebugFileEntry> {
    if (!root.exists()) return emptyList()
    return root.walkTopDown()
        .maxDepth(4)
        .filter { it != root }
        .sortedBy { it.absolutePath }
        .map { file ->
            DebugFileEntry(
                absolutePath = file.absolutePath,
                relativePath = root.toPath().relativize(file.toPath()).toString(),
                isDirectory = file.isDirectory,
                sizeBytes = if (file.isFile) file.length() else 0L,
            )
        }
        .toList()
}

@Composable
fun DepotDebugFilesPane(
    rootDirectory: File,
    onUseAsManifest: (String) -> Unit,
) {
    var entries by remember(rootDirectory.absolutePath) {
        mutableStateOf(scanDebugFiles(rootDirectory))
    }

    fun refresh() {
        rootDirectory.mkdirs()
        entries = scanDebugFiles(rootDirectory)
    }

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text("App Files Debug", style = MaterialTheme.typography.titleMedium)
            Text(rootDirectory.absolutePath, style = MaterialTheme.typography.bodySmall)

            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(onClick = ::refresh) { Text("Refresh") }
                Button(
                    onClick = {
                        if (rootDirectory.exists()) {
                            rootDirectory.deleteRecursively()
                        }
                        rootDirectory.mkdirs()
                        refresh()
                    },
                ) { Text("Clear Depot Dir") }
            }

            if (entries.isEmpty()) {
                Text("No files yet.", style = MaterialTheme.typography.bodySmall)
            } else {
                entries.take(40).forEach { entry ->
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Column(
                            modifier = Modifier.padding(12.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            Text(entry.relativePath, style = MaterialTheme.typography.bodySmall)
                            Text(
                                if (entry.isDirectory) "directory" else "file size=${entry.sizeBytes}",
                                style = MaterialTheme.typography.bodySmall,
                            )
                            FlowRow(
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                verticalArrangement = Arrangement.spacedBy(8.dp),
                            ) {
                                if (!entry.isDirectory) {
                                    Button(onClick = { onUseAsManifest(entry.absolutePath) }) {
                                        Text("Use as manifest")
                                    }
                                }
                                Button(
                                    onClick = {
                                        File(entry.absolutePath).deleteRecursively()
                                        refresh()
                                    },
                                ) { Text("Delete") }
                            }
                        }
                    }
                }
            }
        }
    }
}
