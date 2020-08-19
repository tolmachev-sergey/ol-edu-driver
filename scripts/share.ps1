param(
    [switch]$Elevated,
    [String]$MoveTo
)

function Test-Admin {
  $currentUser = New-Object Security.Principal.WindowsPrincipal $([Security.Principal.WindowsIdentity]::GetCurrent())
  $currentUser.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)
}

if ((Test-Admin) -eq $false)  {
    if (!$elevated) {
        Start-Process powershell.exe `
            -Verb RunAs `
            -ArgumentList ('-ExecutionPolicy Unrestricted -File "{0}" -Elevated -MoveTo "{1}"' `
                           -f ($myinvocation.MyCommand.Definition, (Get-Location).Path)) `
            -WorkingDirectory (Get-Location).Path
    }

    exit
}

Set-Location -Path $MoveTo

if (!(Get-SmbShare -Name QemuShare -ErrorAction SilentlyContinue)) {
    Write-Output "Creating SMB share for the VM"
    Resolve-Path -Path ".\" | New-SmbShare -Name "QemuShare" | Grant-SmbShareAccess -AccountName Everyone -AccessRight Full
}

pause
