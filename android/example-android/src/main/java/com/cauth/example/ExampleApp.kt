package com.cauth.example

import android.content.ClipData
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.toClipEntry
import androidx.compose.ui.unit.dp
import com.cauth.android.CAuthClient
import com.cauth.android.compose.CAuthSteamAuthActionButtons
import com.cauth.android.compose.CAuthSteamAuthForm
import com.cauth.android.compose.CAuthSteamAuthHeader
import com.cauth.android.compose.CAuthSteamAuthPlatformSelector
import com.cauth.android.compose.CAuthSteamAuthResults
import com.cauth.android.compose.CAuthSteamAuthStatus
import com.cauth.android.compose.CAuthSteamAuthTrace
import com.cauth.android.compose.CAuthSteamCloudPane
import com.cauth.android.compose.CAuthSteamDepotPane
import com.cauth.android.compose.rememberCAuthSteamAuthController
import com.cauth.android.compose.rememberCAuthSteamCloudController
import com.cauth.android.compose.rememberCAuthSteamDepotController
import com.cauth.android.steam.auth.CAuthSteamAuthState
import com.cauth.android.steam.auth.LoginPlatform
import com.cauth.android.steam.cloud.CAuthSteamCloudState
import com.cauth.android.steam.depot.CAuthSteamDepotState
import com.cauth.android.steam.depot.formatUnsignedDecimal
import com.cauth.android.steam.depot.parseUnsignedDecimal
import java.io.File
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun CAuthExampleApp() {
    Scaffold(
        topBar = {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                Text(
                    text = "CAuth Host Example",
                    style = MaterialTheme.typography.titleLarge,
                )
                Text(
                    text = "Auth, Depot, and Cloud run as separate Android modules over the same native client.",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        },
    ) { innerPadding ->
        ExampleScreen(Modifier.padding(innerPadding))
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ExampleScreen(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val client = remember { CAuthClient.create() }
    DisposableEffect(client) {
        onDispose { client.close() }
    }

    val authController = rememberCAuthSteamAuthController(client = client)
    val depotController = rememberCAuthSteamDepotController(client = client)
    val cloudController = rememberCAuthSteamCloudController(client = client)

    val authState by authController.state.collectAsState()
    val depotState by depotController.state.collectAsState()
    val cloudState by cloudController.state.collectAsState()
    val scope = rememberCoroutineScope()
    val clipboard = LocalClipboard.current
    val focusManager = LocalFocusManager.current
    val scrollState = rememberScrollState()
    var selectedSection by remember { mutableStateOf(ExampleSection.Auth) }
    var depotWorkflowState by remember { mutableStateOf<ExampleWorkflowState?>(null) }
    var cloudWorkflowState by remember { mutableStateOf<ExampleWorkflowState?>(null) }
    var cloudWorkflowMode by remember { mutableStateOf<CloudWorkflowMode?>(null) }
    var depotLastFailureSnapshot by remember { mutableStateOf<ExampleWorkflowSnapshot?>(null) }
    var depotRetrySnapshot by remember { mutableStateOf<ExampleWorkflowSnapshot?>(null) }
    var cloudLastFailureSnapshot by remember { mutableStateOf<ExampleWorkflowSnapshot?>(null) }
    var cloudRetrySnapshot by remember { mutableStateOf<ExampleWorkflowSnapshot?>(null) }
    val defaultCloudRoot = remember(context) {
        File(context.filesDir, "cauth-cloud").absolutePath
    }
    val defaultDepotRoot = remember(context) {
        File(context.filesDir, "cauth-depot").absolutePath
    }
    val defaultManifestPath = remember(defaultDepotRoot) {
        File(defaultDepotRoot, "manifest.bin").absolutePath
    }
    val defaultChunkPath = remember(defaultDepotRoot) {
        File(defaultDepotRoot, "chunk.bin").absolutePath
    }
    val defaultFilePath = remember(defaultDepotRoot) {
        File(defaultDepotRoot, "file.bin").absolutePath
    }
    val defaultAllFilesRoot = remember(defaultDepotRoot) {
        defaultDepotRoot
    }

    LaunchedEffect(defaultCloudRoot, cloudState.localRoot) {
        if (cloudState.localRoot.isBlank()) {
            cloudController.setLocalRoot(defaultCloudRoot)
        }
    }
    LaunchedEffect(cloudState.appIdText, cloudState.remoteRoot) {
        if (cloudState.appIdText.isBlank()) {
            cloudController.setAppIdText("2868840")
        }
        if (cloudState.remoteRoot.isBlank()) {
            cloudController.setRemoteRoot("savegames")
        }
    }
    LaunchedEffect(depotState.appIdText, depotState.branch, depotState.maxCountText) {
        if (depotState.appIdText.isBlank()) {
            depotController.setAppIdText("2868840")
        }
        if (depotState.branch.isBlank()) {
            depotController.setBranch("public")
        }
        if (depotState.maxCountText.isBlank()) {
            depotController.setMaxCountText("10")
        }
    }
    LaunchedEffect(defaultManifestPath, depotState.outputPath, depotState.manifestPath) {
        if (depotState.outputPath.isBlank()) {
            depotController.setOutputPath(defaultManifestPath)
        }
        if (depotState.manifestPath.isBlank()) {
            depotController.setManifestPath(defaultManifestPath)
        }
    }
    LaunchedEffect(
        defaultChunkPath,
        defaultFilePath,
        defaultAllFilesRoot,
        depotState.chunkOutputPath,
        depotState.fileOutputPath,
        depotState.allFilesOutputRoot,
        depotState.verifyLocalRoot,
    ) {
        if (depotState.chunkOutputPath.isBlank()) {
            depotController.setChunkOutputPath(defaultChunkPath)
        }
        if (depotState.fileOutputPath.isBlank()) {
            depotController.setFileOutputPath(defaultFilePath)
        }
        if (depotState.allFilesOutputRoot.isBlank()) {
            depotController.setAllFilesOutputRoot(defaultAllFilesRoot)
        }
        if (depotState.verifyLocalRoot.isBlank()) {
            depotController.setVerifyLocalRoot(defaultAllFilesRoot)
        }
    }

    fun startDepotWorkflow(startAtStep: Int = 0) {
        if (startAtStep == 0) {
            depotRetrySnapshot = null
        }
        scope.launch {
            depotWorkflowState = buildDepotWorkflowState(
                phaseLabel = if (startAtStep == 0) "Starting" else "Retrying",
                message = if (startAtStep == 0) {
                    "Preparing manifest workflow"
                } else {
                    "Retrying ${DEPOT_WORKFLOW_STEPS[startAtStep]}"
                },
                running = true,
                completedThrough = startAtStep - 1,
                activeIndex = startAtStep,
            )
            runCatching {
                runDepotManifestFlow(
                    controller = depotController,
                    outputPath = defaultManifestPath,
                    startAtStep = startAtStep,
                    onUpdate = { depotWorkflowState = it },
                )
            }.onSuccess {
                if (startAtStep > 0) {
                    depotRetrySnapshot = captureDepotWorkflowSnapshot(
                        state = depotController.state.value,
                        label = "Retry Result",
                    )
                }
                depotWorkflowState = buildDepotWorkflowState(
                    phaseLabel = "Complete",
                    message = "Manifest flow completed",
                    running = false,
                    completedThrough = DEPOT_WORKFLOW_STEPS.lastIndex,
                    tone = ExampleStatusTone.Success,
                )
            }.onFailure { failure ->
                val failedIndex = resolveFailedStepIndex(
                    workflowState = depotWorkflowState,
                    fallbackIndex = startAtStep,
                )
                val completedThrough = resolveCompletedStepIndex(
                    workflowState = depotWorkflowState,
                    fallbackIndex = startAtStep - 1,
                )
                val hint = buildDepotFailureHint(
                    failedIndex = failedIndex,
                    state = depotController.state.value,
                )
                val failedSnapshot = captureDepotWorkflowSnapshot(
                    state = depotController.state.value,
                    label = if (startAtStep > 0) "Retry Failure" else "Failed Attempt",
                )
                if (startAtStep > 0) {
                    depotRetrySnapshot = failedSnapshot
                    if (depotLastFailureSnapshot == null) {
                        depotLastFailureSnapshot = failedSnapshot
                    }
                } else {
                    depotLastFailureSnapshot = failedSnapshot
                }
                depotWorkflowState = buildDepotWorkflowState(
                    phaseLabel = "Failed",
                    message = listOfNotNull(
                        failure.message ?: "Manifest flow failed",
                        hint,
                    ).joinToString(separator = "\nHint: "),
                    running = false,
                    failedIndex = failedIndex,
                    completedThrough = completedThrough,
                    tone = ExampleStatusTone.Failure,
                )
            }
        }
    }

    fun startCloudWorkflow(mode: CloudWorkflowMode, startAtStep: Int = 0) {
        cloudWorkflowMode = mode
        if (startAtStep == 0) {
            cloudRetrySnapshot = null
        }
        scope.launch {
            cloudWorkflowState = buildCloudWorkflowState(
                mode = mode,
                phaseLabel = if (startAtStep == 0) "Starting" else "Retrying",
                message = if (startAtStep == 0) {
                    "Preparing cloud ${mode.name.lowercase()} flow"
                } else {
                    "Retrying ${resolveCloudWorkflowStepLabel(mode, startAtStep)}"
                },
                running = true,
                completedThrough = startAtStep - 1,
                activeIndex = startAtStep,
            )
            runCatching {
                runCloudFlow(
                    controller = cloudController,
                    mode = mode,
                    startAtStep = startAtStep,
                    onUpdate = { cloudWorkflowState = it },
                )
            }.onSuccess {
                if (startAtStep > 0) {
                    cloudRetrySnapshot = captureCloudWorkflowSnapshot(
                        state = cloudController.state.value,
                        mode = mode,
                        label = "Retry Result",
                    )
                }
                cloudWorkflowState = buildCloudWorkflowState(
                    mode = mode,
                    phaseLabel = "Complete",
                    message = "Cloud ${mode.name.lowercase()} flow completed",
                    running = false,
                    completedThrough = CLOUD_WORKFLOW_STEPS.lastIndex,
                    tone = ExampleStatusTone.Success,
                )
            }.onFailure { failure ->
                val failedIndex = resolveFailedStepIndex(
                    workflowState = cloudWorkflowState,
                    fallbackIndex = startAtStep,
                )
                val completedThrough = resolveCompletedStepIndex(
                    workflowState = cloudWorkflowState,
                    fallbackIndex = startAtStep - 1,
                )
                val hint = buildCloudFailureHint(
                    failedIndex = failedIndex,
                    state = cloudController.state.value,
                )
                val failedSnapshot = captureCloudWorkflowSnapshot(
                    state = cloudController.state.value,
                    mode = mode,
                    label = if (startAtStep > 0) "Retry Failure" else "Failed Attempt",
                )
                if (startAtStep > 0) {
                    cloudRetrySnapshot = failedSnapshot
                    if (cloudLastFailureSnapshot == null) {
                        cloudLastFailureSnapshot = failedSnapshot
                    }
                } else {
                    cloudLastFailureSnapshot = failedSnapshot
                }
                cloudWorkflowState = buildCloudWorkflowState(
                    mode = mode,
                    phaseLabel = "Failed",
                    message = listOfNotNull(
                        failure.message ?: "Cloud ${mode.name.lowercase()} flow failed",
                        hint,
                    ).joinToString(separator = "\nHint: "),
                    running = false,
                    failedIndex = failedIndex,
                    completedThrough = completedThrough,
                    tone = ExampleStatusTone.Failure,
                )
            }
        }
    }

    fun copyText(label: String, text: String) {
        scope.launch {
            clipboard.setClipEntry(
                ClipData.newPlainText(label, text).toClipEntry(),
            )
        }
    }

    fun applyFieldPreset(update: () -> Unit) {
        focusManager.clearFocus(force = true)
        update()
    }

    Column(
        modifier = modifier
            .padding(16.dp)
            .fillMaxSize()
            .verticalScroll(scrollState),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        CAuthSteamAuthHeader(nativeVersion = authState.nativeVersion)

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("Test Surface", style = MaterialTheme.typography.titleMedium)
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    ExampleSection.entries.forEach { section ->
                        FilterChip(
                            selected = selectedSection == section,
                            onClick = { selectedSection = section },
                            label = { Text(section.label) },
                        )
                    }
                }
                Text(
                    text = buildString {
                        append("Auth: ")
                        append(authState.statusText)
                        append(" | Depot: ")
                        append(depotState.statusText)
                        append(" | Cloud: ")
                        append(cloudState.statusText)
                    },
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }

        when (selectedSection) {
            ExampleSection.Auth -> {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        Text("Auth Test Helpers", style = MaterialTheme.typography.titleMedium)
                        Text(
                            text = "Use quick presets before logging in, or pull a saved session / probe CM without retyping.",
                            style = MaterialTheme.typography.bodySmall,
                        )
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            Button(
                                onClick = {
                                    applyFieldPreset {
                                        authController.setDeviceName("AndroidExample_CAuth")
                                        authController.setLoginPlatform(LoginPlatform.SteamClient)
                                    }
                                },
                            ) {
                                Text("Steam Client")
                            }
                            Button(
                                onClick = {
                                    applyFieldPreset {
                                        authController.setDeviceName("AndroidExample_CAuth")
                                        authController.setLoginPlatform(LoginPlatform.WebBrowser)
                                    }
                                },
                            ) {
                                Text("Web Browser")
                            }
                            Button(onClick = authController::loadSavedSession) {
                                Text("Load Saved")
                            }
                            Button(onClick = authController::probeCm) {
                                Text("Probe CM")
                            }
                        }
                    }
                }
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        Text("Steam Auth", style = MaterialTheme.typography.titleMedium)
                        CAuthSteamAuthForm(
                            state = authState,
                            controller = authController,
                        )
                        CAuthSteamAuthPlatformSelector(
                            selectedPlatform = authState.loginPlatform,
                            onPlatformSelected = authController::setLoginPlatform,
                        )
                        CAuthSteamAuthActionButtons(controller = authController)
                        CAuthSteamAuthStatus(statusText = authState.statusText)
                        CAuthSteamAuthTrace(traceLines = authState.traceLines)
                        CAuthSteamAuthResults(state = authState)
                    }
                }
                ExampleStatusCard(
                    title = "Auth Status",
                    entries = buildAuthStatusEntries(authState),
                    tone = buildAuthStatusTone(authState),
                )
                ExampleExpandableRawCard(
                    title = "Auth Raw Result",
                    rawText = buildAuthSummary(authState),
                )
                ExampleExpandableRawCard(
                    title = "Auth Trace",
                    rawText = authState.traceLines.joinToString(separator = "\n"),
                )
                ExampleCopyPane(
                    title = "Auth Copy Tools",
                    summaryText = buildAuthSummary(authState),
                    traceText = authState.traceLines.joinToString(separator = "\n"),
                )
            }

            ExampleSection.Depot -> {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        Text("Depot Test Helpers", style = MaterialTheme.typography.titleMedium)
                        Text(
                            text = "Log in first, then fetch request code, download a manifest, and inspect its file list with the same native client.",
                            style = MaterialTheme.typography.bodySmall,
                        )
                        Text(defaultDepotRoot, style = MaterialTheme.typography.bodySmall)
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            Button(
                                enabled = !depotState.busy,
                                onClick = { startDepotWorkflow() },
                            ) {
                                Text("Run Manifest Flow")
                            }
                            Button(
                                onClick = {
                                    applyFieldPreset {
                                        depotController.setAppIdText("2868840")
                                        depotController.setBranch("public")
                                        depotController.setMaxCountText("10")
                                    }
                                },
                            ) {
                                Text("Preset 2868840")
                            }
                            Button(
                                onClick = {
                                    applyFieldPreset {
                                        depotController.setAppIdText("440")
                                        depotController.setBranch("public")
                                        depotController.setMaxCountText("10")
                                    }
                                },
                            ) {
                                Text("Preset 440")
                            }
                            Button(onClick = { applyFieldPreset { depotController.setBranch("public") } }) {
                                Text("Use public")
                            }
                            Button(onClick = { applyFieldPreset { depotController.setMaxCountText("10") } }) {
                                Text("Count 10")
                            }
                            Button(onClick = { applyFieldPreset { depotController.setOutputPath(defaultManifestPath) } }) {
                                Text("Use default manifest path")
                            }
                            Button(onClick = { applyFieldPreset { depotController.setChunkOutputPath(defaultChunkPath) } }) {
                                Text("Use default chunk path")
                            }
                            Button(onClick = { applyFieldPreset { depotController.setFileOutputPath(defaultFilePath) } }) {
                                Text("Use default file path")
                            }
                            Button(onClick = { applyFieldPreset { depotController.setAllFilesOutputRoot(defaultAllFilesRoot) } }) {
                                Text("Use default depot root")
                            }
                        }
                    }
                }
                depotWorkflowState?.let { workflowState ->
                    ExampleWorkflowCard(
                        state = workflowState,
                        onRetryFailedStep = workflowState.failedStepIndex
                            ?.takeIf { !depotState.busy }
                            ?.let { failedIndex -> { startDepotWorkflow(failedIndex) } },
                    )
                }
                if (depotLastFailureSnapshot != null && depotRetrySnapshot != null) {
                    ExampleWorkflowComparisonCard(
                        title = "Depot Retry Comparison",
                        previousFailure = depotLastFailureSnapshot!!,
                        currentSnapshot = depotRetrySnapshot!!,
                    )
                }
                CAuthSteamDepotPane(
                    modifier = Modifier.fillMaxWidth(),
                    controller = depotController,
                )
                ExampleStatusCard(
                    title = "Depot Status",
                    entries = buildDepotStatusEntries(depotState),
                    tone = buildDepotStatusTone(depotState),
                )
                DepotKeyResultCards(
                    state = depotState,
                    controller = depotController,
                    onCopyText = ::copyText,
                )
                ExampleExpandableRawCard(
                    title = "Depot Raw Result",
                    rawText = buildDepotSummary(depotState),
                )
                ExampleExpandableRawCard(
                    title = "Depot Trace",
                    rawText = depotState.traceLines.joinToString(separator = "\n"),
                )
                ExampleCopyPane(
                    title = "Depot Copy Tools",
                    summaryText = buildDepotSummary(depotState),
                    traceText = depotState.traceLines.joinToString(separator = "\n"),
                )
                DepotDebugFilesPane(
                    rootDirectory = File(defaultDepotRoot),
                    onUseAsManifest = depotController::setManifestPath,
                )
            }

            ExampleSection.Cloud -> {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        Text("Cloud Test Helpers", style = MaterialTheme.typography.titleMedium)
                        Text(
                            text = "Default local root points at this app's private files directory so pull/push can be exercised without extra setup.",
                            style = MaterialTheme.typography.bodySmall,
                        )
                        Text(defaultCloudRoot, style = MaterialTheme.typography.bodySmall)
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            Button(
                                enabled = !cloudState.busy,
                                onClick = { startCloudWorkflow(CloudWorkflowMode.Pull) },
                            ) {
                                Text("Run Pull Flow")
                            }
                            Button(
                                enabled = !cloudState.busy,
                                onClick = { startCloudWorkflow(CloudWorkflowMode.Push) },
                            ) {
                                Text("Run Push Flow")
                            }
                            Button(
                                onClick = {
                                    applyFieldPreset {
                                        cloudController.setAppIdText("2868840")
                                        cloudController.setRemoteRoot("savegames")
                                    }
                                },
                            ) {
                                Text("Preset 2868840")
                            }
                            Button(onClick = { applyFieldPreset { cloudController.setLocalRoot(defaultCloudRoot) } }) {
                                Text("Use app files dir")
                            }
                            Button(onClick = { applyFieldPreset { cloudController.setCountText("20") } }) {
                                Text("Count 20")
                            }
                            Button(onClick = { applyFieldPreset { cloudController.setStartIndexText("0") } }) {
                                Text("Start 0")
                            }
                        }
                    }
                }
                cloudWorkflowState?.let { workflowState ->
                    ExampleWorkflowCard(
                        state = workflowState,
                        onRetryFailedStep = workflowState.failedStepIndex
                            ?.takeIf { !cloudState.busy }
                            ?.let { failedIndex ->
                                cloudWorkflowMode?.let { mode ->
                                    { startCloudWorkflow(mode, failedIndex) }
                                }
                            },
                    )
                }
                if (cloudLastFailureSnapshot != null && cloudRetrySnapshot != null) {
                    ExampleWorkflowComparisonCard(
                        title = "Cloud Retry Comparison",
                        previousFailure = cloudLastFailureSnapshot!!,
                        currentSnapshot = cloudRetrySnapshot!!,
                    )
                }
                CAuthSteamCloudPane(
                    modifier = Modifier.fillMaxWidth(),
                    controller = cloudController,
                )
                ExampleStatusCard(
                    title = "Cloud Status",
                    entries = buildCloudStatusEntries(cloudState),
                    tone = buildCloudStatusTone(cloudState),
                )
                ExampleStatusCard(
                    title = "Cloud Verify",
                    entries = buildCloudVerifyStatusEntries(cloudState),
                    tone = buildCloudVerifyTone(cloudState),
                )
                CloudKeyResultCards(
                    state = cloudState,
                    controller = cloudController,
                )
                ExampleExpandableRawCard(
                    title = "Cloud Raw Result",
                    rawText = buildCloudSummary(cloudState),
                )
                ExampleExpandableRawCard(
                    title = "Cloud Trace",
                    rawText = cloudState.traceLines.joinToString(separator = "\n"),
                )
                ExampleCopyPane(
                    title = "Cloud Copy Tools",
                    summaryText = buildCloudSummary(cloudState),
                    traceText = cloudState.traceLines.joinToString(separator = "\n"),
                )
            }
        }
    }
}

