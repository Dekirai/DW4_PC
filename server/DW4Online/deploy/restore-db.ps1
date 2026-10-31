<#
.SYNOPSIS
  Restores a backup of backup-db.ps1 (a .sql.gz dump) into a MariaDB database: the restore drill and the rollback.

.DESCRIPTION
  Decompresses the dump next to the backup (<backup>.restore.sql; the backups folder is readable only by
  Administrators and SYSTEM), puts
  "CREATE DATABASE ... ; USE ..." in front and feeds the file to the MariaDB client byte for byte through cmd.exe, so
  the client asks for the password once. Do not pipe a dump through Windows PowerShell 5.1 instead
  ("7z e -so x.sql.gz | mariadb.exe ..."): the pipe re-encodes the text and turns every non-ASCII character (an
  umlaut in a device name or an e-mail address) into "?" without an error.

  Afterwards it prints the restored database's account count and schema version and removes the decompressed file.

  -Database dw4online_restore (the default) is the restore drill: the database must not exist yet; drop it afterwards
  (DEPLOY.md section 7). -Database dw4online -Replace is the rollback of an update (DEPLOY.md section 8): it drops and
  re-creates the live database, so it refuses while the DW4Online service runs. The users' grants on dw4online.*
  survive DROP DATABASE. It is also the way out of a migration that stopped half way (DEPLOY.md, "Migrations"):
  restore the backup made before the update, then run "DW4Online.Server.exe migrate" in the console.

  -MariaDbBin is the database's bin folder ('C:\Program Files\MariaDB 11.4\bin' for the MSI install, the default; the
  XAMPP folder's mysql\bin for a XAMPP MariaDB). XAMPP's MariaDB 10.4 names its client mysql.exe (no mariadb.exe):
  pass -ClientExe mysql.exe; without -ClientExe the script also takes mysql.exe when the folder has no mariadb.exe.
  -OptionFile config\backup.cnf uses the backup's login instead of asking for the password (a root login there can
  create and drop databases; a read-only backup user cannot).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File restore-db.ps1 -Backup D:\Dekirai\Servers\DW4Online\backups\daily\dw4online-20261030-033000.sql.gz
  powershell -ExecutionPolicy Bypass -File restore-db.ps1 -Backup <file> -Database dw4online -Replace
  powershell -ExecutionPolicy Bypass -File restore-db.ps1 -Backup <file> -MariaDbBin C:\xampp\mysql\bin -ClientExe mysql.exe
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Backup,
    [string]$Database = 'dw4online_restore',
    [switch]$Replace,
    [string]$MariaDbBin = 'C:\Program Files\MariaDB 11.4\bin',
    [string]$ClientExe = 'mariadb.exe',
    [string]$User = 'root',
    [string]$HostName = '127.0.0.1',
    [int]$Port = 3306,
    # An option file with [client] user / password instead of the password prompt (tests and scripts).
    [string]$OptionFile = '',
    [string]$ServiceName = 'DW4Online'
)

$ErrorActionPreference = 'Stop'
$client = Join-Path $MariaDbBin $ClientExe
if (-not (Test-Path $client) -and -not $PSBoundParameters.ContainsKey('ClientExe') -and (Test-Path (Join-Path $MariaDbBin 'mysql.exe'))) {
    $ClientExe = 'mysql.exe' # XAMPP's MariaDB 10.4 has no mariadb.exe
    $client = Join-Path $MariaDbBin $ClientExe
}
if (-not (Test-Path $client)) {
    throw "$client not found: pass the database's bin folder with -MariaDbBin (XAMPP: -MariaDbBin <XAMPP folder>\mysql\bin -ClientExe mysql.exe)."
}
$Backup = (Resolve-Path $Backup).Path
if ($Database -notmatch '^[A-Za-z0-9_]+$') { throw "-Database '$Database': letters, digits and _ only." }
if ($Database -eq 'dw4online' -and -not $Replace) { throw 'Restoring into the live database dw4online needs -Replace (the rollback, DEPLOY.md section 8).' }
if ($Replace) {
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service -and $service.Status -ne 'Stopped') { throw "Stop the service first: Stop-Service $ServiceName" }
}

$sql = "$Backup.restore.sql"
$create = if ($Replace) { "DROP DATABASE IF EXISTS ``$Database``;`n" } else { '' }
$create += "CREATE DATABASE ``$Database`` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;`nUSE ``$Database``;`n"
$check = "`nSELECT 'accounts', COUNT(*) FROM ``$Database``.accounts;`nSELECT 'schema version', MAX(version) FROM ``$Database``.schema_version;`n"
try {
    $out = [IO.File]::Create($sql)
    try {
        $head = [Text.Encoding]::ASCII.GetBytes($create)
        $out.Write($head, 0, $head.Length)
        $in = [IO.File]::OpenRead($Backup)
        $gz = New-Object IO.Compression.GZipStream($in, [IO.Compression.CompressionMode]::Decompress)
        try { $gz.CopyTo($out) } finally { $gz.Dispose(); $in.Dispose() }
        $tail = [Text.Encoding]::ASCII.GetBytes($check)
        $out.Write($tail, 0, $tail.Length)
    } finally { $out.Dispose() }

    $auth = if ($OptionFile) { "--defaults-extra-file=`"$OptionFile`"" } else { "-u $User -p" }
    Write-Output "Restoring $Backup into $Database$(if (-not $OptionFile) { ' (MariaDB asks for the password of ' + $User + ')' })"
    # cmd.exe redirects the file unchanged; --default-character-set matches the dump (backup-db.ps1 writes utf8mb4).
    & cmd.exe /c "`"$client`" $auth -h $HostName -P $Port --default-character-set=utf8mb4 --batch --skip-column-names <`"$sql`""
    if ($LASTEXITCODE -ne 0) { throw "$ClientExe exited with $LASTEXITCODE (nothing or only part of the dump was restored)." }
    Write-Output "Restored into $Database."
} finally {
    if (Test-Path $sql) { Remove-Item -Force $sql }
}
