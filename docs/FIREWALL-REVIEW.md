# Firewall state observed during validation

Windows reported the active Public profile with the firewall enabled and the
default inbound action set to Block. Exact-application allow exceptions were
also present for the Debug/Release protocol tests and the Debug/staged test app.
These rules were enabled for the Public profile and allowed remote address `*`
separately for TCP and UDP. Their origin was not established; AirConnect has no
automatic rule-creation code in its normal startup path.

The agent attempted to narrow only the two initially discovered task executable
paths to Private/LocalSubnet. Windows returned E_ACCESSDENIED for all four rules.
No rule was changed. Further inspection after Release validation found matching
rules for the Release protocol test and staged test application as well.

An administrator should review the rules whose program paths are inside this
AirConnect workspace, restrict receiving to trusted Private networks and the
local subnet, and remove obsolete development-test exceptions. Preserve rules
for unrelated applications. No router port forwarding was configured.

The optional `tools/allow-private-firewall.ps1` is for the built application and
creates a narrow Private/LocalSubnet rule; it does not silently delete existing
allow/block rules. Existing broader exceptions therefore require review too.
The test application reports the Public-profile situation in Network settings.
