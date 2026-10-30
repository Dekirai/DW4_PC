<#
.SYNOPSIS
  Prints the SPKI pins the game client build needs (the IP phase) and writes them for the build.

.DESCRIPTION
  The game client pins the server's public key: the SHA-256 of the certificate's DER SubjectPublicKeyInfo, base64 (44
  characters), the value "DW4Online.Server.exe tls-pin" prints too. The client takes two pins, the key in use and the
  spare, from a build step (CMake DW4_ONLINE_PINS, or the file local/online_pins.txt of the DW4_PC checkout; DEPLOY.md
  6.1). This script reads the pins from

  -KeyFile FILE[,FILE]   the kept keys (PEM, tls-key.ps1 -NewKey; needs OpenSSL, asks for the passphrases)
  -CertFile FILE[,FILE]  certificates (.cer / .crt / .pem)
  -Pfx FILE[,FILE]       PFX files (asks for their passwords)
  -Server HOST[:PORT]    the certificate a running server presents (a TLS handshake only, nothing is sent)

  in the order given: the first pin is the one in use, the second the spare. With -Server and other sources it checks
  that the server presents the first one. -Out FILE writes the pins for the client build (one per line); the pins are
  public values, but the file stays outside Git (local/ is ignored). -PassEnv NAME takes the passphrases from the
  environment variable NAME instead of asking (tests).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File print-pins.ps1 -KeyFile E:\dw4online\server-key.pem,E:\dw4online\spare-key.pem -Out C:\src\DW4_PC\local\online_pins.txt
  powershell -ExecutionPolicy Bypass -File print-pins.ps1 -Server 88.198.52.221
#>
param(
    [string[]]$KeyFile = @(),
    [string[]]$CertFile = @(),
    [string[]]$Pfx = @(),
    [string]$Server = '',
    [string]$Out = '',
    [string]$OpenSsl = '',
    [string]$PassEnv = ''
)

$ErrorActionPreference = 'Stop'
# (powershell -File passes "a,b" as one string: split the lists here.)
$KeyFile = @($KeyFile | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$CertFile = @($CertFile | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Pfx = @($Pfx | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

# --- The SPKI pin from a certificate's DER (as new-selfsigned-cert.ps1 and the client compute it) ----------------------

function Read-DerLength([byte[]]$der, [int]$at) {
    # Returns @(contentStart, contentLength) of the DER element whose tag is at $at.
    $first = $der[$at + 1]
    if ($first -lt 0x80) { return @(($at + 2), [int]$first) }
    $count = $first -band 0x7F
    $length = 0
    for ($i = 0; $i -lt $count; $i++) { $length = ($length * 256) + $der[$at + 2 + $i] }
    return @(($at + 2 + $count), $length)
}

function Get-Sha256Base64([byte[]]$bytes) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return [Convert]::ToBase64String($sha.ComputeHash($bytes)) } finally { $sha.Dispose() }
}

function Get-CertificatePin([byte[]]$der) {
    # Certificate ::= SEQUENCE { tbsCertificate SEQUENCE { [0] version?, serialNumber, signature, issuer, validity,
    # subject, subjectPublicKeyInfo, ... }, ... }: walk to the seventh element (sixth without a version) and hash it.
    $tbs = Read-DerLength $der 0
    $at = $tbs[0]
    $inner = Read-DerLength $der $at
    $at = $inner[0]
    $skip = if ($der[$at] -eq 0xA0) { 6 } else { 5 }
    for ($i = 0; $i -lt $skip; $i++) {
        $element = Read-DerLength $der $at
        $at = $element[0] + $element[1]
    }
    $spki = Read-DerLength $der $at
    $bytes = New-Object byte[] ($spki[0] + $spki[1] - $at)
    [Array]::Copy($der, $at, $bytes, 0, $bytes.Length)
    return Get-Sha256Base64 $bytes
}

function Read-Secret([string]$prompt) {
    if ($PassEnv) {
        $value = [Environment]::GetEnvironmentVariable($PassEnv)
        if (-not $value) { throw "The environment variable $PassEnv is empty." }
        return $value
    }
    $secure = Read-Host -AsSecureString $prompt
    return [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
}

function Find-OpenSsl {
    if ($OpenSsl) { return $OpenSsl }
    foreach ($candidate in @("$env:ProgramFiles\Git\mingw64\bin\openssl.exe", "$env:ProgramFiles\Git\usr\bin\openssl.exe")) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    $command = Get-Command openssl -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw 'OpenSSL was not found (needed for -KeyFile): install Git for Windows or pass -OpenSsl <path>.'
}

function Get-KeyPin([string]$key) {
    $openssl = Find-OpenSsl
    $der = [System.IO.Path]::GetTempFileName()
    try {
        $arguments = @('pkey', '-in', $key, '-pubout', '-outform', 'DER', '-out', $der)
        if ($PassEnv) { $arguments += @('-passin', "env:$PassEnv") }
        & $openssl @arguments | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "openssl pkey failed for $key ($LASTEXITCODE)." }
        return Get-Sha256Base64 ([System.IO.File]::ReadAllBytes($der))
    } finally {
        Remove-Item -LiteralPath $der -ErrorAction SilentlyContinue
    }
}

function Get-ServerPin([string]$target) {
    $name = $target
    $port = 443
    if ($target -match '^\[(.+)\]:(\d+)$' -or $target -match '^([^:]+):(\d+)$') { $name = $Matches[1]; $port = [int]$Matches[2] }
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $client.Connect($name, $port)
        # Any certificate: this only looks at the key the server presents (no request is sent).
        $accept = [System.Net.Security.RemoteCertificateValidationCallback] { param($s, $c, $chain, $errors) $true }
        $ssl = New-Object System.Net.Security.SslStream($client.GetStream(), $false, $accept)
        try {
            $ssl.AuthenticateAsClient($name)
            $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 -ArgumentList $ssl.RemoteCertificate
            try {
                Write-Output "  ${target}: $($cert.Subject), valid until $($cert.NotAfter.ToUniversalTime().ToString('yyyy-MM-dd')) (UTC)" | Out-Host
                return Get-CertificatePin $cert.RawData
            } finally { $cert.Dispose() }
        } finally { $ssl.Dispose() }
    } finally { $client.Dispose() }
}

