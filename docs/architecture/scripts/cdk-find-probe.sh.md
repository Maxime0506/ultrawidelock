<!-- generated documentation — edit the source, not this file -->
# `scripts/cdk-find-probe.sh`

cdk-find-probe.sh — print the probe triple (VID:PID:Serial) wired to the DWM3001CDK.
Usage: cdk-find-probe.sh <cache-file>
stdout   the triple, or nothing when pinning is unnecessary
exit 0   triple printed, or nothing to do (probe-rs absent, 0 or 1 probe attached)
exit 1   several probes attached and the CDK could not be settled (reason on stderr)
WHY IDENTIFY BY SILICON. Probe enumeration order is not stable across replugs
(mk/cdk.mk measured it flipping between two `probe-rs list` calls with no cable
touched), and every J-Link OB calls itself "J-Link", so nothing in the listing
says which one sits on the DWM3001CDK. What does say so is the part behind the
probe: FICR INFO.PART at 0x10000100 reads 0x00052833 on an nRF52833 and the
read FAULTS through a probe wired to anything else (verified on the bench
against an nRF5340 DK). So with several probes attached, read that word
through each candidate and the CDK identifies itself.
The winning triple is cached in <cache-file> (under firmware/keys/, which is
deny-all gitignored -- a probe serial is machine-local state and must never be
committed). While the cached serial is attached it is trusted without touching
any probe, so the identification cost is paid once per bench, not per flash.
Unplugged the CDK for good, or moved the cache to the wrong board somehow?
Delete the cache file and the next probe-touching target re-identifies.
With zero or one probe attached this prints nothing and exits 0: one probe
needs no pinning (the tools pick it), and probe-rs being absent must not
become a new reason a flash cannot run -- both per the guard in mk/cdk.mk.
