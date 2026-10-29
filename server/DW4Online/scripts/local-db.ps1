<#
.SYNOPSIS
  A private MariaDB instance for DW4Online development and integration tests.

.DESCRIPTION
  Uses the MariaDB binaries of an existing installation (default: XAMPP's C:\xampp\mysql\bin) with its OWN data
  directory under server/DW4Online/.local (gitignored) and its OWN port, bound to 127.0.0.1 only. It never touches
  the installation's own data directory, other databases or services. Stops only the mysqld it started (by PID).

  start   create the data directory on first use (random root password in .local/mysql-root.txt) and start mysqld
  stop    shut the instance down (SQL SHUTDOWN, then the recorded PID if needed)
  status  show whether the recorded mysqld is running
  env     print the connection string for the integration tests (DW4ONLINE_TEST_MYSQL)
  app     create the database 'dw4online' and its user, print the connection string for appsettings.Local.json

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\local-db.ps1 start
#>
param(
    [ValidateSet('start', 'stop', 'status', 'env', 'app')]
    [string]$Command = 'status',
    [string]$MySqlBin = 'C:\xampp\mysql\bin',
    [int]$Port = 3427
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$local = Join-Path $root '.local'
$data = Join-Path $local 'mysql-data'
$pidFile = Join-Path $local 'mysqld.pid'
$rootPwFile = Join-Path $local 'mysql-root.txt'
$appPwFile = Join-Path $local 'mysql-app.txt'
$logFile = Join-Path $local 'mysqld.err'
$mysqld = Join-Path $MySqlBin 'mysqld.exe'
$mysql = Join-Path $MySqlBin 'mysql.exe'

function New-Secret([int]$length) {
    $chars = 'abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789'.ToCharArray()
    $bytes = New-Object byte[] $length
    [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    -join ($bytes | ForEach-Object { $chars[$_ % $chars.Length] })
}

function Get-RunningPid {
    if (-not (Test-Path $pidFile)) { return $null }
    $id = [int](Get-Content $pidFile -Raw)
    $p = Get-Process -Id $id -ErrorAction SilentlyContinue
    if ($null -ne $p -and $p.ProcessName -eq 'mysqld') { return $id }
    return $null
}

function Test-Port {
    $client = New-Object System.Net.Sockets.TcpClient
    try { $client.Connect('127.0.0.1', $Port); return $true } catch { return $false } finally { $client.Dispose() }
}

function Invoke-Sql([string]$sql) {
    $pw = (Get-Content $rootPwFile -Raw).Trim()
    & $mysql --protocol=tcp -h 127.0.0.1 -P $Port -u root "--password=$pw" -N -e $sql
    if ($LASTEXITCODE -ne 0) { throw "mysql exited with $LASTEXITCODE" }
}

switch ($Command) {
    'start' {
        if (Get-RunningPid) { Write-Output "Already running (PID $(Get-RunningPid)) on port $Port."; break }
        if (-not (Test-Path $mysqld)) { throw "mysqld.exe not found in $MySqlBin (use -MySqlBin)." }
        if (Test-Port) { throw "Port $Port is in use by another process; choose another with -Port." }
        New-Item -ItemType Directory -Force $local | Out-Null
        if (-not (Test-Path $data)) {
            $pw = New-Secret 24
            Set-Content -Path $rootPwFile -Value $pw -NoNewline -Encoding ascii
            & (Join-Path $MySqlBin 'mysql_install_db.exe') "--datadir=$data" "--port=$Port" "--password=$pw" --silent
            if ($LASTEXITCODE -ne 0) { throw "mysql_install_db exited with $LASTEXITCODE" }
        }
        $mysqldArgs = @("--defaults-file=`"$data\my.ini`"", '--bind-address=127.0.0.1', "--port=$Port", "--log-error=`"$logFile`"",
                  '--character-set-server=utf8mb4', '--collation-server=utf8mb4_unicode_ci', '--default-time-zone=+00:00',
                  '--innodb-use-native-aio=0') # native AIO hung DROP DATABASE on XAMPP's 10.4 (InnoDB latch wait)
        $p = Start-Process -FilePath $mysqld -ArgumentList $mysqldArgs -WindowStyle Hidden -PassThru
        Set-Content -Path $pidFile -Value $p.Id -NoNewline -Encoding ascii
        for ($i = 0; $i -lt 60 -and -not (Test-Port); $i++) { Start-Sleep -Milliseconds 500 }
        if (-not (Test-Port)) { throw "mysqld did not open port $Port; see $logFile" }
        Write-Output "mysqld running (PID $($p.Id)) on 127.0.0.1:$Port, data in $data"
    }
    'stop' {
        $id = Get-RunningPid
        if (-not $id) { Write-Output 'Not running.'; break }
        try { Invoke-Sql 'SHUTDOWN' } catch { Write-Output "SQL SHUTDOWN failed: $_" }
        $p = Get-Process -Id $id -ErrorAction SilentlyContinue
        if ($null -ne $p) { $null = $p.WaitForExit(30000) }
        if (Get-Process -Id $id -ErrorAction SilentlyContinue) { Stop-Process -Id $id -Confirm:$false }
        Remove-Item $pidFile -ErrorAction SilentlyContinue
        Write-Output "Stopped PID $id."
    }
    'status' {
        $id = Get-RunningPid
        if ($id) { Write-Output "Running (PID $id) on 127.0.0.1:$Port." } else { Write-Output 'Not running.' }
    }
    'env' {
        $pw = (Get-Content $rootPwFile -Raw).Trim()
        Write-Output "`$env:DW4ONLINE_TEST_MYSQL = 'Server=127.0.0.1;Port=$Port;User ID=root;Password=$pw'"
    }
    'app' {
        if (-not (Test-Path $appPwFile)) { Set-Content -Path $appPwFile -Value (New-Secret 24) -NoNewline -Encoding ascii }
        $apw = (Get-Content $appPwFile -Raw).Trim()
        Invoke-Sql ("CREATE DATABASE IF NOT EXISTS dw4online CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci; " +
                    "CREATE USER IF NOT EXISTS 'dw4online'@'127.0.0.1' IDENTIFIED BY '$apw'; " +
                    "GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES ON dw4online.* TO 'dw4online'@'127.0.0.1';")
        Write-Output "Database:ConnectionString = Server=127.0.0.1;Port=$Port;Database=dw4online;User ID=dw4online;Password=$apw"
    }
}
