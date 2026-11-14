// Pegaso: antivirus check of finished downloads.
//
// The file is handed to the antivirus installed on the computer through the Windows
// Antimalware Scan Interface (AMSI) - Microsoft Defender, or whichever product registered
// itself. The scan runs in the background; a detection is reported in the log and with a
// notification. Pegaso does not delete or move anything: that is left to the antivirus.
#pragma once

// Queues a file for scanning. Files of more than 512 MB are not scanned.
void	PegasoScanFile(const CString &sPath);

// Main thread, called regularly: reports the results.
void	PegasoScanProcess();
