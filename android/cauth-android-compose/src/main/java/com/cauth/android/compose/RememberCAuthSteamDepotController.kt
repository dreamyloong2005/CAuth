package com.cauth.android.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import com.cauth.android.CAuthClient
import com.cauth.android.steam.depot.CAuthSteamDepotController

@Composable
fun rememberCAuthSteamDepotController(
    client: CAuthClient,
): CAuthSteamDepotController {
    val scope = rememberCoroutineScope()
    return remember(client, scope) { CAuthSteamDepotController(client, scope) }
}
