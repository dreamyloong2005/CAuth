package com.cauth.android.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import com.cauth.android.CAuthClient
import com.cauth.android.steam.cloud.CAuthSteamCloudController

@Composable
fun rememberCAuthSteamCloudController(
    client: CAuthClient,
): CAuthSteamCloudController {
    val scope = rememberCoroutineScope()
    return remember(client, scope) { CAuthSteamCloudController(client, scope) }
}
