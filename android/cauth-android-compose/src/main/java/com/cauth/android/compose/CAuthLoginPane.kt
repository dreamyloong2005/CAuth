package com.cauth.android.compose

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.cauth.android.steam.auth.CAuthSteamAuthController

@Composable
fun CAuthLoginPane(
    modifier: Modifier = Modifier,
    controller: CAuthSteamAuthController = rememberCAuthSteamAuthController(),
) {
    val scrollState = rememberScrollState()
    val state by controller.state.collectAsState()

    Card(modifier = modifier) {
        Column(
            modifier = Modifier
                .padding(16.dp)
                .verticalScroll(scrollState),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            CAuthSteamAuthHeader(
                nativeVersion = state.nativeVersion,
            )
            CAuthSteamAuthForm(
                state = state,
                controller = controller,
            )
            CAuthSteamAuthPlatformSelector(
                selectedPlatform = state.loginPlatform,
                onPlatformSelected = controller::setLoginPlatform,
            )
            CAuthSteamAuthRouteSection(
                state = state,
                controller = controller,
            )
            CAuthSteamAuthActionButtons(state = state, controller = controller)
            CAuthSteamAuthStatus(
                statusText = state.statusText,
                moduleStatus = state.moduleStatus,
                moduleTask = state.moduleTask,
            )
            CAuthSteamAuthTrace(traceLines = state.traceLines)
            CAuthSteamAuthResults(state = state, controller = controller)
        }
    }
}
