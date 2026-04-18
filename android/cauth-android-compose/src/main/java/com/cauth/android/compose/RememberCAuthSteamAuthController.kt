package com.cauth.android.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import com.cauth.android.CAuthClient
import com.cauth.android.steam.auth.CAuthSteamAuthController

@Composable
fun rememberCAuthSteamAuthController(
    client: CAuthClient = remember { CAuthClient.create() },
    closeClientOnDispose: Boolean = true,
): CAuthSteamAuthController {
    val scope = rememberCoroutineScope()
    val controller = remember(client, scope) { CAuthSteamAuthController(client, scope) }

    DisposableEffect(client, closeClientOnDispose) {
        onDispose {
            if (closeClientOnDispose) {
                client.close()
            }
        }
    }

    return controller
}