private fun buildAuthStatusEntries(state: CAuthSteamAuthState): List<ExampleStatusEntry> = buildList {
    add(ExampleStatusEntry("Status", state.statusText))
    add(ExampleStatusEntry("Busy", state.busy.toString()))
    state.loginResult?.let {
        add(ExampleStatusEntry("Login", it.status.name))
        add(ExampleStatusEntry("SteamID", it.steamId.toString()))
    }
    state.savedSession?.let {
        add(ExampleStatusEntry("Saved", it.present.toString()))
        add(ExampleStatusEntry("Refresh", it.hasRefreshToken.toString()))
        add(ExampleStatusEntry("Access", it.hasAccessToken.toString()))
    }
    state.cmProbe?.let {
        add(ExampleStatusEntry("CM Probe", if (it.ok) "ok" else "failed"))
        add(ExampleStatusEntry("CM Endpoint", it.endpoint ?: "(none)"))
    }
    state.cmLogon?.let {
        add(ExampleStatusEntry("CM Logon", if (it.ok) "ok" else "failed"))
        add(ExampleStatusEntry("EResult", it.eresult.toString()))
    }
}

private fun buildAuthStatusTone(state: CAuthSteamAuthState): ExampleStatusTone = when {
    state.loginResult?.status == com.cauth.android.steam.auth.LoginStatus.Failed -> ExampleStatusTone.Failure
    state.loginResult?.status == com.cauth.android.steam.auth.LoginStatus.SteamGuardRequired -> ExampleStatusTone.Warning
    state.loginResult?.status == com.cauth.android.steam.auth.LoginStatus.Succeeded -> ExampleStatusTone.Success
    state.savedSession?.present == true -> ExampleStatusTone.Success
    else -> ExampleStatusTone.Neutral
}

