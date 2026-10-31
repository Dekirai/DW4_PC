<#
.SYNOPSIS
  Nightly MariaDB backup of the DW4Online database: dump, gzip, keep 14 daily + 8 weekly copies, optional off-site copy.

.DESCRIPTION
  Uses mariadb-dump with an option file for the credentials (see DEPLOY.md, section 7) so no password appears on the
  command line. Weekly copies are the Sunday dumps. Exit code 0 on success, 1 on failure (visible in Task Scheduler).

  -MariaDbBin is the database's bin folder: 'C:\Program Files\MariaDB 11.4\bin' for the MSI install (the default), the
  XAMPP folder's mysql\bin for a XAMPP MariaDB. XAMPP's MariaDB 10.4 names the dump program mysqldump.exe (no
  mariadb-dump.exe): pass -DumpExe mysqldump.exe; without -DumpExe the script also takes mysqldump.exe when the folder
  has no mariadb-dump.exe.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File backup-db.ps1
  powershell -ExecutionPolicy Bypass -File backup-db.ps1 -CopyTo \\storage\dw4online
  powershell -ExecutionPolicy Bypass -File backup-db.ps1 -MariaDbBin C:\xampp\mysql\bin -DumpExe mysqldump.exe
#>
param(
    [string]$MariaDbBin = 'C:\Program Files\MariaDB 11.4\bin',
    [string]$Database = 'dw4online',
    [string]$OptionFile = 'D:\Dekirai\Servers\DW4Online\config\backup.cnf',
    [string]$Target = 'D:\Dekirai\Servers\DW4Online\backups',
    [int]$KeepDaily = 14,
    [int]$KeepWeekly = 8,
    [string]$CopyTo = '',
    [string]$DumpExe = 'mariadb-dump.exe'
)

$ErrorActionPreference = 'Stop'
$sql = $null
try {
    $dump = Join-Path $MariaDbBin $DumpExe
    if (-not (Test-Path $dump) -and -not $PSBoundParameters.ContainsKey('DumpExe') -and (Test-Path (Join-Path $MariaDbBin 'mysqldump.exe'))) {
        $DumpExe = 'mysqldump.exe' # XAMPP's MariaDB 10.4 has no mariadb-dump.exe
        $dump = Join-Path $MariaDbBin $DumpExe
    }
    if (-not (Test-Path $dump)) {
        throw "$dump not found: pass the database's bin folder with -MariaDbBin (XAMPP: -MariaDbBin <XAMPP folder>\mysql\bin -DumpExe mysqldump.exe)."
    }
    if (-not (Test-Path $OptionFile)) { throw "$OptionFile not found." }
    $daily = Join-Path $Target 'daily'
    $weekly = Join-Path $Target 'weekly'
    New-Item -ItemType Directory -Force $daily, $weekly | Out-Null

    $stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
    $sql = Join-Path $daily "$Database-$stamp.sql"
    $gz = "$sql.gz"
    # --defaults-extra-file must be the first option.
    & $dump "--defaults-extra-file=$OptionFile" --single-transaction --quick --hex-blob --routines --events `
        --no-tablespaces --default-character-set=utf8mb4 "--result-file=$sql" $Database
    if ($LASTEXITCODE -ne 0) { throw "$DumpExe exited with $LASTEXITCODE" }

    $in = [IO.File]::OpenRead($sql)
    $outFile = [IO.File]::Create($gz)
    $zip = New-Object IO.Compression.GZipStream($outFile, [IO.Compression.CompressionLevel]::Optimal)
    try { $in.CopyTo($zip) } finally { $zip.Dispose(); $outFile.Dispose(); $in.Dispose() }
    Remove-Item $sql

    if ((Get-Date).DayOfWeek -eq 'Sunday') { Copy-Item $gz $weekly }
    Get-ChildItem $daily -Filter '*.sql.gz' | Sort-Object Name -Descending | Select-Object -Skip $KeepDaily | Remove-Item
    Get-ChildItem $weekly -Filter '*.sql.gz' | Sort-Object Name -Descending | Select-Object -Skip $KeepWeekly | Remove-Item
    if ($CopyTo) { Copy-Item $gz $CopyTo }

    Write-Output "Backup written: $gz ($([math]::Round((Get-Item $gz).Length / 1KB)) KB)"
    exit 0
} catch {
    # A half-written dump is useless and holds personal data in plain text: remove it.
    if ($sql -and (Test-Path $sql)) { Remove-Item -Force $sql -ErrorAction SilentlyContinue }
    Write-Error "DW4Online backup failed: $_"
    exit 1
}
