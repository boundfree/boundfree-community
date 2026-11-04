#pragma once

#include <QString>
#include <QStringList>

namespace bf {

// Legacy-compatible machine fingerprint.
//
// Algorithm (matches legacy Python get_machine_id()):
//   SHA-256(MachineGuid.utf8)[:32].toUpper()
// MAC address is NOT part of the machine id. Any readPrimaryMacAddress() result
// is for non-license diagnostics only and must never enter the fingerprint.
//
// machineIdFromInputs(machineGuid) returns the dashed display form:
//   XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX
// (32 uppercase hex chars, grouped by 8, joined by '-'). This is the only symbol
// referenced by existing main.cpp smoke entry points and must remain stable.
//
// The second parameter (macAddress) is retained for ABI/source compatibility but
// is IGNORED. Existing callers passing (guid, mac) continue to produce the same
// legacy machine id as if only guid were used.
QString machineIdFromInputs(const QString& machineGuid, const QString& macAddress = QString());
QString machineIdFromGuid(const QString& machineGuid);

// Canonical form used for license payload comparison: 32 uppercase hex chars,
// no separators. The legacy keygen stores payload.machine_id in this form.
QString machineIdCanonical(const QString& machineGuid);
QString machineIdCanonicalFromFormatted(const QString& formattedMachineId);

// Platform machine fingerprint sources.
//
// readMachineGuid():
//   Windows: HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid (REG_SZ).
//   Non-Windows: BF_MOCK_MACHINE_GUID override, then the system machine-id files.
//
// readPrimaryMacAddress() [diagnostics only — NOT part of the machine id]:
//   Windows: first operational non-loopback adapter hardware address
//            (GetAdaptersAddresses), colon-separated uppercase hex.
//   Non-Windows: first non-loopback /sys/class/net/<iface>/address entry,
//                colon-separated lowercase hex; BF_MOCK_MAC override for dev.
//
// currentMachineId():
//   Dashed display fingerprint derived from the live platform MachineGuid.
//   Equivalent to machineIdFromGuid(readMachineGuid()).
//
// These accessors are pure data readers. On failure they return an empty string;
// callers decide how to report the resulting license status. They never surface
// user-visible localized text.
QString readMachineGuid();
QString readMachineGuidFromFiles(const QStringList& paths);
QString readPrimaryMacAddress();
QString currentMachineId();

}  // namespace bf