private fun buildDepotStatusEntries(state: CAuthSteamDepotState): List<ExampleStatusEntry> = buildList {
    add(ExampleStatusEntry("Status", state.statusText))
    add(ExampleStatusEntry("Busy", state.busy.toString()))
    add(ExampleStatusEntry("AppID", state.appIdText.ifBlank { "(none)" }))
    add(ExampleStatusEntry("Branch", state.branch.ifBlank { "(none)" }))
    resolveSelectedDepotPlatform(state)?.let {
        add(ExampleStatusEntry("Platform", it))
    }
    state.preflight?.let {
        add(ExampleStatusEntry("Build", it.buildId))
        add(ExampleStatusEntry("Depots", it.depots.size.toString()))
    }
    state.depotKey?.let {
        add(ExampleStatusEntry("Depot Key", if (it.present) "present" else "missing"))
        add(ExampleStatusEntry("Depot Key EResult", it.eresult.toString()))
    }
    state.manifestRequestCode?.let {
        add(ExampleStatusEntry("Request Code", if (it.present) formatUnsignedDecimal(it.requestCode) else "missing"))
    }
    add(ExampleStatusEntry("Request Code Field", state.requestCodeText.ifBlank { "(blank)" }))
    state.manifestInfo?.let {
        add(ExampleStatusEntry("Manifest Files", it.fileCount.toString()))
        add(ExampleStatusEntry("Manifest Chunks", it.chunkCount.toString()))
    }
    state.manifestFiles?.let {
        add(ExampleStatusEntry("Listed Files", it.printedCount.toString()))
    }
    state.localVerify?.let {
        add(ExampleStatusEntry("Verify Root", state.verifyLocalRoot.ifBlank { "(none)" }))
        add(ExampleStatusEntry("Verify Clean", it.clean.toString()))
        add(ExampleStatusEntry("Verify Checked", it.checkedCount.toString()))
        add(ExampleStatusEntry("Verify Missing", it.missingCount.toString()))
        add(ExampleStatusEntry("Verify Mismatch", it.mismatchedCount.toString()))
    }
}

