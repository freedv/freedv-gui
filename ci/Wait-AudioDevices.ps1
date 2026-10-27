<#
  .SYNOPSIS
  Blocks until the named Windows audio endpoints are enumerable, or fails with
  diagnostics after a timeout.

  .DESCRIPTION
  FreeDV enumerates audio devices through the Core Audio MMDevice API and aborts
  with a fatal "device cannot be found" message box if a configured device is
  missing when a test starts. On CI the virtual cables (VB-Cable / VAC) sometimes
  take a while to expose their render / capture endpoints after the audio service
  is (re)started, or briefly drop out of enumeration after another process
  releases them -- which makes the PGO profile collection job fail intermittently.

  This script polls "Get-AudioDevice -List" (from AudioDeviceCmdlets, the same
  active-endpoint enumeration surface FreeDV uses) until every required endpoint
  is present. It restarts the Windows audio stack once up front and once more
  partway through as a nudge. On timeout it prints a full device dump and throws.

  .PARAMETER Playback
  Names of required playback (render) endpoints.

  .PARAMETER Recording
  Names of required recording (capture) endpoints.

  .PARAMETER TimeoutSeconds
  Total time to wait for all endpoints. Default 180.

  .PARAMETER NoRestart
  Do not restart the audio services (just poll).

  .EXAMPLE
  PS> ./ci/Wait-AudioDevices.ps1 `
        -Playback  "Speakers (VB-Audio Virtual Cable)", "Line 1 (Virtual Audio Cable)" `
        -Recording "CABLE Output (VB-Audio Virtual Cable)", "Line 1 (Virtual Audio Cable)"
#>
param (
    [string[]] $Playback = @(),
    [string[]] $Recording = @(),
    [int] $TimeoutSeconds = 180,
    [switch] $NoRestart
)

$ErrorActionPreference = 'Stop'

# The PowerShell Gallery download occasionally fails on the runners, so
# retry a few times before giving up.
if (-not (Get-Module -ListAvailable -Name AudioDeviceCmdlets)) {
    $attempt = 1
    while ($true) {
        try {
            Install-Module -Name AudioDeviceCmdlets -Force -Confirm:$false -Scope CurrentUser
            break
        } catch {
            if ($attempt -ge 5) { throw }
            Write-Host "Installing AudioDeviceCmdlets failed (attempt $attempt): $_"
            Start-Sleep -Seconds (10 * $attempt)
            $attempt++
        }
    }
}
Import-Module AudioDeviceCmdlets

$Playback  = @($Playback  | Where-Object { $_ } | Select-Object -Unique)
$Recording = @($Recording | Where-Object { $_ } | Select-Object -Unique)

function Get-MissingEndpoints {
    $devices   = Get-AudioDevice -List
    $havePlay  = @($devices | Where-Object { $_.Type -eq 'Playback'  } | ForEach-Object { $_.Name })
    $haveRec   = @($devices | Where-Object { $_.Type -eq 'Recording' } | ForEach-Object { $_.Name })

    $missing = @()
    foreach ($name in $Playback)  { if ($havePlay -notcontains $name) { $missing += "playback : $name" } }
    foreach ($name in $Recording) { if ($haveRec  -notcontains $name) { $missing += "recording: $name" } }
    return $missing
}

function Restart-AudioStack {
    if ($NoRestart) { return }
    Write-Host "Restarting Windows audio services (AudioEndpointBuilder, audiosrv)..."
    # AudioEndpointBuilder owns endpoint enumeration; audiosrv depends on it and
    # is restarted with it. -Force also restarts dependent services.
    foreach ($svc in 'AudioEndpointBuilder', 'audiosrv') {
        try   { Restart-Service -Name $svc -Force -ErrorAction Stop }
        catch { Write-Host "  ($svc could not be restarted: $_)" }
    }
    Start-Sleep -Seconds 3
}

function Show-DeviceDump {
    Write-Host "--- Get-AudioDevice -List ---"
    Get-AudioDevice -List | Format-Table -AutoSize Index, Default, Type, Name | Out-String | Write-Host
    Write-Host "--- Win32_SoundDevice ---"
    Get-CimInstance Win32_SoundDevice | Format-Table -AutoSize Name, Status, StatusInfo | Out-String | Write-Host
    Write-Host "--- AudioEndpoint PnP devices ---"
    Get-PnpDevice -Class AudioEndpoint -ErrorAction SilentlyContinue |
        Format-Table -AutoSize Status, FriendlyName | Out-String | Write-Host
}

Write-Host "Waiting up to $TimeoutSeconds s for audio endpoints:"
$Playback  | ForEach-Object { Write-Host "  [playback ] $_" }
$Recording | ForEach-Object { Write-Host "  [recording] $_" }

Restart-AudioStack

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$nudged   = $false
$missing  = Get-MissingEndpoints

while ($missing.Count -gt 0 -and (Get-Date) -lt $deadline) {
    $remaining = ($deadline - (Get-Date)).TotalSeconds

    if (-not $nudged -and $remaining -lt ($TimeoutSeconds / 2)) {
        Write-Host "Still missing after half the timeout; nudging the audio stack:"
        $missing | ForEach-Object { Write-Host "  $_" }
        Restart-AudioStack
        $nudged = $true
    }

    Start-Sleep -Seconds 3
    $missing = Get-MissingEndpoints
}

if ($missing.Count -eq 0) {
    Write-Host "All required audio endpoints are present."
    Get-AudioDevice -List | Format-Table -AutoSize Index, Default, Type, Name | Out-String | Write-Host
    exit 0
}

Write-Host "::error::Timed out waiting for audio endpoints; still missing:"
$missing | ForEach-Object { Write-Host "  $_" }
Show-DeviceDump
throw "Required audio endpoints not available after $TimeoutSeconds s"
