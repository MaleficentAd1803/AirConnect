param([Parameter(Mandatory=$true)][string]$Application)
$ErrorActionPreference='Stop'
$exe=(Resolve-Path -LiteralPath $Application).Path
if([IO.Path]::GetFileName($exe) -ne 'AirConnect.exe') { throw 'Select the built AirConnect.exe.' }
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
if(-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run this optional script in an administrator PowerShell session.' }
$name='AirConnect.Private.LocalSubnet'
$existing=Get-NetFirewallRule -Name $name -ErrorAction SilentlyContinue
if($existing) {
 $program=($existing | Get-NetFirewallApplicationFilter).Program
 if($program -ne $exe) { throw 'The existing AirConnect rule points to another installation. Review it in Windows Firewall before updating it.' }
 Write-Output 'The AirConnect rule already exists.'
} else {
 New-NetFirewallRule -Name $name -DisplayName 'AirConnect (private local network)' -Direction Inbound -Action Allow -Program $exe -Profile Private -RemoteAddress LocalSubnet -Protocol Any | Out-Null
 Write-Output 'Added one inbound application rule for Private networks and the local subnet.'
}
