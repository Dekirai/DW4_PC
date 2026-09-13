# Synthetic parser fixtures only. No game, compiler, profiler or service starts.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../scripts/recomp-inprocess-probe.ps1')
$script:probeChecks = 0
function Check-Probe([bool]$Condition, [string]$Message) {
    ++$script:probeChecks
    if (-not $Condition) { throw $Message }
}
function Copy-ProbeFields($Fields) {
    $copy = [ordered]@{}
    foreach ($name in $Fields.Keys) { $copy[$name] = $Fields[$name] }
    return $copy
}
function Format-ProbeLine([string]$Kind, $Fields) {
    $tokens = foreach ($name in $Fields.Keys) { $name + '=' + [string]$Fields[$name] }
    return '[staticrecomp] ' + $Kind + ': ' + ($tokens -join ' ')
}
function Test-RejectedProbe([string[]]$Lines, [string]$Name, [bool]$Enabled = $true) {
    $result = Get-RecompInprocessProbe -Lines $Lines -Enabled $Enabled
    Check-Probe (-not $result.valid -and -not $result.complete -and $null -eq $result.report -and
        $result.sites.Count -eq 0 -and -not [string]::IsNullOrWhiteSpace($result.reason)) $Name
}

$header = [ordered]@{
    complete='1'; thread_cpu_valid='1'; thread_id='1234'; delay_ns='2000000000'; duration_ns='1000000000'
    start_ns='2000000100'; end_ns='3000000000'; cpu_100ns='9876543'; opportunities='10000'; samples='8'
    failures='0'; overflow='0'; seed='123456789'; stride='1024'; calibration_samples='16'
    calibration_cycles='6789'; calibration_wall_ns='12345'
}
$first = [ordered]@{key='8040154c';samples='5';cycles='1000';wall_ns='600';zero_cycles='1';failures='0'}
$second = [ordered]@{key='8025d318';samples='3';cycles='400';wall_ns='250';zero_cycles='0';failures='0'}
$headerLine = Format-ProbeLine 'cpu-probe' $header
$firstLine = Format-ProbeLine 'cpu-probe-site' $first
$secondLine = Format-ProbeLine 'cpu-probe-site' $second
$good = @('Ordinary unrelated runtime output.', $headerLine, $firstLine, $secondLine, '')
$before = $good | ConvertTo-Json
$result = Get-RecompInprocessProbe -Lines $good -Enabled $true
Check-Probe ($result.valid -and $result.complete -and $null -eq $result.reason) 'A valid completed probe was rejected.'
Check-Probe ($result.report.samples -is [uint64] -and $result.report.samples -eq 8 -and $result.sites.Count -eq 2) 'Typed summary/site counters were lost.'
Check-Probe ($result.sites[0].key -ceq '8040154c' -and $result.sites[0].zero_cycles -eq 1) 'Site identity or valid zero-cycle observations changed.'
Check-Probe ($result.raw_lines.Count -eq 3 -and $result.raw_lines[0] -ceq $headerLine) 'Raw diagnostic lines were not preserved separately.'
Check-Probe (($good | ConvertTo-Json) -ceq $before) 'Parsing mutated the supplied log lines.'
Check-Probe (($result | ConvertTo-Json -Depth 6 | ConvertFrom-Json).sites.Count -eq 2) 'The result cannot be serialized for harness evidence.'

$disabled = Get-RecompInprocessProbe -Lines @('Unrelated stderr.') -Enabled $false
Check-Probe ($disabled.valid -and -not $disabled.complete -and -not $disabled.enabled -and $disabled.raw_lines.Count -eq 0) 'A disabled clean probe did not retain its distinct status.'
Test-RejectedProbe $good 'Unexpected probe output was accepted while disabled.' $false
Test-RejectedProbe @($firstLine) 'An unexpected orphan site was accepted while disabled.' $false
Test-RejectedProbe @() 'Missing enabled probe output was accepted.'
Test-RejectedProbe @($firstLine,$secondLine) 'An absent summary was accepted.'
Test-RejectedProbe @($headerLine) 'An absent site set was accepted.'
Test-RejectedProbe @($headerLine,$headerLine,$firstLine,$secondLine) 'A duplicate summary was accepted.'
Test-RejectedProbe @($headerLine,$firstLine,$firstLine,$secondLine) 'A duplicate site was accepted.'
Test-RejectedProbe @($headerLine,$firstLine,$firstLine.Replace('8040154c','8040154C'),$secondLine) 'A case-variant duplicate site was accepted.'