# --- Collect ------------------------------------------------------------------------------------------------------------

$pins = New-Object System.Collections.Generic.List[string]
$labels = New-Object System.Collections.Generic.List[string]
function Add-Pin([string]$pin, [string]$label) {
    if ($pin.Length -ne 44 -or -not $pin.EndsWith('=')) { throw "$label gave no SHA-256 pin ($pin)." }
    if ($pins.Contains($pin)) { Write-Output "  (${label}: the same key as an earlier one)"; return }
    $pins.Add($pin)
    $labels.Add($label)
}

foreach ($file in $KeyFile) { Add-Pin (Get-KeyPin $file) "key $file" }
foreach ($file in $CertFile) {
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 -ArgumentList (Resolve-Path -LiteralPath $file).Path
    try { Add-Pin (Get-CertificatePin $cert.RawData) "certificate $file" } finally { $cert.Dispose() }
}
foreach ($file in $Pfx) {
    $password = Read-Secret "Password of $file"
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 -ArgumentList (Resolve-Path -LiteralPath $file).Path, $password
    try { Add-Pin (Get-CertificatePin $cert.RawData) "PFX $file" } finally { $cert.Dispose() }
}
$served = $null
if ($Server) {
    $served = Get-ServerPin $Server
    if ($pins.Count -eq 0) { Add-Pin $served "server $Server" }
}
if ($pins.Count -eq 0) { throw 'Name at least one source: -KeyFile, -CertFile, -Pfx or -Server.' }
if ($pins.Count -gt 2) { throw "The client takes two pins (in use, spare); $($pins.Count) were given." }

Write-Output ''
Write-Output 'SPKI pins (SHA-256, base64) for the game client, in use first:'
for ($i = 0; $i -lt $pins.Count; $i++) { Write-Output "  $($pins[$i])   ($(if ($i -eq 0) { 'in use' } else { 'spare' }): $($labels[$i]))" }
if ($served) {
    if ($served -eq $pins[0]) {
        Write-Output "The server $Server presents the key in use."
    } elseif ($pins.Contains($served)) {
        Write-Output "The server $Server presents the SPARE key (clients accept it; ship a client update with a new spare)."
    } else {
        throw "The server $Server presents another key ($served): clients with these pins would refuse it."
    }
}
Write-Output ''
Write-Output "Client build: CMake -DDW4_ONLINE_PINS=$($pins -join ',')"
Write-Output '  or these lines in local/online_pins.txt of the DW4_PC checkout (-Out writes it); then build again.'
if ($Out) {
    $folder = Split-Path -Parent ([System.IO.Path]::GetFullPath($Out))
    if (-not (Test-Path -LiteralPath $folder)) { New-Item -ItemType Directory -Path $folder | Out-Null }
    $text = "# DW4Online server SPKI pins for the client build (in use, then spare): print-pins.ps1, $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd')) UTC`n"
    $text += ($pins -join "`n") + "`n"
    [System.IO.File]::WriteAllText([System.IO.Path]::GetFullPath($Out), $text, (New-Object System.Text.UTF8Encoding($false)))
    Write-Output "Written to $Out."
}