private fun buildDepotStatusTone(state: CAuthSteamDepotState): ExampleStatusTone = when {
    state.localVerify?.clean == false -> ExampleStatusTone.Failure
    state.statusText.contains("failed", ignoreCase = true) ||
        state.statusText.contains("required", ignoreCase = true) -> ExampleStatusTone.Failure
    state.localVerify?.clean == true -> ExampleStatusTone.Success
    state.depotKey?.present == true ||
        state.manifestRequestCode?.present == true ||
        state.manifestInfo?.present == true ||
        state.manifestFiles?.present == true -> ExampleStatusTone.Success
    state.preflight != null || state.manifests != null || state.branches != null -> ExampleStatusTone.Warning
    else -> ExampleStatusTone.Neutral
}

private fun buildCloudStatusEntries(state: CAuthSteamCloudState): List<ExampleStatusEntry> = buildList {
    add(ExampleStatusEntry("Status", state.statusText))
    add(ExampleStatusEntry("Busy", state.busy.toString()))
    add(ExampleStatusEntry("AppID", state.appIdText.ifBlank { "(none)" }))
    add(ExampleStatusEntry("Remote Root", state.remoteRoot.ifBlank { "(none)" }))
    add(ExampleStatusEntry("Local Root", state.localRoot.ifBlank { "(none)" }))
    state.fileList?.let {
        add(ExampleStatusEntry("List OK", it.ok.toString()))
        add(ExampleStatusEntry("Remote Files", it.totalFiles.toString()))
        add(ExampleStatusEntry("List EResult", it.eresult.toString()))
    }
    state.operationResult?.let {
        add(ExampleStatusEntry("Direction", it.direction.name))
        add(ExampleStatusEntry("Transferred", it.transferredCount.toString()))
        add(ExampleStatusEntry("Conflicts", it.conflictCount.toString()))
        add(ExampleStatusEntry("Bytes", it.transferredBytes.toString()))
    }
}

private fun buildCloudVerifyStatusEntries(state: CAuthSteamCloudState): List<ExampleStatusEntry> = buildList {
    add(ExampleStatusEntry("Local Root", state.localRoot.ifBlank { "(none)" }))
    add(ExampleStatusEntry("Remote Root", state.remoteRoot.ifBlank { "(none)" }))
    add(ExampleStatusEntry("Include Extra Local", state.verifyIncludeExtraLocal.toString()))
    state.verifyResult?.let {
        add(ExampleStatusEntry("Verify Clean", it.clean.toString()))
        add(ExampleStatusEntry("Checked", it.checkedCount.toString()))
        add(ExampleStatusEntry("OK", it.okCount.toString()))
        add(ExampleStatusEntry("Missing", it.missingCount.toString()))
        add(ExampleStatusEntry("Mismatch", it.mismatchedCount.toString()))
        add(ExampleStatusEntry("Size Only", it.sizeOnlyCount.toString()))
        add(ExampleStatusEntry("Filtered Out", it.filteredOutCount.toString()))
        add(ExampleStatusEntry("Extra Local", it.extraLocalCount.toString()))
        add(ExampleStatusEntry("Total", it.totalCount.toString()))
    } ?: add(ExampleStatusEntry("Verify", "Not run"))
}

private fun buildCloudStatusTone(state: CAuthSteamCloudState): ExampleStatusTone = when {
    state.operationResult?.ok == false || state.fileList?.ok == false ||
        state.statusText.contains("failed", ignoreCase = true) ||
        state.statusText.contains("401", ignoreCase = true) -> ExampleStatusTone.Failure
    state.operationResult?.conflictCount?.let { it > 0 } == true -> ExampleStatusTone.Warning
    state.operationResult?.ok == true || state.fileList?.ok == true -> ExampleStatusTone.Success
    else -> ExampleStatusTone.Neutral
}

private fun buildCloudVerifyTone(state: CAuthSteamCloudState): ExampleStatusTone = when {
    state.verifyResult?.clean == false -> ExampleStatusTone.Failure
    state.verifyResult?.clean == true &&
        state.verifyResult?.sizeOnlyCount?.let { it > 0 } == true -> ExampleStatusTone.Warning
    state.verifyResult?.clean == true -> ExampleStatusTone.Success
    else -> ExampleStatusTone.Neutral
}

