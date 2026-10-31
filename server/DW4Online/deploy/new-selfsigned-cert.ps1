<#
.SYNOPSIS
  Creates the self-signed certificate for https://<IP address> (the IP phase) and prints the pins the game client needs.

.DESCRIPTION
  Until there is a domain, the game client talks to https://88.198.52.221 and pins the server's public key (the SHA-256
  of the certificate's SubjectPublicKeyInfo, "SPKI pin") instead of trusting a certificate authority. This script:

  1. creates a certificate for the IP address (subject alternative name IP, server authentication, RSA 3072, valid
     -Years, default 10) in LocalMachine\My. Its key cannot be exported: it never leaves this machine.
  2. gives the service account (NT SERVICE\DW4Online) read access to that key; the service runs the TLS handshakes.
  3. with -SparePfx: creates a second, spare key and certificate (in the current user's store, never where the
     service looks), writes them to a password-protected PFX file and removes them from this machine again. Keep that
     file offline (not on this server). Its pin goes into the client as well, so if this machine or its key is lost,
     installing the spare (-ImportPfx) keeps every client working without an update. -SparePfx with -SpareOnly makes
     only a spare, later, for the certificate already in use (its pin stays; print both pins for the client).
  4. prints both pins (computed here and checked against "DW4Online.Server.exe tls-pin") and the settings for
     appsettings.Production.json.

  -ImportPfx installs an existing PFX (the spare, or the certificate of a moved server) instead of creating one, and
  grants the service its key. -PinFile only prints the pin of a certificate file (.cer / .crt / .pem; for a PFX use
  "DW4Online.Server.exe tls-pin --file x.pfx --password-env NAME"). -PinServedBy <address> only connects to
  <address>:<-PinServedPort> the way the game client does (TLS without a host name, no certificate check) and
  prints the pin of the certificate the running service actually serves: it must be the first pin printed at creation.
  Neither needs elevation; nothing is changed.

  -HttpsPort is the service's HTTPS port (Kestrel:Endpoints:Https:Url), 443 unless another program holds 443 (a
  Traefik, IIS or Apache: DEPLOY.md section 4.1, for example 8443). It goes into the settings printed at the end and is
  the port -PinServedBy connects to unless -PinServedPort names another one.

  A new key means a new pin, and every client without that pin stops connecting: the script refuses to create a second
  certificate for an address that already has a valid one unless -Force is given. Nothing else changes: no other
  certificate, binding or service is touched. Run it elevated, after install-service.ps1 (the service account must
  exist), then restart the service (or wait up to 10 minutes: it re-reads the store).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File new-selfsigned-cert.ps1 -SparePfx E:\dw4online-spare.pfx
  powershell -ExecutionPolicy Bypass -File new-selfsigned-cert.ps1 -SparePfx E:\dw4online-spare.pfx -SpareOnly
  powershell -ExecutionPolicy Bypass -File new-selfsigned-cert.ps1 -ImportPfx E:\dw4online-spare.pfx
  powershell -ExecutionPolicy Bypass -File new-selfsigned-cert.ps1 -PinFile C:\temp\server.cer
  powershell -ExecutionPolicy Bypass -File new-selfsigned-cert.ps1 -PinServedBy 88.198.52.221
  powershell -ExecutionPolicy Bypass -File new-selfsigned-cert.ps1 -PinServedBy 88.198.52.221 -PinServedPort 8443
#>
param(
    [string]$IpAddress = '88.198.52.221',
    [string[]]$DnsName = @(),
    [int]$Years = 10,
    [ValidateSet('RSA', 'ECDSA_nistP256')]
    [string]$KeyAlgorithm = 'RSA',
    [string]$ServiceAccount = 'NT SERVICE\DW4Online',
    [string]$SparePfx = '',
    [System.Security.SecureString]$SparePassword = $null,
    [string]$ImportPfx = '',
    [System.Security.SecureString]$ImportPassword = $null,
    [string]$PinFile = '',
    [string]$PinServedBy = '',
    [int]$PinServedPort = 0,
    [int]$HttpsPort = 443,
    [ValidateSet('LocalMachine', 'CurrentUser')]
    [string]$StoreLocation = 'LocalMachine',
    [string]$Exe = (Join-Path (Split-Path -Parent $PSScriptRoot) 'DW4Online.Server.exe'),
    [switch]$Force,
    [switch]$SpareOnly,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$store = "Cert:\$StoreLocation\My"

# --- The SPKI pin, computed from the certificate's DER without the service (two implementations must agree) ---------

function Read-DerLength([byte[]]$der, [int]$at) {
    # Returns @(contentStart, contentLength) of the DER element whose tag is at $at.
    $first = $der[$at + 1]
    if ($first -lt 0x80) { return @(($at + 2), [int]$first) }
    $count = $first -band 0x7F
    $length = 0
    for ($i = 0; $i -lt $count; $i++) { $length = ($length * 256) + $der[$at + 2 + $i] }
    return @(($at + 2 + $count), $length)
}

function Get-SpkiPin([System.Security.Cryptography.X509Certificates.X509Certificate2]$cert) {
    # Certificate ::= SEQUENCE { tbsCertificate SEQUENCE { [0] version?, serialNumber, signature, issuer, validity,
    # subject, subjectPublicKeyInfo, ... }, ... }: walk to the seventh element (sixth without a version) and hash it.
    $der = $cert.RawData
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
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return [Convert]::ToBase64String($sha.ComputeHash($bytes)) } finally { $sha.Dispose() }
}

