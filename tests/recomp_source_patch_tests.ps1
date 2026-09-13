$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Load only these two trusted definitions. Dot-sourcing the complete build
# script would configure or build the runtime, which this test must never do.
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $repoRoot 'scripts/build-recomp.ps1'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($buildScript,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw 'The trusted build script has parse errors.' }
foreach ($name in @('Normalize-Patch','Assert-SourcePatch')) {
    $definitions = @($ast.FindAll({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    },$true))
    if ($definitions.Count -ne 1) { throw "Expected one trusted function definition: $name" }
    . ([scriptblock]::Create($definitions[0].Extent.Text))
}

$git = (Get-Command git.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$ownedRoot = Join-Path $repoRoot 'work/recomp-source-patch-tests'
$runRoot = Join-Path $ownedRoot ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N'))
$workRoot = Join-Path $runRoot 'inspection'
[IO.Directory]::CreateDirectory($workRoot) | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$script:checks = 0
function Check($Condition,[string]$Message) {
    ++$script:checks
    if (-not $Condition) { throw $Message }
}
function Git-Checked([string[]]$Arguments) {
    & $git @Arguments | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Fixture Git command failed: $($Arguments -join ' ')" }
}
function Expect-Rejection([scriptblock]$Operation,[string]$ExpectedMessage) {
    $observed = ''
    try { & $Operation | Out-Null } catch { $observed = $_.Exception.Message }
    Check ($observed -like "*$ExpectedMessage*") "Wrong or missing rejection: $observed"
}
function Write-Utf8([string]$Path,[string]$Text) { [IO.File]::WriteAllText($Path,$Text,$utf8) }

# Only these newly allocated local repositories are changed. No original game
# assets, actual runtime sources, network service or external account is used.
$source = Join-Path $runRoot 'source'
Git-Checked @('init','--quiet',$source)
Git-Checked @('-C',$source,'config','core.autocrlf','false')
Git-Checked @('-C',$source,'config','core.abbrev','7')
$context = 'UTF-8 context: ' + [char]0x2014 + ' ' + [char]0x00fc + ' ' + [char]0x30c6
$original = $context + "`nmode=original`nend`n"
$modified = $context + "`nmode=patched`nend`n"
$existing = Join-Path $source 'existing.txt'
Write-Utf8 $existing $original
Git-Checked @('-C',$source,'add','existing.txt')
Git-Checked @('-C',$source,'-c','user.name=DW4 fixture','-c','user.email=fixture@example.invalid',
    'commit','--quiet','-m','Create isolated patch fixture')
Write-Utf8 $existing $modified
$added = Join-Path $source 'added.txt'
Write-Utf8 $added ($context + "`nnew file`n")
Git-Checked @('-C',$source,'add','--intent-to-add','added.txt')
$patchPath = Join-Path $runRoot 'expected.patch'
Git-Checked @('-C',$source,'diff','--binary','--full-index',"--output=$patchPath")
$expected = [IO.File]::ReadAllText($patchPath,[Text.Encoding]::UTF8)
Check ($expected.Contains($context)) 'The actual patch lost its non-ASCII UTF-8 context.'
Check ($expected -match '(?m)^index [0-9a-f]{40}\.\.[0-9a-f]{40}') 'The actual patch lacks stable full object IDs.'
Check ($expected.Contains('new file mode 100644')) 'The patch does not exercise an added source file.'
Check ((Normalize-Patch "a`r`nb`r`n") -ceq "a`nb") 'CRLF normalization differs.'
Check ((Assert-SourcePatch $source $expected $false) -eq $true) 'Exact UTF-8 patch was rejected.'
Check ((Assert-SourcePatch $source ($expected.Replace("`n","`r`n")) $false) -eq $true) 'CRLF expected patch was rejected.'
Git-Checked @('-C',$source,'config','core.abbrev','20')
Check ((Assert-SourcePatch $source $expected $false) -eq $true) 'Repository abbreviation changed patch acceptance.'

$fresh = Join-Path $runRoot 'fresh'
Git-Checked @('-c','core.autocrlf=false','clone','--quiet','--no-hardlinks',$source,$fresh)
Git-Checked @('-C',$fresh,'config','core.autocrlf','false')
Check ((Assert-SourcePatch $fresh $expected $true) -eq $false) 'An allowed clean checkout must return false.'
Check ((Assert-SourcePatch $fresh '' $false) -eq $false) 'An exact clean checkout must return false.'
Expect-Rejection { Assert-SourcePatch $fresh $expected $false } 'changes beyond its supported patch'
Git-Checked @('-C',$fresh,'apply','--check',$patchPath)
Git-Checked @('-C',$fresh,'apply','--intent-to-add',$patchPath)
Check ((Assert-SourcePatch $fresh $expected $false) -eq $true) 'Fresh intent-to-add application differs.'
Check (([IO.File]::ReadAllText((Join-Path $fresh 'added.txt'),[Text.Encoding]::UTF8)) -ceq ($context + "`nnew file`n")) 'Fresh added UTF-8 file differs.'
Check (-not ((& $git -C $fresh diff --cached --name-only) -join '')) 'Intent-to-add unexpectedly staged content.'
Check (-not ((& $git -C $fresh ls-files --others --exclude-standard) -join '')) 'Intent-to-add left an untracked source file.'

# Rejections must neither repair nor reset user changes.
$wrong = $modified.Replace('mode=patched','mode=unrelated')
Write-Utf8 $existing $wrong
Expect-Rejection { Assert-SourcePatch $source $expected $false } 'changes beyond its supported patch'
Expect-Rejection { Assert-SourcePatch $source $expected $true } 'changes beyond its supported patch'
Check (([IO.File]::ReadAllText($existing,[Text.Encoding]::UTF8)) -ceq $wrong) 'The guard rewrote unexpected content.'
Write-Utf8 $existing $modified
$untracked = Join-Path $source 'untracked.txt'
Write-Utf8 $untracked 'preserve this untracked file'
Expect-Rejection { Assert-SourcePatch $source $expected $false } 'untracked or unreadable files'
Check (([IO.File]::ReadAllText($untracked)) -ceq 'preserve this untracked file') 'The guard changed an untracked file.'
# Nonrecursive removal of one exact fixture file created above.
Remove-Item -LiteralPath $untracked
Git-Checked @('-C',$source,'add','existing.txt')
Expect-Rejection { Assert-SourcePatch $source $expected $false } 'staged or unreadable changes'
Check ([bool]((& $git -C $source diff --cached --name-only) -join '')) 'The guard unstaged source changes.'
Check (@(Get-ChildItem -LiteralPath $workRoot -Filter 'source-inspection-*.patch').Count -eq 0) 'Temporary inspection patches leaked.'

$report = [ordered]@{
    passed=$true; checks=$script:checks; powershell_version=$PSVersionTable.PSVersion.ToString();
    build_script_sha256=(Get-FileHash -LiteralPath $buildScript -Algorithm SHA256).Hash;
    fixture_patch_sha256=(Get-FileHash -LiteralPath $patchPath -Algorithm SHA256).Hash;
    run_root=$runRoot; runtime_built=$false; runtime_launched=$false; original_assets_used=$false
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
Write-Host "Recompilation source patch tests: $script:checks checks passed on PowerShell $($PSVersionTable.PSVersion)."
Write-Host "Evidence: $runRoot/result.json"
