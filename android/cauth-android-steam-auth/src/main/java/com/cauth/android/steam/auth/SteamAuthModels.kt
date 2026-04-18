package com.cauth.android.steam.auth

enum class LoginStatus {
    Succeeded,
    SteamGuardRequired,
    Failed,
    Unsupported;

    companion object {
        fun fromNative(statusCode: Int): LoginStatus = when (statusCode) {
            0 -> Succeeded
            1 -> SteamGuardRequired
            2 -> Failed
            else -> Unsupported
        }
    }
}

enum class LoginPlatform(val nativeValue: Int) {
    SteamClient(0),
    WebBrowser(1),
    MobileApp(2),
}

data class LoginRequest(
    val accountName: String,
    val password: String,
    val steamGuardCode: String? = null,
    val deviceName: String? = "ComposeDemo",
    val rememberSession: Boolean = true,
    val platform: LoginPlatform = LoginPlatform.SteamClient,
)

data class LoginResultSnapshot(
    val statusCode: Int,
    val resultCode: Int,
    val message: String,
    val steamId: Long,
    val accountName: String?,
) {
    val status: LoginStatus
        get() = LoginStatus.fromNative(statusCode)
}

data class SavedSessionSnapshot(
    val present: Boolean,
    val steamId: Long,
    val accountName: String?,
    val hasRefreshToken: Boolean,
    val hasAccessToken: Boolean,
    val createdAtUnixSeconds: Long,
)

data class CmProbeSnapshot(
    val ok: Boolean,
    val endpoint: String?,
    val status: String?,
)

data class CmLogonSnapshot(
    val ok: Boolean,
    val endpoint: String?,
    val status: String?,
    val eresult: Int,
    val eresultExtended: Int,
    val heartbeatSeconds: Int,
    val steamId: Long,
)