foreach ($field in $header.Keys) {
    $missing = Copy-ProbeFields $header
    $missing.Remove($field)
    Test-RejectedProbe @((Format-ProbeLine 'cpu-probe' $missing),$firstLine,$secondLine) "Missing summary field $field was accepted."
    Test-RejectedProbe @(($headerLine + ' ' + $field + '=' + $header[$field]),$firstLine,$secondLine) "Duplicate summary field $field was accepted."
    foreach ($bad in @('-1','+1','1.5','1e3','NaN','1,000','18446744073709551616')) {
        $malformed = Copy-ProbeFields $header
        $malformed[$field] = $bad
        Test-RejectedProbe @((Format-ProbeLine 'cpu-probe' $malformed),$firstLine,$secondLine) "Malformed summary $field=$bad was accepted."
    }
}
foreach ($field in $first.Keys) {
    $missing = Copy-ProbeFields $first
    $missing.Remove($field)
    Test-RejectedProbe @($headerLine,(Format-ProbeLine 'cpu-probe-site' $missing),$secondLine) "Missing site field $field was accepted."
    Test-RejectedProbe @($headerLine,($firstLine + ' ' + $field + '=' + $first[$field]),$secondLine) "Duplicate site field $field was accepted."
    if ($field -ceq 'key') { $invalid = @('8040154','08040154c','0x8040154c','8040154g','-040154c') }
    else { $invalid = @('-1','+1','1.5','1e3','NaN','1,000','18446744073709551616') }
    foreach ($bad in $invalid) {
        $malformed = Copy-ProbeFields $first
        $malformed[$field] = $bad
        Test-RejectedProbe @($headerLine,(Format-ProbeLine 'cpu-probe-site' $malformed),$secondLine) "Malformed site $field=$bad was accepted."
    }
}
foreach ($malformedLine in @('[staticrecomp] cpu-probe:', '[staticrecomp] cpu-probe-site:',
    '[staticrecomp] cpu-probe broken', '[staticrecomp] cpu-probe-extra: samples=1',
    ('Prefix ' + $headerLine), ($headerLine + ' orphan'), ($headerLine + ' extra=1'),
    ($headerLine + ' samples=1=2'), $headerLine.Replace('complete=1','Complete=1'),
    $headerLine.Replace('complete=1',('complete=' + [char]0xFF11)))) {
    Test-RejectedProbe @($malformedLine,$firstLine,$secondLine) 'A malformed summary line was accepted.'
}
Test-RejectedProbe @($headerLine,($firstLine + ' extra=1'),$secondLine) 'An unknown site field was accepted.'

$invalidHeaderCases = @(
    @{complete='0'}, @{complete='2'}, @{thread_cpu_valid='0'}, @{thread_cpu_valid='2'}, @{thread_id='0'},
    @{thread_id='4294967296'}, @{seed='0'}, @{seed='4294967296'}, @{stride='0'}, @{stride='1048577'},
    @{calibration_samples='0'}, @{calibration_samples='17'}, @{delay_ns='85000000001'},
    @{duration_ns='0'}, @{duration_ns='999999999'}, @{duration_ns='30000000001'},
    @{start_ns='1999999999'}, @{start_ns='3000000000'}, @{end_ns='2999999999'},
    @{end_ns='2000000100'}, @{end_ns='2000000000'}, @{samples='0'}, @{opportunities='7'},
    @{failures='1'}, @{overflow='1'}, @{samples='9'}, @{samples='7'},
    @{delay_ns='18446744073709551615';start_ns='18446744073709551615';end_ns='18446744073709551615'}
)
foreach ($changes in $invalidHeaderCases) {
    $changed = Copy-ProbeFields $header
    foreach ($name in $changes.Keys) { $changed[$name] = $changes[$name] }
    Test-RejectedProbe @((Format-ProbeLine 'cpu-probe' $changed),$firstLine,$secondLine) ('Invalid window/count combination was accepted: ' + ($changes | ConvertTo-Json -Compress))
}
foreach ($changes in @(@{samples='0'},@{zero_cycles='6'},@{failures='6'},@{failures='1'},
    @{cycles='0'},@{zero_cycles='5'})) {
    $changed = Copy-ProbeFields $first
    foreach ($name in $changes.Keys) { $changed[$name] = $changes[$name] }
    Test-RejectedProbe @($headerLine,(Format-ProbeLine 'cpu-probe-site' $changed),$secondLine) 'Invalid site sample/failure relationships were accepted.'
}

