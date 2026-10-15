# PL80E Open Source Driver

The original POLONO PL80E printer driver (v4.1) has some serious problems with rasterizing the documents before printing.
This results in mangeled text and wrong dimensions.

I took the opportunity to test if ChatGPTs Codex is capable of generating a functioning open source driver for me,
which it did successfully.

I din't review the code in depth, but the driver itself seems to work - at least for my usecase
which is the printing of shipping labels.

The provided release version isn't officially signed but can be installed with the provided install script,
which self signes it for the current machine it is installed on.

Project code and scripts are available under the MIT license; see LICENSE.

Author: Johannes Zinnau and (mostly) ChatGPT  
Project Page: https://github.com/johannes85/pl80e-printer-driver

## Install

Extract the entire ZIP to a local folder. Open **64-bit Windows PowerShell as administrator**, change to that folder and run:

```powershell
.\Install.ps1
```

The default queue is **PL80E Open Source**, using **USB001**. To use another existing printer port:

```powershell
.\Install.ps1 -PortName USB002
```

Find the correct port under the original printer's **Printer properties → Ports**. The original **PL80E** queue is retained. Installation does not print a page or change the default printer. Rerun Install.ps1 from a newer package to update this installation.

If Windows blocks the downloaded scripts, verify the source, unblock the ZIP in its file Properties and extract it again. Organization policies may require your administrator to approve scripts or locally signed drivers.

## Settings

Open **Printing preferences → Label**. Defaults: **100 × 150 mm**, portrait, one copy, **5 ips**, density **10**, gap media, **3 mm** gap. Change the size to match your actual labels. Media choices are gap, black mark and continuous; black mark uses the gap field for mark height. **Restore defaults** takes effect when you press Apply or OK.

Restart printing applications after an update; Firefox can keep the previous driver loaded. Start with one test label and select actual size / 100% when appropriate.

## Local signing

Install.ps1 creates a dedicated **non-exportable, five-year code-signing certificate** on this PC, signs the supplied driver catalog and adds that certificate to the machine's **Trusted Root Certification Authorities** and **Trusted Publishers** stores. This trusts other code signed with the same key as well. The private key is never included in this ZIP.

This is a locally trusted driver, **not Microsoft/WHQL certified**. Signatures have no timestamp; renew or replace the installation before the certificate expires. Install.ps1 reports its expiry date. Secure Boot and Windows signature enforcement are not disabled. Installation state and the signed copy are kept in `%ProgramData%\PL80E Open Source`.

## Uninstall

Close printing applications and finish or cancel queued jobs, then run in an administrator PowerShell:

```powershell
.\Uninstall.ps1
```

The script removes queues recorded by this installer, the registered driver, its Driver Store package and this installer's certificate/private key. It stops if other queues or Windows still use the driver. Resolve the reported issue and rerun it. It does not remove the original PL80E driver or older development certificates.

Use this Uninstall.ps1 for installations made with the matching self-signing Install.ps1. Earlier development installations have separate removal scripts.
