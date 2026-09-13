# Test the real harness's opt-in schedule and child environment without launching it.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$probeTokens = $null
$probeErrors = $null
$probeAst = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot '../scripts/test-recomp-startup.ps1'), [ref]$probeTokens, [ref]$probeErrors)
$script:probeChecks = 0
function Check-ProbeHarness([bool]$Condition, [string]$Message) {
    ++$script:probeChecks
    if (-not $Condition) { throw $Message }
}
Check-ProbeHarness ($probeErrors.Count -eq 0) 'The startup harness must parse.'
foreach ($name in @('InProcessCpuProbeSeconds', 'InProcessCpuProbeAtSeconds')) {
    $parameter = @($probeAst.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq $name })
    Check-ProbeHarness ($parameter.Count -eq 1 -and $parameter[0].DefaultValue.Value -eq 0) 'Probe options must default to zero.'
}
$guards = @($probeAst.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.IfStatementAst] -and
    $_.Extent.Text.StartsWith('if ($InProcessCpuProbeSeconds') -and
    $_.Extent.Text.Contains('throw ') -and -not $_.Extent.Text.Contains('chassisPatchPath')
})
Check-ProbeHarness ($guards.Count -eq 2) 'Both independent probe schedule guards must exist.'
$validate = [scriptblock]::Create(
    'param($InProcessCpuProbeSeconds,$InProcessCpuProbeAtSeconds,$CpuProfileSeconds,$TimeoutSeconds)' + "`n" +
    (($guards | ForEach-Object { $_.Extent.Text }) -join "`n"))
foreach ($case in @(@(0,0,0,30), @(15,40,0,70), @(30,50,0,90))) {
    & $validate @case
    Check-ProbeHarness $true 'A valid schedule was accepted.'
}
foreach ($case in @(@(0,10,0,70), @(15,40,1,70), @(30,51,0,90))) {
    $rejected = $false
    try { & $validate @case } catch { $rejected = $true }
    Check-ProbeHarness $rejected 'Reject orphaned delay, concurrent collectors or an exhausted margin.'
}
$envNode = @($probeAst.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.IfStatementAst] -and
    $_.Extent.Text.StartsWith('if ($InProcessCpuProbeSeconds') -and
    $_.Extent.Text.Contains('EnvironmentVariables')
})
Check-ProbeHarness ($envNode.Count -eq 1) 'Only the explicit opt-in populates probe environment.'
$setEnvironment = [scriptblock]::Create($envNode[0].Extent.Text)
$start = [pscustomobject]@{ EnvironmentVariables = @{} }
$InProcessCpuProbeSeconds = 0
$InProcessCpuProbeAtSeconds = 0
& $setEnvironment
Check-ProbeHarness ($start.EnvironmentVariables.Count -eq 0) 'Disabled probe must set no child variables.'
$InProcessCpuProbeSeconds = 15
$InProcessCpuProbeAtSeconds = 40
& $setEnvironment
Check-ProbeHarness ($start.EnvironmentVariables.Count -eq 2) 'Enabled probe must set exactly its two variables.'
Check-ProbeHarness ($start.EnvironmentVariables.STATICRECOMP_CPU_PROBE_SECONDS -ceq '15') 'Duration must be explicit.'
Check-ProbeHarness ($start.EnvironmentVariables.STATICRECOMP_CPU_PROBE_DELAY_SECONDS -ceq '40') 'Delay must be explicit.'
Write-Host "In-process startup harness checks passed: $script:probeChecks"
