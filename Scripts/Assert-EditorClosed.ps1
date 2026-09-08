param([Parameter(Mandatory = $true)][string]$ProjectFile)
$ErrorActionPreference = 'Stop'
$projectPath = [IO.Path]::GetFullPath($ProjectFile).Replace('/', '\')
$editors = Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe' OR Name='UnrealEditor-Cmd.exe'"
foreach ($editor in $editors) {
    if (!$editor.CommandLine) { throw "Cannot inspect editor process $($editor.ProcessId)." }
    $commandLine = $editor.CommandLine.Replace('/', '\')
    if ($commandLine.IndexOf($projectPath, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        Write-Output "[ERROR] Save and close the editor for $projectPath (PID $($editor.ProcessId))."
        exit 1
    }
}
exit 0