@Composable
private fun DepotKeyResultCards(
    state: CAuthSteamDepotState,
    controller: com.cauth.android.steam.depot.CAuthSteamDepotController,
    onCopyText: (String, String) -> Unit,
) {
    val focusManager = LocalFocusManager.current
    fun applyFieldPreset(update: () -> Unit) {
        focusManager.clearFocus(force = true)
        update()
    }

    state.preflight?.let { snapshot ->
        ExampleKeyResultCard(
            title = "Preflight Picks",
            subtitle = "Tap a depot row to reuse depot id and manifest gid.",
        ) {
            snapshot.depots.take(4).forEach { entry ->
                ExampleKeyResultRow(
                    label = "Depot ${entry.depotId}",
                    value = buildString {
                        append("platform=${entry.platformLabel} gid=${entry.manifestGid} access=${entry.accessStatus}")
                        if (entry.depotFromApp.isNotBlank()) {
                            append(" fromApp=${entry.depotFromApp}")
                        }
                    },
                    actions = {
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            Button(
                                enabled = !state.busy,
                                onClick = {
                                    applyFieldPreset {
                                        controller.useManifestSelection(entry.depotId, entry.manifestGid)
                                    }
                                },
                            ) {
                                Text("Use Depot + Manifest")
                            }
                            Button(
                                enabled = !state.busy,
                                onClick = {
                                    applyFieldPreset {
                                        controller.prepareKeyAndCodeSelection(entry.depotId, entry.manifestGid)
                                    }
                                },
                            ) {
                                Text("Prepare Key + Code")
                            }
                        }
                    },
                )
            }
        }
    }

    state.depotKey?.takeIf { it.present && it.keyHex.isNotBlank() }?.let { snapshot ->
        ExampleKeyResultCard(
            title = "Depot Key Result",
            subtitle = "Reuse the returned key for manifest/file inspection.",
        ) {
            ExampleKeyResultRow(
                label = "Depot ${snapshot.depotId}",
                value = "eresult=${snapshot.eresult}",
                actions = {
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Button(
                            enabled = !state.busy,
                            onClick = { applyFieldPreset { controller.setDepotKeyHex(snapshot.keyHex) } },
                        ) {
                            Text("Use Depot Key")
                        }
                        Button(
                            enabled = !state.busy,
                            onClick = {
                                applyFieldPreset {
                                    controller.setDepotIdText(snapshot.depotId.toString())
                                    controller.setDepotKeyHex(snapshot.keyHex)
                                }
                            },
                        ) {
                            Text("Ready File Actions")
                        }
                    }
                },
            )
        }
    }

    state.manifestRequestCode?.takeIf { it.present }?.let { snapshot ->
        ExampleKeyResultCard(
            title = "Manifest Request Code",
            subtitle = "Push the returned request code back into the download form.",
        ) {
            ExampleKeyResultRow(
                label = "Request Code",
                value = formatUnsignedDecimal(snapshot.requestCode),
                actions = {
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Button(
                            enabled = !state.busy,
                            onClick = {
                                applyFieldPreset {
                                    controller.setRequestCodeText(formatUnsignedDecimal(snapshot.requestCode))
                                }
                            },
                        ) {
                            Text("Use Request Code")
                        }
                        Button(
                            enabled = snapshot.requestCode.toULong() > 0uL,
                            onClick = {
                                onCopyText(
                                    "Manifest Request Code",
                                    formatUnsignedDecimal(snapshot.requestCode),
                                )
                            },
                        ) {
                            Text("Copy Request Code")
                        }
                        Button(
                            enabled = !state.busy,
                            onClick = {
                                applyFieldPreset {
                                    controller.setRequestCodeText(formatUnsignedDecimal(snapshot.requestCode))
                                    val resolvedPath = state.outputPath.ifBlank { state.manifestPath }
                                    if (resolvedPath.isNotBlank()) {
                                        controller.setManifestPath(resolvedPath)
                                    }
                                }
                            },
                        ) {
                            Text("Ready Manifest Load")
                        }
                    }
                },
            )
        }
    }

    state.manifestFiles?.let { snapshot ->
        val pageSize = 6
        val totalItems = snapshot.files.size
        val totalPages = maxOf(1, (totalItems + pageSize - 1) / pageSize)
        var manifestFilePage by remember(totalItems, snapshot.printedCount, snapshot.matchedCount) {
            mutableStateOf(0)
        }
        val currentPage = manifestFilePage.coerceIn(0, totalPages - 1)
        val pageStart = currentPage * pageSize
        val pageEndExclusive = minOf(pageStart + pageSize, totalItems)
        val pageItems = if (pageStart < pageEndExclusive) {
            snapshot.files.asList().subList(pageStart, pageEndExclusive)
        } else {
            emptyList()
        }

        ExampleKeyResultCard(
            title = "Manifest File Picks",
            subtitle = "Tap a file to reuse it in chunk/file download fields. Page ${currentPage + 1}/$totalPages, showing ${pageItems.size} of ${snapshot.printedCount}.",
        ) {
            if (totalPages > 1) {
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Button(
                        enabled = currentPage > 0,
                        onClick = { manifestFilePage = (currentPage - 1).coerceAtLeast(0) },
                    ) {
                        Text("Prev Page")
                    }
                    Button(
                        enabled = currentPage + 1 < totalPages,
                        onClick = { manifestFilePage = (currentPage + 1).coerceAtMost(totalPages - 1) },
                    ) {
                        Text("Next Page")
                    }
                    Text(
                        text = "Items ${pageStart + 1}-${pageEndExclusive} / $totalItems",
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
            }
            pageItems.forEach { entry ->
                ExampleKeyResultRow(
                    label = entry.filename,
                    value = "size=${entry.size} chunks=${entry.chunkCount}",
                    actions = {
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            Button(
                                enabled = !state.busy,
                                onClick = { applyFieldPreset { controller.useManifestFile(entry.filename) } },
                            ) {
                                Text("Use File")
                            }
                            Button(
                                enabled = !state.busy,
                                onClick = {
                                    applyFieldPreset {
                                        controller.useManifestFile(entry.filename)
                                        controller.setChunkIndexText("0")
                                    }
                                },
                            ) {
                                Text("Ready Chunk 0")
                            }
                        }
                    },
                )
            }
            if (snapshot.printedCount > totalItems) {
                Text(
                    text = "...${snapshot.printedCount - totalItems} more file pick(s) were omitted by the current native list limit",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }
    }
}

@Composable
private fun CloudKeyResultCards(
    state: CAuthSteamCloudState,
    controller: com.cauth.android.steam.cloud.CAuthSteamCloudController,
) {
    val focusManager = LocalFocusManager.current
    fun applyFieldPreset(update: () -> Unit) {
        focusManager.clearFocus(force = true)
        update()
    }

    state.fileList?.let { snapshot ->
        ExampleKeyResultCard(
            title = "Cloud File Picks",
            subtitle = "Tap a remote directory to narrow the next list or transfer run.",
        ) {
            snapshot.files.take(5).forEach { entry ->
                val remoteDir = entry.filename.substringBeforeLast('/', "")
                ExampleKeyResultRow(
                    label = entry.filename,
                    value = "size=${entry.fileSize} ts=${entry.timestamp}",
                    actions = {
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            Button(
                                enabled = !state.busy && remoteDir.isNotBlank(),
                                onClick = { applyFieldPreset { controller.setRemoteRoot(remoteDir) } },
                            ) {
                                Text(if (remoteDir.isBlank()) "No Parent Dir" else "Use Remote Dir")
                            }
                            Button(
                                enabled = !state.busy && remoteDir.isNotBlank(),
                                onClick = {
                                    applyFieldPreset {
                                        controller.setRemoteRoot(remoteDir)
                                        controller.setStartIndexText("0")
                                        controller.setCountText("20")
                                    }
                                },
                            ) {
                                Text(if (remoteDir.isBlank()) "No Parent Dir" else "Use Dir + Reset Paging")
                            }
                        }
                    },
                )
            }
        }
    }

    state.operationResult?.let { result ->
        ExampleKeyResultCard(
            title = "Cloud Transfer Result",
            subtitle = "Quickly reuse the current roots for the next run.",
        ) {
            ExampleKeyResultRow(
                label = result.direction.name,
                value = "transferred=${result.transferredCount} conflicts=${result.conflictCount}",
                actions = {
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Button(
                            enabled = !state.busy && state.remoteRoot.isNotBlank(),
                            onClick = { applyFieldPreset { controller.setRemoteRoot(state.remoteRoot) } },
                        ) {
                            Text("Reuse Remote Root")
                        }
                        Button(
                            enabled = !state.busy && state.localRoot.isNotBlank(),
                            onClick = { applyFieldPreset { controller.setLocalRoot(state.localRoot) } },
                        ) {
                            Text("Reuse Local Root")
                        }
                        Button(
                            enabled = !state.busy && (state.remoteRoot.isNotBlank() || state.localRoot.isNotBlank()),
                            onClick = {
                                applyFieldPreset {
                                    if (state.remoteRoot.isNotBlank()) {
                                        controller.setRemoteRoot(state.remoteRoot)
                                    }
                                    if (state.localRoot.isNotBlank()) {
                                        controller.setLocalRoot(state.localRoot)
                                    }
                                    controller.setStartIndexText("0")
                                }
                            },
                        ) {
                            Text("Reuse All Roots")
                        }
                    }
                },
            )
        }
    }
}

private enum class CloudWorkflowMode {
    Pull,
    Push,
}

private val DEPOT_WORKFLOW_STEPS = listOf(
    "Fetch Preflight",
    "Select Depot",
    "Fetch Depot Key",
    "Fetch Request Code",
    "Download Manifest",
    "Load Manifest Info",
    "List Manifest Files",
)

private val CLOUD_WORKFLOW_STEPS = listOf(
    "List Remote Files",
    "Transfer",
)

