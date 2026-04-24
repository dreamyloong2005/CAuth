package com.cauth.android

data class CAuthRouteSelection(
    val endpoint: String? = null,
    val protocol: String? = null,
    val role: String? = null,
) {
    fun isEmpty(): Boolean = endpoint.isNullOrBlank() &&
        protocol.isNullOrBlank() &&
        role.isNullOrBlank()
}

data class CAuthRouteProbeEntrySnapshot(
    val endpoint: String,
    val protocol: String,
    val role: String,
    val note: String,
    val latencyMs: Long,
    val latencyKnown: Boolean,
    val recentSuccess: Boolean,
    val recentFailure: Boolean,
    val successCount: Int,
    val failureCount: Int,
) {
    val latencyLabel: String
        get() = if (latencyKnown) "${latencyMs} ms" else "unknown"
}

data class CAuthRouteProbeSnapshot(
    val ok: Boolean,
    val moduleStatus: String,
    val backend: String,
    val message: String,
    val routes: Array<CAuthRouteProbeEntrySnapshot>,
)
