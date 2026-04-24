package com.cauth.android.steam.cloud

import com.cauth.android.CAuthFileWriteOptions
import com.cauth.android.CAuthRouteProbeSnapshot
import com.cauth.android.CAuthRouteSelection

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

enum class SteamCloudBackend(val nativeValue: Int) {
    Auto(0),
    Web(1),
    Cm(2);

    companion object {
        fun fromNative(value: Int): SteamCloudBackend = entries.firstOrNull {
            it.nativeValue == value
        } ?: Auto
    }
}

enum class SteamCloudRouteTask(val nativeValue: Int) {
    List(1),
    Verify(2),
    Pull(3),
    Push(4);
}

data class SteamCloudRequest(
    val appId: Int,
    val steamId: Long,
    val accessToken: String? = null,
    val localRoot: String? = null,
    val remoteRoot: String? = null,
    val dryRun: Boolean = false,
    val deleteRemoteOrphans: Boolean = false,
    val conflictPolicy: SteamCloudConflictPolicy = SteamCloudConflictPolicy.Default,
    val backend: SteamCloudBackend = SteamCloudBackend.Auto,
    val routeSelection: CAuthRouteSelection? = null,
    val localWriteOptions: CAuthFileWriteOptions = CAuthFileWriteOptions(),
)

data class SteamCloudVerifyEntrySnapshot(
    val remoteFilename: String,
    val localPath: String,
    val statusCode: Int,
    val remoteSize: Int,
    val remoteTimestamp: Long,
    val remoteSha: String,
    val localSize: Long,
    val localSha: String,
    val reason: String,
) {
    val statusLabel: String
        get() = when (statusCode) {
            1 -> "Missing"
            2 -> "Mismatched"
            3 -> "Size Only"
            4 -> "Extra Local"
            else -> "OK"
        }
}

data class SteamCloudVerifySnapshot(
    val present: Boolean,
    val clean: Boolean,
    val includeExtraLocal: Boolean,
    val appId: Int,
    val moduleStatus: String,
    val checkedCount: Long,
    val okCount: Long,
    val missingCount: Long,
    val mismatchedCount: Long,
    val sizeOnlyCount: Long,
    val filteredOutCount: Long,
    val extraLocalCount: Long,
    val totalCount: Long,
    val message: String,
    val entries: Array<SteamCloudVerifyEntrySnapshot>,
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
    val moduleStatus: String,
    val totalFiles: Long,
    val files: Array<SteamCloudFileEntrySnapshot>,
    val message: String,
)

data class SteamCloudResultSnapshot(
    val ok: Boolean,
    val appId: Int,
    val directionCode: Int,
    val conflictPolicyCode: Int,
    val moduleStatus: String,
    val localFileCount: Long,
    val remoteFileCount: Long,
    val transferredCount: Long,
    val deletedCount: Long,
    val skippedCount: Long,
    val conflictCount: Long,
    val transferredBytes: Long,
    val resumable: Boolean,
    val resumed: Boolean,
    val resumeFromBytes: Long,
    val message: String,
) {
    val direction: SteamCloudDirection
        get() = SteamCloudDirection.fromNative(directionCode)

    val conflictPolicy: SteamCloudConflictPolicy
        get() = SteamCloudConflictPolicy.fromNative(conflictPolicyCode)

    val resumeSummary: String
        get() = when {
            resumed && resumeFromBytes > 0L -> "Resumed from $resumeFromBytes bytes"
            resumed -> "Resumed previous upload state"
            resumable -> "Fresh resumable transfer"
            else -> "Not resumable"
        }
}

data class SteamCloudModuleTaskSnapshot(
    val label: String,
    val active: Boolean,
    val moduleStatus: String,
    val message: String,
    val transferTask: SteamCloudTransferTaskSnapshot? = null,
)

data class SteamCloudTransferTaskSnapshot(
    val handle: Long,
    val kindCode: Int,
    val active: Boolean,
    val finished: Boolean,
    val canceled: Boolean,
    val paused: Boolean,
    val succeeded: Boolean,
    val moduleStatus: String,
    val phase: String,
    val completedSteps: Long,
    val totalSteps: Long,
    val completedBytes: Long,
    val totalBytes: Long,
    val resumable: Boolean,
    val resumed: Boolean,
    val resumeFromBytes: Long,
    val target: String,
    val message: String,
    val result: SteamCloudResultSnapshot?,
    val verifyResult: SteamCloudVerifySnapshot?,
) {
    val kindLabel: String
        get() = when (kindCode) {
            1 -> "Pull"
            2 -> "Push"
            3 -> "Verify"
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
            else -> moduleStatus.ifBlank { phase }
        }

    val resumeSummary: String
        get() = when {
            resumed && resumeFromBytes > 0L -> "Resumed from $resumeFromBytes bytes"
            resumed -> "Resumed previous transfer state"
            resumable -> "Fresh resumable transfer"
            else -> "Not resumable"
        }
}

typealias SteamCloudRouteProbeSnapshot = CAuthRouteProbeSnapshot