private suspend fun runDepotManifestFlow(
    controller: com.cauth.android.steam.depot.CAuthSteamDepotController,
    outputPath: String,
    startAtStep: Int = 0,
    onUpdate: (ExampleWorkflowState) -> Unit,
) {
    require(startAtStep in DEPOT_WORKFLOW_STEPS.indices) {
        "Depot workflow step out of range: $startAtStep"
    }
    var currentStep = startAtStep
    fun update(
        phaseLabel: String,
        message: String,
        completedThrough: Int = currentStep - 1,
        activeIndex: Int? = currentStep,
        failedIndex: Int? = null,
        running: Boolean = true,
        tone: ExampleStatusTone = ExampleStatusTone.Neutral,
    ) {
        onUpdate(
            buildDepotWorkflowState(
                phaseLabel = phaseLabel,
                message = message,
                running = running,
                completedThrough = completedThrough,
                activeIndex = activeIndex,
                failedIndex = failedIndex,
                tone = tone,
            ),
        )
    }

    controller.setOutputPath(outputPath)
    controller.setManifestPath(outputPath)
    if (startAtStep <= 0) {
        currentStep = 0
        update(
            phaseLabel = "Preflight",
            message = "Fetching depot preflight data",
        )
        val previousTrace = controller.state.value.traceLines.firstOrNull()
        controller.fetchPreflight()
        val postPreflight = waitForDepotStep(controller, previousTrace)
        val preflight = requireNotNull(postPreflight.preflight) {
            postPreflight.statusText.ifBlank { "Preflight did not return any result" }
        }
        require(hasUsablePreflightResult(preflight)) {
            postPreflight.statusText.ifBlank { "Preflight returned no usable depots" }
        }
    } else {
        val preflight = requireNotNull(controller.state.value.preflight) {
            "Retry requires an existing preflight result"
        }
        require(hasUsablePreflightResult(preflight)) {
            "Retry requires a usable preflight result"
        }
    }

    if (startAtStep <= 1) {
        val selected = requireDepotWorkflowSelection(controller.state.value)
        currentStep = 1
        update(
            phaseLabel = "Select Depot",
            message = "Using depot ${selected.depotId} manifest ${selected.manifestGid}",
        )
        controller.prepareKeyAndCodeSelection(selected.depotId, selected.manifestGid)
    } else {
        requirePositiveStepValue(controller.state.value.depotIdText, "Retry requires a depot selection")
        requirePositiveStepValue(controller.state.value.manifestGidText, "Retry requires a manifest selection")
    }

    if (startAtStep <= 2) {
        currentStep = 2
        update(
            phaseLabel = "Depot Key",
            message = "Fetching depot key",
        )
        val previousTrace = controller.state.value.traceLines.firstOrNull()
        controller.fetchDepotKey()
        waitForDepotStep(controller, previousTrace)
    }

    if (startAtStep <= 3) {
        currentStep = 3
        update(
            phaseLabel = "Request Code",
            message = "Fetching manifest request code",
        )
        val previousTrace = controller.state.value.traceLines.firstOrNull()
        controller.fetchManifestRequestCode()
        val postRequestCode = waitForDepotStep(controller, previousTrace)
        require(hasUsableManifestRequestCode(postRequestCode)) {
            postRequestCode.statusText.ifBlank { "Manifest request code was not returned" }
        }
    }

    require(hasUsableManifestRequestCode(controller.state.value)) {
        "Manifest request code was not returned"
    }

    if (startAtStep <= 4) {
        currentStep = 4
        update(
            phaseLabel = "Download Manifest",
            message = "Downloading manifest to $outputPath",
        )
        val previousTrace = controller.state.value.traceLines.firstOrNull()
        controller.downloadManifest()
        val postDownload = waitForDepotStep(
            controller = controller,
            previousTraceHead = previousTrace,
            timeoutMs = null,
        )
        val downloadedManifest = File(outputPath)
        require(downloadedManifest.exists() && downloadedManifest.length() > 0L) {
            postDownload.statusText.ifBlank { "Manifest download did not produce a file" }
        }
    }

    if (startAtStep <= 5) {
        currentStep = 5
        update(
            phaseLabel = "Manifest Info",
            message = "Loading manifest metadata",
        )
        val previousTrace = controller.state.value.traceLines.firstOrNull()
        controller.loadManifestInfo()
        val postManifestInfo = waitForDepotStep(controller, previousTrace)
        require(hasUsableManifestInfo(postManifestInfo)) {
            postManifestInfo.statusText.ifBlank { "Manifest info was not returned" }
        }
    }

    if (startAtStep <= 6) {
        currentStep = 6
        update(
            phaseLabel = "File List",
            message = "Listing files from manifest",
        )
        val previousTrace = controller.state.value.traceLines.firstOrNull()
        controller.listManifestFiles()
        val postManifestFiles = waitForDepotStep(controller, previousTrace)
        require(hasUsableManifestFileList(postManifestFiles)) {
            postManifestFiles.statusText.ifBlank { "Manifest file list was not returned" }
        }
    }
}

private suspend fun runCloudFlow(
    controller: com.cauth.android.steam.cloud.CAuthSteamCloudController,
    mode: CloudWorkflowMode,
    startAtStep: Int = 0,
    onUpdate: (ExampleWorkflowState) -> Unit,
) {
    require(startAtStep in CLOUD_WORKFLOW_STEPS.indices) {
        "Cloud workflow step out of range: $startAtStep"
    }
    if (startAtStep <= 0) {
        onUpdate(
            buildCloudWorkflowState(
                mode = mode,
                phaseLabel = "List",
                message = "Listing remote files",
                running = true,
                activeIndex = 0,
            ),
        )
        val previousStatus = controller.state.value.statusText
        val previousTraceHead = controller.state.value.traceLines.firstOrNull()
        controller.listRemoteFiles()
        waitForIdle(
            isBusy = { controller.state.value.busy },
            hasObservedChange = {
                val state = controller.state.value
                state.statusText != previousStatus ||
                    state.traceLines.firstOrNull() != previousTraceHead
            },
            timeoutMs = null,
        )
        require(hasUsableCloudFileList(controller.state.value)) {
            controller.state.value.statusText.ifBlank { "Remote file list was not returned" }
        }
    } else {
        require(hasUsableCloudFileList(controller.state.value)) {
            "Retry requires a usable remote file list"
        }
    }

    if (startAtStep <= 1) {
        onUpdate(
            buildCloudWorkflowState(
                mode = mode,
                phaseLabel = if (mode == CloudWorkflowMode.Pull) "Pull" else "Push",
                message = if (mode == CloudWorkflowMode.Pull) {
                    "Pulling remote files"
                } else {
                    "Pushing local files"
                },
                running = true,
                completedThrough = 0,
                activeIndex = 1,
            ),
        )
        val previousStatus = controller.state.value.statusText
        val previousTraceHead = controller.state.value.traceLines.firstOrNull()
        when (mode) {
            CloudWorkflowMode.Pull -> controller.pull()
            CloudWorkflowMode.Push -> controller.push()
        }
        waitForIdle(
            isBusy = { controller.state.value.busy },
            hasObservedChange = {
                val state = controller.state.value
                state.statusText != previousStatus ||
                    state.traceLines.firstOrNull() != previousTraceHead
            },
        )
        require(hasUsableCloudOperationResult(controller.state.value)) {
            controller.state.value.statusText.ifBlank {
                "Cloud ${mode.name.lowercase()} did not complete successfully"
            }
        }
    }
}

private suspend fun waitForIdle(
    isBusy: () -> Boolean,
    hasObservedChange: () -> Boolean = { false },
    timeoutMs: Int? = 40_000,
) {
    var sawBusy = isBusy()
    var elapsedMs = 0
    while (timeoutMs == null || elapsedMs < timeoutMs) {
        val busy = isBusy()
        if (busy) {
            sawBusy = true
        }
        if (!busy && (sawBusy || hasObservedChange())) {
            return
        }
        delay(100)
        elapsedMs += 100
    }
    error("Timed out waiting for workflow step to finish")
}

private suspend fun waitForDepotStep(
    controller: com.cauth.android.steam.depot.CAuthSteamDepotController,
    previousTraceHead: String?,
    timeoutMs: Int? = 40_000,
): CAuthSteamDepotState {
    var sawBusy = controller.state.value.busy
    var elapsedMs = 0
    while (timeoutMs == null || elapsedMs < timeoutMs) {
        val state = controller.state.value
        val traceChanged = state.traceLines.firstOrNull() != previousTraceHead
        if (state.busy) {
            sawBusy = true
        }
        if (!state.busy && (sawBusy || traceChanged)) {
            return state
        }
        delay(100)
        elapsedMs += 100
    }
    error("Timed out waiting for depot workflow step to finish")
}

