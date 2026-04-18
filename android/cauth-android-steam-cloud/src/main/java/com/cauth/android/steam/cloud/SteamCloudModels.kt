package com.cauth.android.steam.cloud

enum class SteamCloudDirection(val nativeValue: Int) {
    Pull(0),
    Push(1);

    companion object {
        fun fromNative(value: Int): SteamCloudDirection = if (value == 1) Push else Pull
    }
}

enum class SteamCloudConflictPolicy(val nativeValue: Int) {
    Default(0),
    LocalWins(1),
    RemoteWins(2),
    NewerWins(3),
    FailOnConflict(4);

    companion object {
        fun fromNative(value: Int): SteamCloudConflictPolicy = entries.firstOrNull {
            it.nativeValue == value
        } ?: Default
    }
}

data class SteamCloudRequest(
    val appId: Int,
    val accessToken: String? = null,
    val localRoot: String? = null,
    val remoteRoot: String? = null,
    val dryRun: Boolean = false,
    val deleteRemoteOrphans: Boolean = false,
    val conflictPolicy: SteamCloudConflictPolicy = SteamCloudConflictPolicy.Default,
)

data class SteamCloudVerifySnapshot(
    val present: Boolean,
    val clean: Boolean,
    val includeExtraLocal: Boolean,
    val appId: Int,
    val checkedCount: Long,
    val okCount: Long,
    val missingCount: Long,
    val mismatchedCount: Long,
    val sizeOnlyCount: Long,
    val filteredOutCount: Long,
    val extraLocalCount: Long,
    val totalCount: Long,
    val message: String,
)

data class SteamCloudFileEntrySnapshot(
    val appId: Int,
    val ugcId: Long,
    val filename: String,
    val timestamp: Long,
    val fileSize: Int,
    val url: String,
    val steamIdCreator: Long,
    val flags: Int,
    val platformsToSync: String,
    val fileSha: String,
)

data class SteamCloudFileListSnapshot(
    val ok: Boolean,
    val present: Boolean,
    val appId: Int,
    val eresult: Int,
    val totalFiles: Long,
    val files: Array<SteamCloudFileEntrySnapshot>,
    val message: String,
)

data class SteamCloudResultSnapshot(
    val ok: Boolean,
    val appId: Int,
    val directionCode: Int,
    val conflictPolicyCode: Int,
    val localFileCount: Long,
    val remoteFileCount: Long,
    val transferredCount: Long,
    val deletedCount: Long,
    val skippedCount: Long,
    val conflictCount: Long,
    val transferredBytes: Long,
    val message: String,
) {
    val direction: SteamCloudDirection
        get() = SteamCloudDirection.fromNative(directionCode)

    val conflictPolicy: SteamCloudConflictPolicy
        get() = SteamCloudConflictPolicy.fromNative(conflictPolicyCode)
}

data class SteamCloudTransferTaskSnapshot(
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
    val result: SteamCloudResultSnapshot?,
) {
    val kindLabel: String
        get() = when (kindCode) {
            1 -> "Pull"
            2 -> "Push"
            else -> "Transfer"
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
