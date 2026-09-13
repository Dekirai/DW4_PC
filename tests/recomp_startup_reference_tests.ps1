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

# Evaluate the actual layout selectors for preserved and current build records.
# Unknown paths must fail before launching or writing a profile.
function Select-LayoutStatements([string]$Variable,[string]$Property) {
    $nodes = @($referenceAst.EndBlock.Statements | Where-Object {
        ($_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -eq $Variable) -or
        ($_ -is [Management.Automation.Language.IfStatementAst] -and
          $_.Extent.Text.StartsWith('if ($buildRecord.PSObject.Properties[' + "'$Property'" + '])'))
    })
    Check-Reference ($nodes.Count -eq 2) "Expected one initializer and one guard for $Property."
    return [scriptblock]::Create(($nodes | ForEach-Object { $_.Extent.Text }) -join "`n")
}
$moduleLayout = Select-LayoutStatements '$packagedModuleRelativePath' 'packaged_module_relative_path'
$configLayout = Select-LayoutStatements '$runtimeConfigFilename' 'runtime_config_filename'
$buildRecord = [pscustomobject]@{}
. $moduleLayout
. $configLayout
Check-Reference ($packagedModuleRelativePath -eq 'gGDJEB2_recomp.dll') 'Preserved module layout changed.'
Check-Reference ($runtimeConfigFilename -eq 'Dolphin.ini') 'Preserved profile filename changed.'
$buildRecord = [pscustomobject]@{packaged_module_relative_path='data/game.dll';runtime_config_filename='Runtime.ini'}
. $moduleLayout
. $configLayout
Check-Reference ($packagedModuleRelativePath -eq 'data/game.dll') 'Current package module was not selected.'
Check-Reference ($runtimeConfigFilename -eq 'Runtime.ini') 'Current profile filename was not selected.'
foreach ($invalid in @('../outside.dll','C:/outside.dll','', $null)) {
    $buildRecord = [pscustomobject]@{packaged_module_relative_path=$invalid;runtime_config_filename=$invalid}
    foreach ($selector in @($moduleLayout,$configLayout)) {
        $rejected=$false
        try { & $selector } catch { $rejected=$true }
        Check-Reference $rejected 'Unknown layout fields must be rejected, not combined with trusted roots.'
    }
}
Write-Host "Reference startup path checks passed: $script:referenceChecks"
