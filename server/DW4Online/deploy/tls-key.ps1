<#
.SYNOPSIS
  The server's TLS keys, made once and kept (the IP phase): new keys, and certificates on a kept key.

.DESCRIPTION
  The game client pins the server's public key (its SPKI pin, DEPLOY.md 6.1) instead of trusting a certificate
  authority. A key made once and kept offline keeps its pin through every certificate made on it: a renewal, a
  reinstalled or moved server need no client update. Run this script with OpenSSL (Git for Windows brings one) on the
  PC where the keys are kept, not on the server:

  -NewKey FILE   makes an RSA 3072 key, encrypted with AES-256 under a passphrase, as a PEM file. It never overwrites a
                 file. Make two once: the key in use and the spare. Keep both offline (a USB stick, a second copy
                 elsewhere) and their passphrases apart (a password manager). Never put them into Git or the repository.
  -Certificate -KeyFile FILE -Out PFX
                 a self-signed certificate for the IP address (subject alternative name IP, server authentication,
                 valid -Years, default 10) on that key, packed into a password-protected PFX for the server, where
                 new-selfsigned-cert.ps1 -ImportPfx installs it without export permission. The pin is the key's, so
                 it stays the same for every certificate of the key.

  Both print the key's SPKI pin (SHA-256 of the DER SubjectPublicKeyInfo, base64), the value print-pins.ps1 writes for
  the client build. OpenSSL asks for the passphrases; -PassEnv NAME takes them from the environment variable NAME
  instead (tests; never a passphrase on the command line).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tls-key.ps1 -NewKey E:\dw4online\server-key.pem
  powershell -ExecutionPolicy Bypass -File tls-key.ps1 -NewKey E:\dw4online\spare-key.pem
  powershell -ExecutionPolicy Bypass -File tls-key.ps1 -Certificate -KeyFile E:\dw4online\server-key.pem -Out E:\dw4online\server.pfx
#>
param(
    [string]$NewKey = '',
    [switch]$Certificate,
    [string]$KeyFile = '',
    [string]$Out = '',
    [string]$IpAddress = '88.198.52.221',
    [int]$Years = 10,
    [string]$OpenSsl = '',
    [string]$PassEnv = ''
)

$ErrorActionPreference = 'Stop'

function Find-OpenSsl {
    if ($OpenSsl) {
        if (-not (Test-Path -LiteralPath $OpenSsl)) { throw "OpenSSL not found at $OpenSsl." }
        return (Resolve-Path -LiteralPath $OpenSsl).Path
    }
    # The native build of Git for Windows first (no POSIX argument conversion), then whatever PATH has.
    foreach ($candidate in @("$env:ProgramFiles\Git\mingw64\bin\openssl.exe", "$env:ProgramFiles\Git\usr\bin\openssl.exe")) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    $command = Get-Command openssl -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw 'OpenSSL was not found: install Git for Windows (it brings openssl.exe) or pass -OpenSsl <path>.'
}

function Invoke-OpenSsl([string[]]$arguments) {
    & $script:openssl @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "openssl $($arguments[0]) failed ($LASTEXITCODE)." }
}

# The passphrase options: from the environment variable -PassEnv names, else OpenSSL asks.
function Pass-In { if ($PassEnv) { return @('-passin', "env:$PassEnv") } else { return @() } }
function Pass-Out { if ($PassEnv) { return @('-passout', "env:$PassEnv") } else { return @() } }

# The SPKI pin of a (private) key file: OpenSSL writes the DER SubjectPublicKeyInfo, .NET hashes it.
function Get-KeyPin([string]$key) {
    $der = [System.IO.Path]::GetTempFileName()
    try {
        Invoke-OpenSsl (@('pkey', '-in', $key, '-pubout', '-outform', 'DER', '-out', $der) + (Pass-In))
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try { return [Convert]::ToBase64String($sha.ComputeHash([System.IO.File]::ReadAllBytes($der))) } finally { $sha.Dispose() }
    } finally {
        Remove-Item -LiteralPath $der -ErrorAction SilentlyContinue
    }
}

$script:openssl = Find-OpenSsl
if ($NewKey) {
    if ($Certificate) { throw '-NewKey and -Certificate are separate steps.' }
    if (Test-Path -LiteralPath $NewKey) { throw "$NewKey exists already: a key is made once and kept, never replaced by accident." }
    $folder = Split-Path -Parent ([System.IO.Path]::GetFullPath($NewKey))
    if (-not (Test-Path -LiteralPath $folder)) { New-Item -ItemType Directory -Path $folder | Out-Null }
    Invoke-OpenSsl (@('genpkey', '-quiet', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:3072', '-aes-256-cbc', '-out', $NewKey) + $(if ($PassEnv) { @('-pass', "env:$PassEnv") } else { @() }))
    $pin = Get-KeyPin $NewKey
    Write-Output "Key written to ${NewKey} (RSA 3072, AES-256 under its passphrase)."
    Write-Output "SPKI pin (SHA-256, base64): $pin"
    Write-Output 'Keep the file offline and its passphrase apart; it never goes into Git or onto the server as a PEM file.'
    exit 0
}
if ($Certificate) {
    if (-not $KeyFile -or -not $Out) { throw '-Certificate needs -KeyFile (the kept key) and -Out (the PFX for the server).' }
    if (-not (Test-Path -LiteralPath $KeyFile)) { throw "$KeyFile not found." }
    if (Test-Path -LiteralPath $Out) { throw "$Out exists already; choose another file." }
    $ip = $null
    if (-not [System.Net.IPAddress]::TryParse($IpAddress, [ref]$ip)) { throw "-IpAddress '$IpAddress' is not an IP address." }
    if ($Years -lt 1 -or $Years -gt 30) { throw '-Years must be between 1 and 30.' }
    $work = Join-Path ([System.IO.Path]::GetTempPath()) ('dw4online-cert-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    try {
        $cert = Join-Path $work 'certificate.pem'
        Invoke-OpenSsl (@('req', '-new', '-x509', '-key', $KeyFile, '-sha256', '-days', [string]($Years * 365), '-subj', "/CN=$IpAddress",
            '-addext', "subjectAltName=IP:$IpAddress", '-addext', 'extendedKeyUsage=serverAuth',
            '-addext', 'keyUsage=critical,digitalSignature,keyEncipherment', '-out', $cert) + (Pass-In))
        Invoke-OpenSsl (@('pkcs12', '-export', '-inkey', $KeyFile, '-in', $cert, '-out', $Out, '-name', "DW4Online $IpAddress",
            '-keypbe', 'AES-256-CBC', '-certpbe', 'AES-256-CBC', '-macalg', 'SHA256') + (Pass-In) + (Pass-Out))
        $x509 = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 -ArgumentList $cert
        try {
            Write-Output "Certificate for IP $IpAddress, valid $($x509.NotBefore.ToUniversalTime().ToString('yyyy-MM-dd')) to $($x509.NotAfter.ToUniversalTime().ToString('yyyy-MM-dd')) (UTC), thumbprint $($x509.Thumbprint)"
        } finally { $x509.Dispose() }
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Output "PFX written to ${Out}: copy it to the server and run new-selfsigned-cert.ps1 -ImportPfx there."
    Write-Output "SPKI pin (SHA-256, base64): $(Get-KeyPin $KeyFile)"
    exit 0
}
throw 'Use -NewKey FILE, or -Certificate -KeyFile FILE -Out PFX (see Get-Help .\tls-key.ps1 -Full).'