private fun buildDepotFailureHint(
    failedIndex: Int?,
    state: CAuthSteamDepotState,
): String {
    val step = failedIndex?.let { DEPOT_WORKFLOW_STEPS.getOrNull(it) }
    return when {
        step == "Fetch Preflight" ->
            "先确认已经登录，并检查 AppID 与 branch 是否正确；可先单独点 Preflight 看是否返回 depots。"
        step == "Fetch Depot Key" ->
            "常见原因是账号没有该 depot 的访问权，或当前登录态不适合下载；先检查 ownership、DepotID 和登录方式。"
        step == "Fetch Request Code" ->
            "通常是 depotId / manifestGid / branch 组合不匹配，或者 branch 需要密码；检查 manifest 选择结果和 branch password hash。"
        step == "Download Manifest" ->
            "确认 request code 已返回且未过期，同时输出路径可写。"
        step == "Load Manifest Info" || step == "List Manifest Files" ->
            "确认 manifest 已成功下载；如果文件名加密，记得先把 depot key 回填进表单。"
        state.statusText.contains("required", ignoreCase = true) ->
            "当前表单还有缺失字段，优先看上面的状态文本提示。"
        else ->
            "先单独运行失败那一步，观察状态卡片和 Trace，再检查表单字段是否被关键结果卡正确回填。"
    }
}

private fun buildCloudFailureHint(
    failedIndex: Int?,
    state: CAuthSteamCloudState,
): String {
    val step = failedIndex?.let { CLOUD_WORKFLOW_STEPS.getOrNull(it) }
    return when {
        state.statusText.contains("401", ignoreCase = true) ||
            state.statusText.contains("access token", ignoreCase = true) ->
            "看起来像是 cloud 授权材料无效；先重新登录，再单独点 List 验证。"
        step == "List Remote Files" ->
            "优先检查 AppID、remote root 和当前登录态；先确保 List 单步能返回文件。"
        step == "Transfer" ->
            "如果是 Pull，检查 local root 是否可写；如果是 Push，检查 local root 下是否真的有文件可上传。"
        state.operationResult?.conflictCount?.let { it > 0 } == true ->
            "当前可能是冲突策略导致的停顿，尝试切换 conflict policy 再跑一次。"
        else ->
            "先单独执行 List，再跑 Pull/Push，确认 remote root 和 local root 都指向预期目录。"
    }
}

private fun buildDepotWorkflowState(
    phaseLabel: String,
    message: String,
    running: Boolean,
    completedThrough: Int = -1,
    activeIndex: Int? = null,
    failedIndex: Int? = null,
    tone: ExampleStatusTone = ExampleStatusTone.Neutral,
): ExampleWorkflowState = ExampleWorkflowState(
    title = "Depot Workflow",
    phaseLabel = phaseLabel,
    message = message,
    running = running,
    tone = tone,
    failedStepIndex = failedIndex,
    steps = DEPOT_WORKFLOW_STEPS.mapIndexed { index, label ->
        ExampleWorkflowStep(
            label = label,
            state = when {
                failedIndex == index -> ExampleWorkflowStepState.Failed
                activeIndex == index -> ExampleWorkflowStepState.Running
                index <= completedThrough -> ExampleWorkflowStepState.Done
                else -> ExampleWorkflowStepState.Pending
            },
        )
    },
)

private fun buildCloudWorkflowState(
    mode: CloudWorkflowMode,
    phaseLabel: String,
    message: String,
    running: Boolean,
    completedThrough: Int = -1,
    activeIndex: Int? = null,
    failedIndex: Int? = null,
    tone: ExampleStatusTone = ExampleStatusTone.Neutral,
): ExampleWorkflowState = ExampleWorkflowState(
    title = if (mode == CloudWorkflowMode.Pull) "Cloud Pull Workflow" else "Cloud Push Workflow",
    phaseLabel = phaseLabel,
    message = message,
    running = running,
    tone = tone,
    failedStepIndex = failedIndex,
    steps = CLOUD_WORKFLOW_STEPS.mapIndexed { index, label ->
        val resolvedLabel = if (index == 1) {
            if (mode == CloudWorkflowMode.Pull) "Pull Remote Files" else "Push Local Files"
        } else {
            label
        }
        ExampleWorkflowStep(
            label = resolvedLabel,
            state = when {
                failedIndex == index -> ExampleWorkflowStepState.Failed
                activeIndex == index -> ExampleWorkflowStepState.Running
                index <= completedThrough -> ExampleWorkflowStepState.Done
                else -> ExampleWorkflowStepState.Pending
            },
        )
    },
)

private fun resolveFailedStepIndex(
    workflowState: ExampleWorkflowState?,
    fallbackIndex: Int,
): Int? = workflowState?.failedStepIndex
    ?: workflowState?.steps?.indexOfFirst { it.state == ExampleWorkflowStepState.Running }?.takeIf { it >= 0 }
    ?: fallbackIndex.takeIf { it >= 0 }

private fun resolveCompletedStepIndex(
    workflowState: ExampleWorkflowState?,
    fallbackIndex: Int,
): Int = workflowState?.steps?.indexOfLast { it.state == ExampleWorkflowStepState.Done }
    ?.takeIf { it >= 0 }
    ?: fallbackIndex

private fun requireDepotWorkflowSelection(
    state: CAuthSteamDepotState,
): com.cauth.android.steam.depot.DepotPreflightEntrySnapshot {
    val preflight = requireNotNull(state.preflight) {
        "Preflight did not return any result"
    }
    val selectedDepotId = state.depotIdText.toIntOrNull()
    val selectedManifestGid = state.manifestGidText.toLongOrNull()
    return preflight.depots.firstOrNull {
        selectedDepotId != null &&
            selectedManifestGid != null &&
            it.depotId == selectedDepotId &&
            it.manifestGid == selectedManifestGid
    } ?: preflight.depots.firstOrNull {
        it.manifestGid > 0L &&
            it.platformLabel.contains("windows", ignoreCase = true)
    } ?: preflight.depots.firstOrNull { it.manifestGid > 0L }
    ?: error("Preflight did not return a usable depot")
}

private fun hasUsablePreflightResult(
    preflight: com.cauth.android.steam.depot.DepotPreflightSnapshot,
): Boolean = preflight.depots.isNotEmpty()

private fun hasUsableManifestRequestCode(
    state: CAuthSteamDepotState,
): Boolean {
    val parsedRequestCode = parseUnsignedDecimal(state.requestCodeText)
    val snapshot = state.manifestRequestCode
    return (parsedRequestCode != null && parsedRequestCode.toULong() > 0uL) ||
        (snapshot?.present == true && snapshot.requestCode.toULong() > 0uL)
}

private fun hasUsableManifestInfo(
    state: CAuthSteamDepotState,
): Boolean {
    val snapshot = state.manifestInfo
    return snapshot?.present == true &&
        snapshot.depotId > 0 &&
        snapshot.manifestGid.toULong() > 0uL
}

private fun hasUsableManifestFileList(
    state: CAuthSteamDepotState,
): Boolean {
    val snapshot = state.manifestFiles
    return snapshot?.present == true && snapshot.totalCount >= 0L
}

private fun hasUsableCloudFileList(
    state: CAuthSteamCloudState,
): Boolean {
    val snapshot = state.fileList
    return snapshot?.ok == true && snapshot.present
}

private fun hasUsableCloudOperationResult(
    state: CAuthSteamCloudState,
): Boolean = state.operationResult?.ok == true

private fun requirePositiveStepValue(raw: String, message: String) {
    require((raw.toLongOrNull() ?: 0L) > 0L) { message }
}

private fun resolveCloudWorkflowStepLabel(
    mode: CloudWorkflowMode,
    stepIndex: Int,
): String = when (stepIndex) {
    0 -> CLOUD_WORKFLOW_STEPS[0]
    1 -> if (mode == CloudWorkflowMode.Pull) "Pull Remote Files" else "Push Local Files"
    else -> "Cloud Step"
}

private fun resolveSelectedDepotPlatform(state: CAuthSteamDepotState): String? {
    val selectedDepotId = state.depotIdText.toIntOrNull()
    val selectedManifestGid = state.manifestGidText.toLongOrNull()
    state.preflight?.depots?.firstOrNull {
        selectedDepotId != null &&
            selectedManifestGid != null &&
            it.depotId == selectedDepotId &&
            it.manifestGid == selectedManifestGid
    }?.let { return it.platformLabel }
    state.manifests?.manifests?.firstOrNull {
        selectedDepotId != null &&
            selectedManifestGid != null &&
            it.depotId == selectedDepotId &&
            it.manifestGid == selectedManifestGid
    }?.let { return it.platformLabel }
    return null
}

