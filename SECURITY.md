# Security policy

Please use GitHub's private vulnerability reporting feature when available.
Include the affected version, the operation and a description of impact.
Keep private recordings, device identifiers and credentials out of reports.

Surco treats archive and input lengths as untrusted. Integrity checks use
CRC32C and provide no authentication. The CLI stages named outputs and
creates new destinations exclusively. Serial access requires an explicit
device path and is limited to reading measurements and configuring its TTY.
