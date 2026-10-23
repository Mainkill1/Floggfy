param([Parameter(Mandatory=$true)][string]$ReportScript)
$ErrorActionPreference='Stop'
# Simulate WMI withholding the executable path while ordinary module inspection
# still succeeds. Use real module APIs against this test's own process.
$script:fixtureSession=[Diagnostics.Process]::GetCurrentProcess().SessionId
function Get-CimInstance {
    param([string]$ClassName,[string]$Filter)
    [pscustomobject]@{ExecutablePath=$null;CommandLine='Spotify.exe';ProcessId=$PID;SessionId=$script:fixtureSession}
}
$report=(& $ReportScript | ConvertFrom-Json)
$row=@($report.Processes)[0]
if($row.ModuleInspection -ne 'Read'){throw 'Self-process module inspection failed'}
if($null -ne $row.AdjacentVersionLoaded -or $null -ne $row.AdjacentFloggfyLoaded){throw 'Unknown executable folder must not be reported as absent adjacent DLLs'}
if($null -eq $row.SystemVersionLoaded){throw 'System library inspection remains known independently of executable folder'}
Write-Output 'PASS: startup diagnostics preserve unknown adjacent DLL location when executable path is unavailable'

$script:fixtureSession+=1
$report=(& $ReportScript | ConvertFrom-Json)
$row=@($report.Processes)[0]
if($null -ne $row.BootstrapReady -or $row.BootstrapInspection -ne 'Different or unknown session'){throw 'Session-local event in another session must remain unknown'}
Write-Output 'PASS: startup readiness is unknown across Windows session namespaces'