private fun captureDepotWorkflowSnapshot(
    state: CAuthSteamDepotState,
    label: String,
): ExampleWorkflowSnapshot = ExampleWorkflowSnapshot(
    label = label,
    status = state.statusText,
    summary = buildDepotSummary(state),
    trace = state.traceLines.joinToString(separator = "\n"),
)

private fun captureCloudWorkflowSnapshot(
    state: CAuthSteamCloudState,
    mode: CloudWorkflowMode,
    label: String,
): ExampleWorkflowSnapshot = ExampleWorkflowSnapshot(
    label = "$label (${mode.name.lowercase()})",
    status = state.statusText,
    summary = buildCloudSummary(state),
    trace = state.traceLines.joinToString(separator = "\n"),
)

private fun buildAuthSummary(state: CAuthSteamAuthState): String = buildString {
    appendLine("status=${state.statusText}")
    appendLine("busy=${state.busy}")
    state.loginResult?.let {
        appendLine("login.status=${it.status}")
        appendLine("login.message=${it.message}")
        appendLine("login.steamId=${it.steamId}")
        appendLine("login.account=${it.accountName ?: "(none)"}")
    }
    state.savedSession?.let {
        appendLine("saved.present=${it.present}")
        appendLine("saved.account=${it.accountName ?: "(none)"}")
        appendLine("saved.steamId=${it.steamId}")
        appendLine("saved.refresh=${it.hasRefreshToken}")
        appendLine("saved.access=${it.hasAccessToken}")
    }
    state.cmProbe?.let {
        appendLine("cm.probe.ok=${it.ok}")
        appendLine("cm.probe.endpoint=${it.endpoint ?: "(none)"}")
        appendLine("cm.probe.status=${it.status ?: "(none)"}")
    }
    state.cmLogon?.let {
        appendLine("cm.logon.ok=${it.ok}")
        appendLine("cm.logon.endpoint=${it.endpoint ?: "(none)"}")
        appendLine("cm.logon.eresult=${it.eresult}")
        appendLine("cm.logon.extended=${it.eresultExtended}")
        appendLine("cm.logon.steamId=${it.steamId}")
    }
}

private fun buildDepotSummary(state: CAuthSteamDepotState): String = buildString {
    appendLine("status=${state.statusText}")
    appendLine("busy=${state.busy}")
    appendLine("appId=${state.appIdText}")
    appendLine("branch=${state.branch}")
    appendLine("requestCodeText=${state.requestCodeText}")
    appendLine("allFilesOutputRoot=${state.allFilesOutputRoot}")
    appendLine("verifyLocalRoot=${state.verifyLocalRoot}")
    state.branches?.let { appendLine("branches=${it.branches.size}") }
    state.manifests?.let { appendLine("manifests=${it.manifests.size}") }
    state.preflight?.let { appendLine("preflight=${it.depots.size} build=${it.buildId}") }
    resolveSelectedDepotPlatform(state)?.let { appendLine("selectedPlatform=$it") }
    state.depotKey?.let {
        appendLine("depotKey.present=${it.present}")
        appendLine("depotKey.eresult=${it.eresult}")
        appendLine("depotKey.depotId=${it.depotId}")
    }
    state.manifestRequestCode?.let {
        appendLine("requestCode.present=${it.present}")
        appendLine("requestCode.value=${formatUnsignedDecimal(it.requestCode)}")
    }
    state.manifestInfo?.let {
        appendLine("manifestInfo.present=${it.present}")
        appendLine("manifestInfo.depotId=${it.depotId}")
        appendLine("manifestInfo.gid=${it.manifestGid}")
        appendLine("manifestInfo.files=${it.fileCount}")
        appendLine("manifestInfo.chunks=${it.chunkCount}")
    }
    state.manifestFiles?.let {
        appendLine("manifestFiles.printed=${it.printedCount}")
        appendLine("manifestFiles.matched=${it.matchedCount}")
        appendLine("manifestFiles.total=${it.totalCount}")
    }
    state.localVerify?.let {
        appendLine("localVerify.present=${it.present}")
        appendLine("localVerify.clean=${it.clean}")
        appendLine("localVerify.checked=${it.checkedCount}")
        appendLine("localVerify.ok=${it.okCount}")
        appendLine("localVerify.missing=${it.missingCount}")
        appendLine("localVerify.mismatched=${it.mismatchedCount}")
        appendLine("localVerify.sizeOnly=${it.sizeOnlyCount}")
        appendLine("localVerify.filteredOut=${it.filteredOutCount}")
        appendLine("localVerify.total=${it.totalCount}")
    }
    state.downloadTask?.let {
        appendLine("downloadTask.kind=${it.kindLabel}")
        appendLine("downloadTask.active=${it.active}")
        appendLine("downloadTask.finished=${it.finished}")
        appendLine("downloadTask.canceled=${it.canceled}")
        appendLine("downloadTask.succeeded=${it.succeeded}")
        appendLine("downloadTask.phase=${it.phase}")
        appendLine("downloadTask.progress=${it.progressSummary}")
        appendLine("downloadTask.target=${it.target}")
        appendLine("downloadTask.message=${it.message}")
    }
}

private fun buildCloudSummary(state: CAuthSteamCloudState): String = buildString {
    appendLine("status=${state.statusText}")
    appendLine("busy=${state.busy}")
    appendLine("appId=${state.appIdText}")
    appendLine("localRoot=${state.localRoot}")
    appendLine("remoteRoot=${state.remoteRoot}")
    appendLine("conflictPolicy=${state.conflictPolicy}")
    state.fileList?.let {
        appendLine("fileList.ok=${it.ok}")
        appendLine("fileList.eresult=${it.eresult}")
        appendLine("fileList.total=${it.totalFiles}")
        appendLine("fileList.returned=${it.files.size}")
    }
    state.verifyResult?.let {
        appendLine("verify.present=${it.present}")
        appendLine("verify.clean=${it.clean}")
        appendLine("verify.includeExtraLocal=${it.includeExtraLocal}")
        appendLine("verify.checked=${it.checkedCount}")
        appendLine("verify.ok=${it.okCount}")
        appendLine("verify.missing=${it.missingCount}")
        appendLine("verify.mismatched=${it.mismatchedCount}")
        appendLine("verify.sizeOnly=${it.sizeOnlyCount}")
        appendLine("verify.filteredOut=${it.filteredOutCount}")
        appendLine("verify.extraLocal=${it.extraLocalCount}")
        appendLine("verify.total=${it.totalCount}")
        appendLine("verify.message=${it.message}")
    }
    state.operationResult?.let {
        appendLine("result.ok=${it.ok}")
        appendLine("result.direction=${it.direction}")
        appendLine("result.transferred=${it.transferredCount}")
        appendLine("result.deleted=${it.deletedCount}")
        appendLine("result.skipped=${it.skippedCount}")
        appendLine("result.conflicts=${it.conflictCount}")
        appendLine("result.bytes=${it.transferredBytes}")
        appendLine("result.message=${it.message}")
    }
    state.transferTask?.let {
        appendLine("transferTask.kind=${it.kindLabel}")
        appendLine("transferTask.active=${it.active}")
        appendLine("transferTask.finished=${it.finished}")
        appendLine("transferTask.canceled=${it.canceled}")
        appendLine("transferTask.succeeded=${it.succeeded}")
        appendLine("transferTask.phase=${it.phase}")
        appendLine("transferTask.progress=${it.progressSummary}")
        appendLine("transferTask.target=${it.target}")
        appendLine("transferTask.message=${it.message}")
        it.result?.let { result ->
            appendLine("transferTask.result.ok=${result.ok}")
            appendLine("transferTask.result.direction=${result.direction}")
            appendLine("transferTask.result.transferred=${result.transferredCount}")
            appendLine("transferTask.result.deleted=${result.deletedCount}")
            appendLine("transferTask.result.skipped=${result.skippedCount}")
            appendLine("transferTask.result.conflicts=${result.conflictCount}")
            appendLine("transferTask.result.bytes=${result.transferredBytes}")
            appendLine("transferTask.result.message=${result.message}")
        }
    }
}

private enum class ExampleSection(val label: String) {
    Auth("Auth"),
    Depot("Depot"),
    Cloud("Cloud"),
}
