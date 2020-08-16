Import-Module BitsTransfer

$imgSrc = "https://releases.ubuntu.com/20.04.1/ubuntu-20.04.1-live-server-amd64.iso"
$folder = ".\vm\"
$imgDst = $folder + "disk.iso"
$hddFilename = $folder + "hdd.qcow2"
$hddSize = "20G"

if (!(Test-Path $folder)) {
    New-Item -Path $folder -ItemType Directory -Force
}

if (!(Test-Path $imgDst)) {
    Write-Output "Downloading system image"
    Start-BitsTransfer -Source $imgSrc -Destination $imgDst
}

if (!(Test-Path $hddFilename)) {
    Write-Output "Creating HDD image"
    qemu-img.exe create -f qcow2 $hddFilename $hddSize
}

pause
