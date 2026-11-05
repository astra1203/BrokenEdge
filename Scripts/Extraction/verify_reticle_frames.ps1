param([Parameter(Mandatory=$true)][string]$RunDirectory)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$run = (Resolve-Path -LiteralPath $RunDirectory).Path
$results = @()
foreach ($gun in @('BoltSniper', 'PumpShotgun')) {
    $directory = Join-Path $run "A-$gun-reticle-frames"
    $samples = @()
    foreach ($file in (Get-ChildItem -LiteralPath $directory -Filter '*.png' | Sort-Object Name)) {
        $bitmap = [System.Drawing.Bitmap]::new($file.FullName)
        try {
            $cx = [int]($bitmap.Width / 2)
            $cy = [int]($bitmap.Height / 2)
            $gaps = @()
            foreach ($axis in @(0, 1)) {
                foreach ($sign in @(-1, 1)) {
                    $start = 0
                    $length = 0
                    $gap = 0
                    foreach ($distance in 3..80) {
                        $x = $cx; $y = $cy
                        if ($axis -eq 0) { $x += $sign * $distance } else { $y += $sign * $distance }
                        $color = $bitmap.GetPixel($x, $y)
                        $white = $color.R -gt 235 -and $color.G -gt 235 -and $color.B -gt 235
                        if ($white) {
                            if ($length -eq 0) { $start = $distance }
                            $length++
                            if ($length -ge 4) { $gap = $start; break }
                        } else { $length = 0 }
                    }
                    $gaps += $gap
                }
            }
            # A real reticle has opposite, symmetric line segments. Reject
            # isolated bright scene pixels and transient impact particles.
            if (($gaps | Where-Object { $_ -eq 0 }).Count -eq 0 -and
                [math]::Abs($gaps[0] - $gaps[1]) -le 2 -and
                [math]::Abs($gaps[2] - $gaps[3]) -le 2) {
                $samples += [PSCustomObject]@{
                    file = $file.Name
                    mean_gap = ($gaps | Measure-Object -Average).Average
                    gaps = $gaps
                    sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                }
            }
        } finally { $bitmap.Dispose() }
    }
    if ($samples.Count -lt 5) { throw "$gun has fewer than five measurable reticle frames" }
    $baseline = $samples[0]
    $peak = $samples | Sort-Object mean_gap -Descending | Select-Object -First 1
    $recovery = $samples[-1]
    if ($peak.mean_gap -lt $baseline.mean_gap + 3) { throw "$gun has no measured three-pixel expansion" }
    if ($recovery.mean_gap -gt $baseline.mean_gap + 1.5) { throw "$gun does not recover to its baseline" }
    foreach ($entry in @(@{name='baseline';row=$baseline}, @{name='peak';row=$peak}, @{name='recovery';row=$recovery})) {
        $destination = Join-Path $run "A-$gun-reticle-$($entry.name).png"
        if (Test-Path -LiteralPath $destination) { throw "Evidence already exists: $destination" }
        Copy-Item -LiteralPath (Join-Path $directory $entry.row.file) -Destination $destination
    }
    $results += [PSCustomObject]@{gun=$gun;baseline=$baseline;peak=$peak;recovery=$recovery;frames=$samples}
}
$results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $run 'ReticlePixels.json') -Encoding utf8
$results | Select-Object gun,@{n='baseline_px';e={$_.baseline.mean_gap}},@{n='peak_px';e={$_.peak.mean_gap}},@{n='recovery_px';e={$_.recovery.mean_gap}} | Format-Table
