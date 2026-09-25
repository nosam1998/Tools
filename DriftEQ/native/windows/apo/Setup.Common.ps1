Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:DriftClsid = '{9D91F96A-E57F-4E78-A268-143693E09E66}'
$script:EfxName = '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7'
$script:ModesName = '{d3993a3f-99c2-4402-b5ec-a92a0367664b},7'
$script:LegacyName = '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2'
$script:CompositeName = '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},15'
$script:DefaultMode = '{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}'
$script:RenderRoot = 'SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render'
$script:ConfigRoot = 'SOFTWARE\DriftEQ\APO\Devices'

function ConvertTo-EndpointGuid([string]$EndpointId) {
    if ($EndpointId -notmatch '^\{0\.0\.0\.00000000\}\.({[0-9a-fA-F-]{36}})$') {
        throw 'Use the full playback endpoint ID shown by Inspect, not a device name or registry path.'
    }
    return ([guid]$Matches[1]).ToString('B').ToUpperInvariant()
}
function Get-ValueSnapshot($Key, [string]$Name) {
    $exists = $null -ne $Key -and @($Key.GetValueNames()) -contains $Name
    if (!$exists) { return [pscustomobject]@{ Name=$Name; Exists=$false; Kind=''; Data=$null } }
    return [pscustomobject]@{ Name=$Name; Exists=$true; Kind=$Key.GetValueKind($Name).ToString(); Data=$Key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) }
}
function Restore-ValueSnapshot($Key, $Snapshot) {
    if (!$Snapshot.Exists) { $Key.DeleteValue([string]$Snapshot.Name, $false); return }
    $kind = [Microsoft.Win32.RegistryValueKind]$Snapshot.Kind
    $value = $Snapshot.Data
    if ($kind -eq [Microsoft.Win32.RegistryValueKind]::MultiString) { $value = [string[]]@($value) }
    if ($kind -eq [Microsoft.Win32.RegistryValueKind]::Binary) { $value = [byte[]]@($value) }
    if ($kind -eq [Microsoft.Win32.RegistryValueKind]::DWord) { $value = [int]$value }
    if ($kind -eq [Microsoft.Win32.RegistryValueKind]::QWord) { $value = [long]$value }
    $Key.SetValue([string]$Snapshot.Name, $value, $kind)
}
function Get-EndpointBlocker($Key) {
    $modes = Get-ValueSnapshot $Key $script:ModesName
    if ($modes.Exists -and $modes.Kind -ne 'MultiString') { return 'The driver uses an unexpected endpoint-mode property type. Automatic attachment is unsupported.' }
    foreach ($name in @($script:LegacyName, $script:EfxName, $script:CompositeName)) {
        $snapshot = Get-ValueSnapshot $Key $name
        if ($snapshot.Exists -and @($snapshot.Data | Where-Object { $_ -and $_ -ne '{00000000-0000-0000-0000-000000000000}' }).Count) {
            if ($name -eq $script:EfxName -and $snapshot.Data -eq $script:DriftClsid) { return 'Drift is already attached. Remove it before replacing this preview.' }
            return 'An existing endpoint/post-mix effect is present. This installer will not replace or wrap another effect.'
        }
    }
    return $null
}
function Assert-OriginalOrOwned($Key, $Original, $Written) {
    $now = Get-ValueSnapshot $Key $Original.Name
    # Compare only our two properties, preserving unrelated driver changes.
    $a = $now | ConvertTo-Json -Compress -Depth 5
    $b = $Original | ConvertTo-Json -Compress -Depth 5
    $c = $Written | ConvertTo-Json -Compress -Depth 5
    if ($a -ne $b -and $a -ne $c) { throw "The driver changed $($Original.Name) after setup. Nothing has been restored; retain the backup for review." }
}
function Assert-TrustedPayload([string]$Directory) {
    foreach ($name in @('DriftApo.dll','DriftControl.exe')) {
        $file = Join-Path $Directory $name
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing package file: $name" }
        $signature = Get-AuthenticodeSignature -LiteralPath $file
        if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
            throw "Install refused: $name is not trust-signed. The preview can be tested with drift_apo_tests.exe without installation. A production APO also needs signing acceptable to the Windows audio engine."
        }
    }
}
function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal $identity
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Install and Remove require an administrator PowerShell. Inspect does not.' }
}
function Invoke-DriftRegistration([string]$Dll, [switch]$Remove) {
    $arguments = if ($Remove) { @('/u','/s',('"' + $Dll + '"')) } else { @('/s',('"' + $Dll + '"')) }
    $process = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList $arguments -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "APO registration returned exit code $($process.ExitCode)." }
}

function Save-SetupBackup([string]$Path, $State) {
    $temporary=$Path+'.pending'
    $State | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $temporary -Encoding UTF8
    if(Test-Path -LiteralPath $Path){[IO.File]::Replace($temporary,$Path,[NullString]::Value)}
    else{[IO.File]::Move($temporary,$Path)}
}
