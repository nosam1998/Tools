#requires -Version 5.1
[CmdletBinding(SupportsShouldProcess=$true)]
param(
    [ValidateSet('Inspect','Install','Remove')][string]$Mode='Inspect',
    [string]$EndpointId
)
. (Join-Path $PSScriptRoot 'Setup.Common.ps1')
if (![Environment]::Is64BitProcess) { throw 'Use 64-bit Windows PowerShell for this x64 preview.' }
$machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
$destination = Join-Path $env:ProgramFiles 'DriftEQ-APO\0.3.0'
$backups = Join-Path $destination 'Backups'
try {
    if ($Mode -eq 'Inspect') {
        $root = $machine.OpenSubKey($script:RenderRoot)
        if (!$root) { throw 'No Windows playback endpoints were found.' }
        try {
            foreach ($name in $root.GetSubKeyNames()) {
                $endpoint=$root.OpenSubKey($name); $properties=$endpoint.OpenSubKey('Properties'); $fx=$endpoint.OpenSubKey('FxProperties')
                try {
                    if ($endpoint.GetValue('DeviceState',0) -ne 1) { continue }
                    $friendly = if ($properties) { ([string]$properties.GetValue('{a45c254e-df1c-4efd-8020-67d146a850e0},2','Audio output') + ' (' + [string]$properties.GetValue('{b3f8fa53-0004-438e-9003-51a46e139bfc},6','device') + ')') } else { 'Audio output' }
                    $blocker=if($fx){Get-EndpointBlocker $fx}else{'No endpoint effects store is exposed. A driver-specific signed extension package is required.'}
                    [pscustomobject]@{
                        Name=$friendly
                        EndpointId=('{0.0.0.00000000}.'+$name)
                        Attachment=if($blocker){$blocker}else{'Candidate only; signing, permissions, driver compatibility and live playback still require validation.'}
                    }
                } finally { if($fx){$fx.Dispose()}; if($properties){$properties.Dispose()}; $endpoint.Dispose() }
            }
        } finally { $root.Dispose() }
        return
    }
    $guid=ConvertTo-EndpointGuid $EndpointId
    $endpointPath="$script:RenderRoot\$guid"
    $configPath="$script:ConfigRoot\$guid"
    $backupFile=Join-Path $backups ($guid+'.json')
    $endpoint=$machine.OpenSubKey($endpointPath)
    if (!$endpoint) { throw 'That playback endpoint does not exist.' }
    $endpoint.Dispose()
    if ($Mode -eq 'Install') {
        # No filesystem, settings, COM, endpoint or audio-service mutation before
        # signatures, compatibility, existing state and write access are checked.
        Assert-TrustedPayload $PSScriptRoot
        Assert-Administrator
        if (Test-Path -LiteralPath $backupFile) { throw 'A saved setup state exists. Use Remove first; its backup must not be overwritten.' }
        $config=$machine.OpenSubKey($configPath)
        if($config){$config.Dispose(); throw 'Unowned Drift settings already exist for this endpoint; setup stopped.'}
        $fx=$machine.OpenSubKey("$endpointPath\FxProperties",$true)
        if(!$fx){throw 'The endpoint has no writable effects store. A driver-specific signed extension package is required; this tool does not change driver registry permissions.'}
        try {
            $blocker=Get-EndpointBlocker $fx
            if($blocker){throw $blocker}
            $original=@((Get-ValueSnapshot $fx $script:EfxName),(Get-ValueSnapshot $fx $script:ModesName))
            $written=@(
                [pscustomobject]@{Name=$script:EfxName;Exists=$true;Kind='String';Data=$script:DriftClsid},
                [pscustomobject]@{Name=$script:ModesName;Exists=$true;Kind='MultiString';Data=@($script:DefaultMode)}
            )
            $dll=Join-Path $destination 'DriftApo.dll'
            $registrationPath="SOFTWARE\Classes\CLSID\$script:DriftClsid\InprocServer32"
            $existing=$machine.OpenSubKey($registrationPath)
            $alreadyRegistered=$null -ne $existing
            if($existing) {
                try { if($existing.GetValue('') -ne $dll){throw 'This APO CLSID is registered to another location; setup stopped.'} }
                finally {$existing.Dispose()}
            }
            if(Test-Path -LiteralPath $dll) {
                if((Get-FileHash -LiteralPath $dll).Hash -ne (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'DriftApo.dll')).Hash){throw 'A different build already occupies this version directory. Remove that build before updating.'}
            }
            if(!$PSCmdlet.ShouldProcess($EndpointId,'Install signed Drift APO and attach it to this playback endpoint')){return}
            New-Item -ItemType Directory -Path $backups -Force | Out-Null
            foreach($name in @('DriftApo.dll','DriftControl.exe','README-APO.md','Setup.ps1','Setup.Common.ps1')){
                $source=Join-Path $PSScriptRoot $name; $target=Join-Path $destination $name
                if([IO.Path]::GetFullPath($source) -ne [IO.Path]::GetFullPath($target)){Copy-Item -LiteralPath $source -Destination $target -Force}
            }
            $backup=[pscustomobject]@{Schema=1;EndpointId=$EndpointId;Guid=$guid;Original=$original;Written=$written;State='Prepared'}
            Save-SetupBackup $backupFile $backup
            $registeredHere=$false; $createdSettings=$false
            try {
                if(!$alreadyRegistered){Invoke-DriftRegistration $dll; $registeredHere=$true}
                $config=$machine.CreateSubKey($configPath)
                try {
                    $createdSettings=$true
                    # Disabled by default. Only this dedicated settings key is
                    # writable by local users; DLLs/COM/endpoint values stay protected.
                    $config.SetValue('Parameters',0,[Microsoft.Win32.RegistryValueKind]::DWord)
                    $acl=$config.GetAccessControl()
                    $users=New-Object Security.Principal.SecurityIdentifier 'S-1-5-32-545'
                    $rights=[Security.AccessControl.RegistryRights]::ReadKey -bor [Security.AccessControl.RegistryRights]::SetValue
                    $rule=New-Object Security.AccessControl.RegistryAccessRule($users,$rights,[Security.AccessControl.AccessControlType]::Allow)
                    $acl.AddAccessRule($rule); $config.SetAccessControl($acl)
                } finally {$config.Dispose()}
                $fx.SetValue($script:ModesName,[string[]]@($script:DefaultMode),[Microsoft.Win32.RegistryValueKind]::MultiString)
                $fx.SetValue($script:EfxName,$script:DriftClsid,[Microsoft.Win32.RegistryValueKind]::String)
                $backup.State='Attached'
                Save-SetupBackup $backupFile $backup
                Write-Output 'Registered and attached, with the effect disabled. Restart Windows before testing. Audio-engine loading and compatibility are not yet verified. Run DriftControl.exe to enable the effect.'
            } catch {
                $failure=$_
                # Leave the backup in place until every restoration step succeeds.
                for($i=0;$i -lt 2;++$i){Assert-OriginalOrOwned $fx $original[$i] $written[$i]}
                foreach($v in $original){Restore-ValueSnapshot $fx $v}
                if($createdSettings){$machine.DeleteSubKeyTree($configPath,$false)}
                if($registeredHere){Invoke-DriftRegistration $dll -Remove}
                Remove-Item -LiteralPath $backupFile
                throw $failure
            }
        } finally {$fx.Dispose()}
    } else {
        Assert-Administrator
        if(!(Test-Path -LiteralPath $backupFile)){throw 'No backup exists for this endpoint. Removal will not guess its former driver configuration.'}
        $backup=Get-Content -LiteralPath $backupFile -Raw | ConvertFrom-Json
        if($backup.Schema -ne 1 -or $backup.Guid -ne $guid -or $backup.EndpointId -ne $EndpointId){throw 'The saved setup state does not match this endpoint.'}
        $fx=$machine.OpenSubKey("$endpointPath\FxProperties",$true)
        if(!$fx){throw 'The effects store is unavailable. Keep the backup; driver-specific recovery is required.'}
        try {
            for($i=0;$i -lt 2;++$i){Assert-OriginalOrOwned $fx $backup.Original[$i] $backup.Written[$i]}
            if(!$PSCmdlet.ShouldProcess($EndpointId,'Disable Drift and restore the saved endpoint effects')){return}
            $config=$machine.OpenSubKey($configPath,$true)
            if($config){try{$config.SetValue('Parameters',0,[Microsoft.Win32.RegistryValueKind]::DWord)}finally{$config.Dispose()}}
            foreach($v in $backup.Original){Restore-ValueSnapshot $fx $v}
            $machine.DeleteSubKeyTree($configPath,$false)
            # Detachment is recorded before unregistering. DLL files remain on
            # disk because audiodg may still hold them until the next restart.
            Move-Item -LiteralPath $backupFile -Destination ($backupFile+'.removed') -Force
            if(@(Get-ChildItem -LiteralPath $backups -Filter '*.json').Count -eq 0){Invoke-DriftRegistration (Join-Path $destination 'DriftApo.dll') -Remove}
            Write-Output 'Restored the original endpoint values. Restart Windows to unload the effect. After all endpoints are removed and Windows has restarted, the DriftEQ-APO folder may be deleted.'
        } finally {$fx.Dispose()}
    }
} finally {$machine.Dispose()}