function Show-Certificate([System.Security.Cryptography.X509Certificates.X509Certificate2]$cert, [string]$label, [string]$pin) {
    Write-Output ''
    Write-Output "$label"
    Write-Output "  Subject:     $($cert.Subject)"
    Write-Output "  Thumbprint:  $($cert.Thumbprint)"
    Write-Output "  Valid:       $($cert.NotBefore.ToUniversalTime().ToString('yyyy-MM-dd')) to $($cert.NotAfter.ToUniversalTime().ToString('yyyy-MM-dd')) (UTC)"
    Write-Output "  SPKI pin (SHA-256, base64): $pin"
}

# Asks the service's own implementation (tls-pin) for the same pin; a difference stops the script.
function Assert-ServicePin([string]$thumbprint, [string]$expected, [string]$location = $StoreLocation) {
    if (-not (Test-Path $Exe)) {
        Write-Output "  ($Exe not found: the pin was not cross-checked with 'DW4Online.Server.exe tls-pin'.)"
        return
    }
    $lines = & $Exe tls-pin --thumbprint $thumbprint --location $location
    if ($LASTEXITCODE -ne 0) { throw "DW4Online.Server.exe tls-pin failed ($LASTEXITCODE)." }
    $line = @($lines | Where-Object { $_ -like 'SPKI pin (SHA-256, base64):*' })
    if ($line.Count -ne 1 -or -not $line[0].EndsWith(' ' + $expected)) {
        throw "DW4Online.Server.exe tls-pin printed another pin ($line) than this script ($expected)."
    }
    Write-Output '  (the same pin as DW4Online.Server.exe tls-pin)'
}

if ($PinFile) {
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 -ArgumentList (Resolve-Path $PinFile).Path
    try { Show-Certificate $cert "Certificate $PinFile" (Get-SpkiPin $cert) } finally { $cert.Dispose() }
    exit 0
}

if ($PinServedPort -le 0) { $PinServedPort = $HttpsPort }
if ($PinServedBy) {
    # Like the game client: no host name (an IP address sends no SNI), any certificate accepted here, then its pin.
    Write-Output "Connecting to ${PinServedBy}:$PinServedPort ..."
    $tcp = New-Object System.Net.Sockets.TcpClient($PinServedBy, $PinServedPort)
    try {
        $tls = New-Object System.Net.Security.SslStream($tcp.GetStream(), $false, { $true })
        try {
            $tls.AuthenticateAsClient($PinServedBy)
            $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 -ArgumentList $tls.RemoteCertificate
            try { Show-Certificate $cert "Served by ${PinServedBy}:$PinServedPort ($($tls.SslProtocol)):" (Get-SpkiPin $cert) } finally { $cert.Dispose() }
        } finally { $tls.Dispose() }
    } finally { $tcp.Dispose() }
    exit 0
}

# --- Checks -----------------------------------------------------------------------------------------------------------

$ip = $null
if (-not [System.Net.IPAddress]::TryParse($IpAddress, [ref]$ip)) { throw "-IpAddress '$IpAddress' is not an IP address." }
if ($Years -lt 1 -or $Years -gt 30) { throw '-Years must be between 1 and 30.' }
if ($StoreLocation -eq 'LocalMachine' -and -not $DryRun) {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw "This script needs administrator rights: open PowerShell with 'Run as administrator' (right-click the Start button, Terminal (Admin)), then run the same command again." }
}
if (-not $DryRun) {
    try {
        $null = (New-Object System.Security.Principal.NTAccount($ServiceAccount)).Translate([System.Security.Principal.SecurityIdentifier])
    } catch {
        throw "The account $ServiceAccount does not exist: run install-service.ps1 first (it creates the service and its account)."
    }
}