# Report order is not part of the contract, and late polling can end after the deadline.
$late = Copy-ProbeFields $header
$late.end_ns = '3000000500'
$lateResult = Get-RecompInprocessProbe @($secondLine,(Format-ProbeLine 'cpu-probe' $late),$firstLine) $true
Check-Probe ($lateResult.valid -and $lateResult.report.end_ns -eq 3000000500) 'Valid reordered lines or delayed completion were rejected.'
$zeroKey = Copy-ProbeFields $first
$zeroKey.key = '00000000'
Check-Probe ((Get-RecompInprocessProbe @($headerLine,(Format-ProbeLine 'cpu-probe-site' $zeroKey),$secondLine) $true).valid) 'A valid reserved/zero site key was rejected.'
$uppercase = Copy-ProbeFields $first
$uppercase.key = '8040154C'
Check-Probe ((Get-RecompInprocessProbe @($headerLine,(Format-ProbeLine 'cpu-probe-site' $uppercase),$secondLine) $true).sites[0].key -ceq '8040154c') 'Uppercase hexadecimal identity was not normalized.'

# Runtime configuration boundaries are inclusive, and all-zero measurements stay explicit.
$bounds = Copy-ProbeFields $header
$bounds.thread_id = '4294967295'
$bounds.seed = '4294967295'
$bounds.stride = '1048576'
$bounds.delay_ns = '85000000000'
$bounds.duration_ns = '30000000000'
$bounds.start_ns = '85000000001'
$bounds.end_ns = '115000000000'
Check-Probe ((Get-RecompInprocessProbe @((Format-ProbeLine 'cpu-probe' $bounds),$firstLine,$secondLine) $true).valid) 'Inclusive runtime upper bounds were rejected.'
$bounds.seed = '1'
$bounds.stride = '1'
$bounds.calibration_samples = '1'
$zeroCycles = Copy-ProbeFields $first
$zeroCycles.zero_cycles = '5'
$zeroCycles.cycles = '0'
Check-Probe ((Get-RecompInprocessProbe @((Format-ProbeLine 'cpu-probe' $bounds),
    (Format-ProbeLine 'cpu-probe-site' $zeroCycles),$secondLine) $true).valid) 'Valid minimum configuration or explicit all-zero cycle observations were rejected.'

# Decimal arithmetic must preserve UInt64 precision near counter limits.
$large = Copy-ProbeFields $header
$large.samples = '18446744073709551615'
$large.opportunities = '18446744073709551615'
$largeFirst = Copy-ProbeFields $first
$largeFirst.samples = '18446744073709551614'
$largeSecond = Copy-ProbeFields $second
$largeSecond.samples = '1'
$largeResult = Get-RecompInprocessProbe @((Format-ProbeLine 'cpu-probe' $large),
    (Format-ProbeLine 'cpu-probe-site' $largeFirst),(Format-ProbeLine 'cpu-probe-site' $largeSecond)) $true
Check-Probe ($largeResult.valid -and $largeResult.report.samples -eq [uint64]::MaxValue -and
    $largeResult.sites[0].samples -eq [uint64]::Parse('18446744073709551614')) 'Large counters lost precision.'
$largeSecond.samples = '2'
Test-RejectedProbe @((Format-ProbeLine 'cpu-probe' $large),(Format-ProbeLine 'cpu-probe-site' $largeFirst),
    (Format-ProbeLine 'cpu-probe-site' $largeSecond)) 'A sample-sum overflow was silently wrapped.'

Write-Output "$script:probeChecks in-process CPU probe parser checks passed. No runtime was launched."
