# Read-only startup report. Does not load DLLs or change Spotify, settings,
# processes or files. Paths and complete command lines stay out of the output.
$ErrorActionPreference='Stop'
$systemVersion=Join-Path ([Environment]::GetFolderPath('System')) 'version.dll'
$self=[Diagnostics.Process]::GetCurrentProcess()
try {$reportSession=$self.SessionId} finally {$self.Dispose()}
$rows=@()
foreach($entry in @(Get-CimInstance Win32_Process -Filter "Name='Spotify.exe'")) {
    $exe=$entry.ExecutablePath
    $folder=if($exe){Split-Path -Parent $exe}else{$null}
    $role=if($null -eq $entry.CommandLine){'Unknown'}elseif($entry.CommandLine.Contains('--type=')){'Helper'}else{'Main'}
    $localVersion=$null;$localFloggfy=$null;$systemLoaded=$null;$moduleStatus='Unavailable';$locations=@()
    try {
        $process=Get-Process -Id $entry.ProcessId
        try {
            $modules=@($process.Modules)
            $localVersion=if($folder){$false}else{$null};$localFloggfy=if($folder){$false}else{$null};$systemLoaded=$false
            foreach($module in $modules) {
                $name=$module.ModuleName
                if($name -ine 'version.dll' -and $name -ine 'Floggfy.dll'){continue}
                $path=$module.FileName
                if($path -ieq $systemVersion){$systemLoaded=$true;$locations+='System32 version.dll'}
                elseif($folder -and $path -ieq (Join-Path $folder 'version.dll')){$localVersion=$true;$locations+='Adjacent version.dll'}
                elseif($folder -and $path -ieq (Join-Path $folder 'Floggfy.dll')){$localFloggfy=$true;$locations+='Adjacent Floggfy.dll'}
                else{$locations+='Other '+$name}
            }
            $moduleStatus='Read'
        } finally {$process.Dispose()}
    } catch {$moduleStatus='Inaccessible or process exited';$localVersion=$null;$localFloggfy=$null;$systemLoaded=$null;$locations=@()}
    $bootstrap=$null;$ready=$null;$bootstrapInspection='Different or unknown session'
    if($null -ne $entry.SessionId -and $entry.SessionId -eq $reportSession) {try {
        $ready=[Threading.EventWaitHandle]::OpenExisting(('Local\FloggfyStartupReady-'+$entry.ProcessId))
        $bootstrap=$ready.WaitOne(0);$bootstrapInspection='Read'
    } catch [Threading.WaitHandleCannotBeOpenedException] {$bootstrap=$false;$bootstrapInspection='Not present'}
      catch {$bootstrap=$null;$bootstrapInspection='Inaccessible'}
      finally {if($ready){$ready.Dispose()}}}
    $ini=$null
    if($folder){try {$ini=Test-Path -LiteralPath (Join-Path $folder 'SpotifyHistory.ini') -PathType Leaf}catch{}}
    $rows += [pscustomobject]@{
        ProcessId=$entry.ProcessId
        Role=$role
        SessionId=$entry.SessionId
        ExecutablePathLength=if($exe){$exe.Length}else{$null}
        ModuleInspection=$moduleStatus
        AdjacentVersionLoaded=$localVersion
        AdjacentFloggfyLoaded=$localFloggfy
        SystemVersionLoaded=$systemLoaded
        BootstrapReady=$bootstrap
        BootstrapInspection=$bootstrapInspection
        AdjacentIniExists=$ini
        Modules=$locations
    }
}
[pscustomobject]@{
    InspectionSession=$reportSession
    OSVersion=[Environment]::OSVersion.Version.ToString()
    Processes=$rows
    ReadinessMeaning='BootstrapReady confirms initial monitor/settings startup, not successful attachment of every hook.'
} | ConvertTo-Json -Depth 5