function Test-ForAddress([System.Security.Cryptography.X509Certificates.X509Certificate2]$cert) {
    foreach ($ext in $cert.Extensions) {
        if ($ext.Oid.Value -eq '2.5.29.17') {
            # The formatted SAN lists "IP Address=1.2.3.4" (the label is localized, the address is not).
            if ($ext.Format($false) -match ('(^|[=:,\s])' + [regex]::Escape($IpAddress) + '($|[,\s])')) { return $true }
        }
    }
    return $false
}

$now = Get-Date
$existing = @(Get-ChildItem $store | Where-Object { $_.HasPrivateKey -and $_.NotAfter -gt $now -and (Test-ForAddress $_) })
if ($SpareOnly) {
    if (-not $SparePfx) { throw '-SpareOnly needs -SparePfx (the file the spare goes to).' }
    if ($ImportPfx) { throw '-SpareOnly and -ImportPfx do not go together.' }
    if ($existing.Count -eq 0) { throw "No valid certificate for $IpAddress in ${store}: create one first (without -SpareOnly)." }
} elseif ($existing.Count -gt 0 -and -not $Force) {
    foreach ($e in $existing) { Show-Certificate $e "Already in ${store}:" (Get-SpkiPin $e) }
    throw ("A valid certificate for $IpAddress exists already (pins above). A new key means a new pin: every client " +
        'without it stops connecting. Pass -Force only if that is what you want.')
}

# Read access to the key for the service: the CNG key file under ProgramData (machine) or AppData (user).
function Grant-KeyRead([System.Security.Cryptography.X509Certificates.X509Certificate2]$cert) {
    $key = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($cert)
    if ($null -eq $key) { $key = [System.Security.Cryptography.X509Certificates.ECDsaCertificateExtensions]::GetECDsaPrivateKey($cert) }
    if ($null -eq $key -or -not ($key.PSObject.Properties.Name -contains 'Key')) { throw 'The certificate has no CNG private key.' }
    $name = $key.Key.UniqueName
    $dirs = if ($StoreLocation -eq 'LocalMachine') {
        @("$env:ProgramData\Microsoft\Crypto\Keys", "$env:ProgramData\Microsoft\Crypto\SystemKeys")
    } else {
        @("$env:APPDATA\Microsoft\Crypto\Keys")
    }
    $file = $dirs | ForEach-Object { Join-Path $_ $name } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $file) { throw "The key file $name was not found in $($dirs -join ', ')." }
    & icacls $file /grant "${ServiceAccount}:R" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "icacls $file failed ($LASTEXITCODE)." }
    Write-Output "  $ServiceAccount may read the key ($file)."
}

function Read-Password([string]$prompt) {
    $first = Read-Host -AsSecureString "$prompt"
    $second = Read-Host -AsSecureString "$prompt (again)"
    $a = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($first))
    $b = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($second))
    if ($a -ne $b) { throw 'The two passwords differ.' }
    if ($a.Length -lt 12) { throw 'Use a password of at least 12 characters.' }
    return $first
}

function New-IpCertificate([string]$exportPolicy, [string]$friendly, [string]$location = $store) {
    $san = "2.5.29.17={text}IPAddress=$IpAddress"
    foreach ($dns in $DnsName) { $san += "&DNS=$dns" }
    $parameters = @{
        Type              = 'Custom'
        Subject           = "CN=$IpAddress"
        FriendlyName      = $friendly
        TextExtension     = @($san, '2.5.29.37={text}1.3.6.1.5.5.7.3.1') # the address; server authentication
        CertStoreLocation = $location
        NotAfter          = (Get-Date).AddYears($Years)
        HashAlgorithm     = 'SHA256'
        Provider          = 'Microsoft Software Key Storage Provider'
        KeyExportPolicy   = $exportPolicy
    }
    if ($KeyAlgorithm -eq 'RSA') {
        $parameters.KeyAlgorithm = 'RSA'
        $parameters.KeyLength = 3072
        $parameters.KeyUsage = @('DigitalSignature', 'KeyEncipherment')
    } else {
        $parameters.KeyAlgorithm = 'ECDSA_nistP256'
        $parameters.KeyUsage = @('DigitalSignature')
    }
    return New-SelfSignedCertificate @parameters
}

