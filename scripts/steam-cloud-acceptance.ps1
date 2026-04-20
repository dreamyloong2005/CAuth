param(
    [Parameter(Mandatory = $true)]
    [uint32]$AppId,

    [Parameter(Mandatory = $true)]
    [UInt64]$SteamId,

    [Parameter(Mandatory = $true)]
    [string]$LocalRoot,

    [string]$RemoteRoot = "",
    [string]$CAuthExe = "",

    [ValidateSet("default", "local-wins", "remote-wins", "newer-wins", "fail", "fail-on-conflict")]
    [string]$ConflictPolicy = "newer-wins",

    [uint32]$Count = 50,
    [uint32]$StartIndex = 0,
    [string]$AccessToken = "",
    [string]$LogPath = "",
    [switch]$DeleteRemoteOrphans,
    [switch]$RunPush,
    [switch]$PlanOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $CAuthExe) {
    $CAuthExe = Join-Path $scriptRoot "..\build\windows-msvc-debug\cauth.exe"
}

function Format-Argument {
    param([string]$Value)

    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }

    return $Value
}

function Write-Log {
    param([string]$Message)

    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $line = "[${timestamp}] $Message"
    Write-Host $line
    Add-Content -LiteralPath $script:ResolvedLogPath -Value $line
}

function Get-SteamCloudArguments {
    param([string]$Subcommand)

    $arguments = @(
        "steam", "cloud", $Subcommand,
        "--steam-id", "$SteamId"
    )

    if ($AccessToken) {
        $arguments += @("--access-token", $AccessToken)
    }

    return $arguments
}

function Invoke-CAuthStep {
    param(
        [string]$Name,
        [string[]]$Arguments
    )

    $renderedCommand = @($script:ResolvedCAuthExe) + ($Arguments | ForEach-Object { Format-Argument $_ })
    Write-Log ""
    Write-Log "=== $Name ==="
    Write-Log ("Command: " + ($renderedCommand -join " "))

    if ($PlanOnly) {
        Write-Log "Plan-only mode: skipped execution."
        return 0
    }

    $output = & $script:ResolvedCAuthExe @Arguments 2>&1
    $exitCode = $LASTEXITCODE

    if ($output) {
        foreach ($line in $output) {
            Write-Log ([string]$line)
        }
    }

    Write-Log "Exit code: $exitCode"
    return $exitCode
}

if (-not $LogPath) {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $LogPath = Join-Path $scriptRoot "..\build\logs\steam-cloud-acceptance-$timestamp.log"
}

$script:ResolvedLogPath = [System.IO.Path]::GetFullPath($LogPath)
$logDirectory = Split-Path -Parent $script:ResolvedLogPath
if ($logDirectory) {
    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
}
Set-Content -LiteralPath $script:ResolvedLogPath -Value ""

$script:ResolvedCAuthExe = [System.IO.Path]::GetFullPath($CAuthExe)
$resolvedLocalRoot = [System.IO.Path]::GetFullPath($LocalRoot)
New-Item -ItemType Directory -Force -Path $resolvedLocalRoot | Out-Null

Write-Log "Steam cloud acceptance started."
Write-Log "AppId=$AppId"
Write-Log "SteamId=$SteamId"
Write-Log "LocalRoot=$resolvedLocalRoot"
Write-Log ("RemoteRoot=" + ($(if ($RemoteRoot) { $RemoteRoot } else { "(empty)" })))
Write-Log "ConflictPolicy=$ConflictPolicy"
Write-Log "Count=$Count StartIndex=$StartIndex"
Write-Log "DeleteRemoteOrphans=$($DeleteRemoteOrphans.IsPresent)"
Write-Log "RunPush=$($RunPush.IsPresent)"
Write-Log "PlanOnly=$($PlanOnly.IsPresent)"
Write-Log "LogPath=$script:ResolvedLogPath"

if (-not $PlanOnly -and -not (Test-Path -LiteralPath $script:ResolvedCAuthExe)) {
    throw "CAuth executable not found: $script:ResolvedCAuthExe"
}

$listArguments = Get-SteamCloudArguments -Subcommand "list"
$listArguments += @(
    "--app-id", "$AppId",
    "--count", "$Count",
    "--start-index", "$StartIndex",
    "--extended-details", "1"
)

if ($RemoteRoot) {
    $listArguments += @("--remote-root", $RemoteRoot)
}

$verifyArguments = Get-SteamCloudArguments -Subcommand "verify"
$verifyArguments += @(
    "--app-id", "$AppId",
    "--local-root", $resolvedLocalRoot
)

if ($RemoteRoot) {
    $verifyArguments += @("--remote-root", $RemoteRoot)
}

$pullArguments = Get-SteamCloudArguments -Subcommand "pull"
$pullArguments += @(
    "--app-id", "$AppId",
    "--local-root", $resolvedLocalRoot,
    "--conflict-policy", $ConflictPolicy,
    "--dry-run"
)

if ($RemoteRoot) {
    $pullArguments += @("--remote-root", $RemoteRoot)
}

$pushDryRunArguments = Get-SteamCloudArguments -Subcommand "push"
$pushDryRunArguments += @(
    "--app-id", "$AppId",
    "--local-root", $resolvedLocalRoot,
    "--conflict-policy", $ConflictPolicy,
    "--dry-run"
)

if ($RemoteRoot) {
    $pushDryRunArguments += @("--remote-root", $RemoteRoot)
}

if ($DeleteRemoteOrphans) {
    $pushDryRunArguments += "--delete-remote-orphans"
}

$steps = @(
    @{ Name = "List remote cloud files"; Arguments = $listArguments },
    @{ Name = "Verify local cloud files"; Arguments = $verifyArguments },
    @{ Name = "Pull cloud save dry-run"; Arguments = $pullArguments },
    @{ Name = "Push cloud save dry-run"; Arguments = $pushDryRunArguments }
)

if ($RunPush) {
    $pushArguments = Get-SteamCloudArguments -Subcommand "push"
    $pushArguments += @(
        "--app-id", "$AppId",
        "--local-root", $resolvedLocalRoot,
        "--conflict-policy", $ConflictPolicy
    )

    if ($RemoteRoot) {
        $pushArguments += @("--remote-root", $RemoteRoot)
    }

    if ($DeleteRemoteOrphans) {
        $pushArguments += "--delete-remote-orphans"
    }

    $steps += @{ Name = "Push cloud save (real upload)"; Arguments = $pushArguments }
}

foreach ($step in $steps) {
    $exitCode = Invoke-CAuthStep -Name $step.Name -Arguments $step.Arguments
    if ($exitCode -ne 0) {
        Write-Log "Acceptance stopped because a step failed."
        exit $exitCode
    }
}

Write-Log ""
Write-Log "Steam cloud acceptance finished successfully."
