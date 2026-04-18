package com.cauth.android.steam.depot

data class AppBranchEntrySnapshot(
    val name: String,
    val buildId: String,
    val description: String,
    val timeUpdated: Int,
    val passwordRequired: Boolean,
)

data class AppBranchListSnapshot(
    val present: Boolean,
    val appId: Int,
    val branches: Array<AppBranchEntrySnapshot>,
)

data class DepotManifestEntrySnapshot(
    val depotId: Int,
    val manifestGid: Long,
    val size: Long,
    val downloadSize: Long,
    val encrypted: Boolean,
    val platformLabel: String,
    val osList: String,
    val osArch: String,
    val depotFromApp: String,
    val sharedInstall: Boolean,
)

data class DepotManifestListSnapshot(
    val present: Boolean,
    val appId: Int,
    val branch: String,
    val manifests: Array<DepotManifestEntrySnapshot>,
)

data class DepotPreflightEntrySnapshot(
    val depotId: Int,
    val manifestGid: Long,
    val size: Long,
    val downloadSize: Long,
    val encrypted: Boolean,
    val platformLabel: String,
    val osList: String,
    val osArch: String,
    val depotFromApp: String,
    val sharedInstall: Boolean,
    val accessStatus: String,
    val keyEresult: Int,
    val keyAvailable: Boolean,
)

data class DepotPreflightSnapshot(
    val present: Boolean,
    val appId: Int,
    val branch: String,
    val buildId: String,
    val depots: Array<DepotPreflightEntrySnapshot>,
)

data class DepotKeySnapshot(
    val present: Boolean,
    val depotId: Int,
    val eresult: Int,
    val keyHex: String,
)

data class ManifestRequestCodeSnapshot(
    val present: Boolean,
    val requestCode: Long,
)

fun formatUnsignedDecimal(value: Long): String = value.toULong().toString()

fun parseUnsignedDecimal(value: String): Long? = value
    .trim()
    .takeIf { it.isNotEmpty() }
    ?.toULongOrNull()
    ?.toLong()

data class ManifestInfoSnapshot(
    val present: Boolean,
    val depotId: Int,
    val manifestGid: Long,
    val creationTime: Int,
    val filenamesEncrypted: Boolean,
    val fileCount: Long,
    val chunkCount: Long,
    val totalUncompressedSize: Long,
    val totalCompressedSize: Long,
    val uniqueChunks: Int,
)

data class ManifestFileEntrySnapshot(
    val filename: String,
    val flags: Int,
    val size: Long,
    val chunkCount: Long,
)

data class ManifestFileListSnapshot(
    val present: Boolean,
    val matchedCount: Long,
    val printedCount: Long,
    val totalCount: Long,
    val files: Array<ManifestFileEntrySnapshot>,
)

data class DepotLocalVerifySnapshot(
    val present: Boolean,
    val clean: Boolean,
    val checkedCount: Long,
    val okCount: Long,
    val missingCount: Long,
    val mismatchedCount: Long,
    val sizeOnlyCount: Long,
    val filteredOutCount: Long,
    val totalCount: Long,
)

data class DepotDownloadTaskSnapshot(
    val handle: Long,
    val kindCode: Int,
    val active: Boolean,
    val finished: Boolean,
    val canceled: Boolean,
    val succeeded: Boolean,
    val phase: String,
    val completedSteps: Long,
    val totalSteps: Long,
    val completedBytes: Long,
    val totalBytes: Long,
    val target: String,
    val message: String,
) {
    val kindLabel: String
        get() = when (kindCode) {
            1 -> "Manifest"
            2 -> "Chunk"
            3 -> "File"
            4 -> "All Files"
            else -> "Download"
        }

    val progressFraction: Float?
        get() = when {
            totalBytes > 0L -> (completedBytes.toDouble() / totalBytes.toDouble()).coerceIn(0.0, 1.0).toFloat()
            totalSteps > 0L -> (completedSteps.toDouble() / totalSteps.toDouble()).coerceIn(0.0, 1.0).toFloat()
            else -> null
        }

    val progressSummary: String
        get() = when {
            totalBytes > 0L -> "${completedBytes}/${totalBytes} bytes"
            totalSteps > 0L -> "${completedSteps}/${totalSteps} steps"
            else -> phase
        }
}