if ($DryRun -and $SpareOnly) {
    Write-Output "Would keep the certificate in use for IP $IpAddress, create a spare $KeyAlgorithm key in Cert:\CurrentUser\My,"
    Write-Output "write it to $SparePfx, remove it from this machine and print both pins."
    exit 0
}
if ($DryRun) {
    Write-Output "Would create a $KeyAlgorithm certificate for IP $IpAddress (valid $Years years, key not exportable) in $store,"
    Write-Output "grant $ServiceAccount read access to its key$(if ($SparePfx) { ", write a spare key to $SparePfx" })$(if ($ImportPfx) { " (instead: import $ImportPfx)" }) and print the pins."
    exit 0
}

# --- Create or import ---------------------------------------------------------------------------------------------------

# The spare: created in the current user's store (the service reads LocalMachine\My and could pick a fresh certificate
# there for a moment, one whose key it may not read), exported, then removed with its key.
function New-Spare {
    if (Test-Path $SparePfx) { throw "$SparePfx exists already; choose another file." }
    if ($null -eq $SparePassword) { $script:SparePassword = Read-Password 'Password for the spare PFX' }
    $spare = New-IpCertificate 'Exportable' "DW4Online $IpAddress (spare)" 'Cert:\CurrentUser\My'
    try {
        $script:pins += Get-SpkiPin $spare
        Show-Certificate $spare 'Spare (exported, then removed from this machine):' $script:pins[-1]
        Assert-ServicePin $spare.Thumbprint $script:pins[-1] 'CurrentUser'
        $export = @{ Cert = $spare; FilePath = $SparePfx; Password = $script:SparePassword }
        if ((Get-Command Export-PfxCertificate).Parameters.ContainsKey('CryptoAlgorithmOption')) { $export.CryptoAlgorithmOption = 'AES256_SHA256' }
        $null = Export-PfxCertificate @export
        Write-Output "  Written to ${SparePfx}: move it off this server (offline, with its password kept apart)."
    } finally {
        Remove-Item -LiteralPath (Join-Path 'Cert:\CurrentUser\My' $spare.Thumbprint) -DeleteKey
    }
}

$pins = @()
if ($SpareOnly) {
    $cert = $existing | Sort-Object NotAfter -Descending | Select-Object -First 1
    $pins += Get-SpkiPin $cert
    Show-Certificate $cert "In use in ${store} (kept):" $pins[-1]
    New-Spare
} elseif ($ImportPfx) {
    if ($null -eq $ImportPassword) { $ImportPassword = Read-Host -AsSecureString "Password of $ImportPfx" }
    # Not exportable once installed: the PFX file stays the only copy that can leave the machine.
    $imported = @(Import-PfxCertificate -FilePath $ImportPfx -CertStoreLocation $store -Password $ImportPassword)
    $cert = $imported | Where-Object { $_.HasPrivateKey } | Select-Object -First 1
    if (-not $cert) { throw "$ImportPfx holds no certificate with a private key." }
    if (-not (Test-ForAddress $cert)) { Write-Warning "The imported certificate does not name $IpAddress; set Tls:StoreHostName to a name it holds." }
    $pins += Get-SpkiPin $cert
    Show-Certificate $cert "Imported into ${store}:" $pins[-1]
    Assert-ServicePin $cert.Thumbprint $pins[-1]
    Grant-KeyRead $cert
} else {
    $cert = New-IpCertificate 'NonExportable' "DW4Online $IpAddress"
    $pins += Get-SpkiPin $cert
    Show-Certificate $cert "Created in $store (the key cannot be exported):" $pins[-1]
    Assert-ServicePin $cert.Thumbprint $pins[-1]
    Grant-KeyRead $cert

    if ($SparePfx) { New-Spare }
}

# --- What goes where ------------------------------------------------------------------------------------------------

Write-Output ''
Write-Output 'For the game client build (DEPLOY.md 6.4: print-pins.ps1 -Out writes local/online_pins.txt; the first is in use):'
foreach ($pin in $pins) { Write-Output "  $pin" }
Write-Output ''
Write-Output 'appsettings.Production.json:'
Write-Output "  `"Kestrel`": { `"Endpoints`": { `"Https`": { `"Url`": `"https://0.0.0.0:$HttpsPort`" } } },"
Write-Output "  `"Tls`": { `"StoreHostName`": `"$IpAddress`" },"
Write-Output "  `"Service`": { `"RelayHost`": `"$IpAddress`" }"
$base = if ($HttpsPort -eq 443) { "https://$IpAddress" } else { "https://${IpAddress}:$HttpsPort" }
Write-Output "The game client's server address: $base"
Write-Output ''
Write-Output 'Then: Restart-Service DW4Online (or wait up to 10 minutes); its log names the certificate and the pin.'
