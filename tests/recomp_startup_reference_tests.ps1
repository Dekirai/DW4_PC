# Exercise the actual reference-path selection without launching the startup harness.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$referenceTokens = $null
$referenceErrors = $null
$referenceAst = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot '../scripts/test-recomp-startup.ps1'),[ref]$referenceTokens,[ref]$referenceErrors)
$script:referenceChecks = 0
function Check-Reference([bool]$Condition,[string]$Message) {
    ++$script:referenceChecks
    if (-not $Condition) { throw $Message }
}
Check-Reference ($referenceErrors.Count -eq 0) 'The harness must parse.'
$referenceBranches = @($referenceAst.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.IfStatementAst] -and $_.Extent.Text.StartsWith('if ($ReferenceBuildDirectory')
})
Check-Reference ($referenceBranches.Count -eq 2) 'Reference mode must retain its ownership guard and explicit path selection.'
$guard = [scriptblock]::Create($referenceBranches[0].Extent.Text)
$ReferenceBuildDirectory = 'preserved reference'
foreach ($case in @(@($false,$true,'example.exe'),@($true,$false,'example.exe'),@($true,$true,''))) {
    $UnifiedExecutable=$case[0]; $UseBundledModule=$case[1]; $Runner=$case[2]
    $rejected=$false
    try { & $guard } catch { $rejected=$true }
    Check-Reference $rejected 'Reference mode requires a specific packaged unified executable and module.'
}
$UnifiedExecutable=$true; $UseBundledModule=$true; $Runner='example.exe'
& $guard
$repoRoot = Split-Path -Parent $PSScriptRoot
$ReferenceBuildDirectory = Join-Path $repoRoot ('work/recomp-reference-tests/'+[Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($ReferenceBuildDirectory) | Out-Null
$selectPaths=[scriptblock]::Create($referenceBranches[1].Extent.Text)
. $selectPaths
Check-Reference ($buildRecordPath -eq (Join-Path $ReferenceBuildDirectory 'build-record.json')) 'Use the preserved build record.'
Check-Reference ($runtimePatchPath -eq (Join-Path $ReferenceBuildDirectory 'moderngekko-local-runtime.patch')) 'Use the matching runtime patch.'
Check-Reference ($chassisPatchPath -eq (Join-Path $ReferenceBuildDirectory 'recompcore-headless-shader-progress.patch')) 'Use the matching chassis patch.'
$abiNode = @($referenceAst.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -eq '$abiReportPath'
})
Check-Reference ($abiNode.Count -eq 1) 'The ABI report must be selected once before its existing hash check.'
$selectAbi=[scriptblock]::Create($abiNode[0].Extent.Text)
$buildRecord=[pscustomobject]@{abi_report_path=(Join-Path $repoRoot 'work/current-abi-placeholder.json')}
. $selectAbi
Check-Reference ($abiReportPath -eq (Join-Path $ReferenceBuildDirectory 'abi.json')) 'A reference run must use its preserved ABI report.'
$ReferenceBuildDirectory=''
. $selectAbi
Check-Reference ($abiReportPath -eq $buildRecord.abi_report_path) 'Ordinary runs must keep the current build ABI report.'
Write-Host "Reference startup path checks passed: $script:referenceChecks"
