# Parse preserved in-process CPU diagnostics only. This file never launches a process.
function Get-RecompInprocessProbe {
    param([AllowNull()][AllowEmptyCollection()][string[]]$Lines, [bool]$Enabled = $false)

    $rawLines = [Collections.Generic.List[string]]::new()
    foreach ($line in $Lines) {
        if ($null -ne $line -and $line -match '\[staticrecomp\]\s+cpu-probe') { $rawLines.Add($line) }
    }
    $result = [ordered]@{
        enabled=$Enabled; valid=$false; complete=$false; reason=$null
        report=$null; sites=@(); raw_lines=$rawLines.ToArray()
    }
    if (-not $Enabled) {
        if ($rawLines.Count -eq 0) {
            $result.valid = $true
            $result.reason = 'The in-process CPU probe was disabled.'
        } else { $result.reason = 'Unexpected in-process CPU probe output while the probe was disabled.' }
        return [pscustomobject]$result
    }

    $summaryFields = @('complete','thread_cpu_valid','thread_id','delay_ns','duration_ns',
        'start_ns','end_ns','cpu_100ns','opportunities','samples','failures','overflow','seed',
        'stride','calibration_samples','calibration_cycles','calibration_wall_ns')
    $siteFields = @('key','samples','cycles','wall_ns','zero_cycles','failures')
    $culture = [Globalization.CultureInfo]::InvariantCulture

    function Read-ProbeFields([string]$Text, [string[]]$Required, [bool]$IsSite) {
        $values = [ordered]@{}
        foreach ($token in ($Text -split '\s+')) {
            if ($token -cnotmatch '^([a-z][a-z0-9_]*)=([^=\s]+)$') { throw "Malformed probe token: $token." }
            $name = $Matches[1]
            $value = $Matches[2]
            if ($name -cnotin $Required) { throw "Unknown probe field: $name." }
            if ($values.Contains($name)) { throw "Duplicate probe field: $name." }
            if ($IsSite -and $name -ceq 'key') {
                if ($value -cnotmatch '^[0-9a-fA-F]{8}$') { throw 'A probe site key must contain exactly eight hexadecimal digits.' }
                $values[$name] = $value.ToLowerInvariant()
            } else {
                [uint64]$parsed = 0
                if ($value -cnotmatch '^[0-9]+$' -or -not [uint64]::TryParse($value,
                    [Globalization.NumberStyles]::None,$culture,[ref]$parsed)) {
                    throw "Invalid unsigned 64-bit probe field: $name."
                }
                $values[$name] = $parsed
            }
        }
        foreach ($name in $Required) {
            if (-not $values.Contains($name)) { throw "Missing probe field: $name." }
        }
        return $values
    }

    try {
        $summary = $null
        $sites = [Collections.Generic.List[object]]::new()
        $keys = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($line in $rawLines) {
            $text = $line.Trim()
            if ($text -cmatch '^\[staticrecomp\] cpu-probe: (.+)$') {
                if ($null -ne $summary) { throw 'Duplicate in-process CPU probe summary.' }
                $summary = Read-ProbeFields $Matches[1] $summaryFields $false
            } elseif ($text -cmatch '^\[staticrecomp\] cpu-probe-site: (.+)$') {
                $site = Read-ProbeFields $Matches[1] $siteFields $true
                if (-not $keys.Add($site.key)) { throw "Duplicate probe site key: $($site.key)." }
                if ($site.samples -eq 0) { throw "Probe site $($site.key) has no samples." }
                if ($site.zero_cycles -gt $site.samples -or $site.failures -gt $site.samples) {
                    throw "Probe site $($site.key) has inconsistent sample counts."
                }
                $sites.Add([pscustomobject]$site)
            } else { throw 'Malformed in-process CPU probe output line.' }
        }
        if ($null -eq $summary) { throw 'Missing in-process CPU probe summary.' }
        if ($summary.complete -ne 1) { throw 'The in-process CPU probe window is incomplete.' }
        if ($summary.thread_cpu_valid -ne 1 -or $summary.thread_id -eq 0 -or $summary.thread_id -gt [uint32]::MaxValue) {
            throw 'The in-process CPU probe has no valid owner-thread CPU measurement.'
        }
        if ($summary.delay_ns -gt 85000000000 -or $summary.stride -lt 1 -or $summary.stride -gt 1048576 -or
            $summary.seed -lt 1 -or $summary.seed -gt [uint32]::MaxValue -or
            $summary.calibration_samples -lt 1 -or $summary.calibration_samples -gt 16) {
            throw 'The in-process CPU probe configuration or calibration is outside runtime bounds.'
        }
        if ($summary.duration_ns -lt 1000000000 -or $summary.duration_ns -gt 30000000000) {
            throw 'The requested probe duration is outside one through thirty seconds.'
        }
        # The deadline is fixed before the first sample. Calibration/polling can
        # make the observed start later, so end-start need not equal duration.
        $deadline = [decimal]$summary.delay_ns + [decimal]$summary.duration_ns
        if ($summary.start_ns -lt $summary.delay_ns -or [decimal]$summary.start_ns -ge $deadline -or
            $summary.end_ns -le $summary.start_ns -or [decimal]$summary.end_ns -lt $deadline) {
            throw 'The in-process CPU probe did not cover its requested window.'
        }
        if ($summary.samples -eq 0 -or $summary.opportunities -lt $summary.samples -or $sites.Count -eq 0) {
            throw 'The in-process CPU probe has missing or inconsistent sample counts.'
        }
        if ($summary.failures -ne 0 -or $summary.overflow -ne 0) {
            throw 'The in-process CPU probe reported measurement failures or site overflow.'
        }
        [decimal]$siteSamples = 0
        foreach ($site in $sites) {
            if ($site.failures -ne 0) { throw "Probe site $($site.key) reported measurement failures." }
            if (($site.cycles -eq 0) -ne ($site.zero_cycles -eq $site.samples)) {
                throw "Probe site $($site.key) has inconsistent zero-cycle observations."
            }
            $siteSamples += [decimal]$site.samples
        }
        if ($siteSamples -ne [decimal]$summary.samples) { throw 'Probe site samples do not sum to the summary sample count.' }
        $result.valid = $true
        $result.complete = $true
        $result.report = [pscustomobject]$summary
        $result.sites = $sites.ToArray()
    } catch { $result.reason = $_.Exception.Message }
    return [pscustomobject]$result
}
