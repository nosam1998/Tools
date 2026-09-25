# Exercises recovery against a unique temporary CURRENT USER key only.
# Never installs a DLL or touches machine audio configuration.
. (Join-Path $PSScriptRoot 'Setup.Common.ps1')
function Assert($Condition,[string]$Message){if(!$Condition){throw $Message}}
$path='SOFTWARE\DriftEQ-SetupTests\'+[guid]::NewGuid().ToString('N')
$root=[Microsoft.Win32.Registry]::CurrentUser
$key=$root.CreateSubKey($path)
try {
    $id='{0.0.0.00000000}.{12345678-1234-1234-1234-1234567890ab}'
    Assert ((ConvertTo-EndpointGuid $id) -eq '{12345678-1234-1234-1234-1234567890AB}') 'Canonical endpoint parsing'
    $rejected=$false; try{ConvertTo-EndpointGuid '..\OtherDevice'}catch{$rejected=$true}; Assert $rejected 'Reject invalid endpoint paths'
    Assert ($null -eq (Get-EndpointBlocker $key)) 'Empty effects store can be evaluated'
    $snapshot=Get-ValueSnapshot $key $script:EfxName
    $key.SetValue($script:EfxName,$script:DriftClsid,[Microsoft.Win32.RegistryValueKind]::String)
    Assert ($null -ne (Get-EndpointBlocker $key)) 'Do not replace installed effects'
    $written=Get-ValueSnapshot $key $script:EfxName
    Assert-OriginalOrOwned $key $snapshot $written
    Restore-ValueSnapshot $key $snapshot
    Assert (!(Get-ValueSnapshot $key $script:EfxName).Exists) 'Restore absence, not an empty string'
    $key.SetValue($script:ModesName,[string[]]@('first','second'),[Microsoft.Win32.RegistryValueKind]::MultiString)
    $modes=Get-ValueSnapshot $key $script:ModesName
    $serialized=$modes | ConvertTo-Json -Depth 5 | ConvertFrom-Json
    $key.DeleteValue($script:ModesName)
    Restore-ValueSnapshot $key $serialized
    Assert (($key.GetValue($script:ModesName) -join ',') -eq 'first,second') 'Restore multi-string contents after JSON round trip'
    $key.SetValue($script:EfxName,'{AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA}',[Microsoft.Win32.RegistryValueKind]::String)
    $rejected=$false; try{Assert-OriginalOrOwned $key $snapshot $written}catch{$rejected=$true}; Assert $rejected 'Driver changes prevent rollback overwrite'
    $key.DeleteValue($script:EfxName)
    $key.SetValue($script:CompositeName,[string[]]@('{BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB}'),[Microsoft.Win32.RegistryValueKind]::MultiString)
    Assert ($null -ne (Get-EndpointBlocker $key)) 'Preserve composite effect chains'
    $directory=Join-Path ([IO.Path]::GetTempPath()) ('DriftEQ-BackupTest-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $directory | Out-Null
    try {
        $file=Join-Path $directory 'state.json'
        Save-SetupBackup $file ([pscustomobject]@{State='Prepared';Original=@($snapshot,$modes)})
        Save-SetupBackup $file ([pscustomobject]@{State='Attached';Original=@($snapshot,$modes)})
        $saved=Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
        Assert ($saved.State -eq 'Attached' -and $saved.Original.Count -eq 2) 'Atomic backup replacement preserves recovery state'
        Assert (!(Test-Path -LiteralPath ($file+'.pending'))) 'No pending backup remains after commit'
    } finally {Remove-Item -LiteralPath $directory -Recurse}
    Write-Output 'PASS: setup ID validation, existing-effect protection, exact restoration, JSON backups, and conflict detection'
} finally {$key.Dispose(); $root.DeleteSubKeyTree($path,$false)}
