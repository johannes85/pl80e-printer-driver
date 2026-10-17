# PL80E Open Source Driver — Compact Setup

The original POLONO PL80E printer driver (v4.1) has some serious problems with rasterizing the documents before printing.
This results in mangeled text and wrong dimensions.

I took the opportunity to test if ChatGPTs Codex is capable of generating a functioning open source driver for me,
which it did successfully.

I din't review the code in depth, but the driver itself seems to work - at least for my usecase
which is the printing of shipping labels.

The provided release version isn't officially signed but can be installed with the provided setup application,
which self signes it for the current machine it is installed on.

Project code and scripts are available under the MIT license; see LICENSE.

Author: Johannes Zinnau and (mostly) ChatGPT  
Project Page: https://github.com/johannes85/pl80e-printer-driver

This compact package requires the Microsoft .NET 10 Windows Desktop Runtime (x64). Download it from https://dotnet.microsoft.com/en-us/download/dotnet/10.0 (Windows Desktop Runtime, x64).

## Install

1. Extract the entire ZIP to a local folder.
2. Connect and power on the printer.
3. Double-click **PL80ESetup.exe** and approve the Windows administrator prompt.
4. Select the correct USB port. **PL80E Open Source Driver installed** indicates that the driver already has a queue on that port; use **Uninstall** to remove it. Click **Refresh ports** if needed.
5. Click **Install / update** and wait for the success message.

The printer queue is **PL80E Open Source**. The original **PL80E** installation remains unchanged. Installation does not print a page or change the default printer. Rerun the new setup application to update. Uninstall first if you need to change an existing queue's port.

The setup EXE itself is not signed with a publicly trusted publisher certificate. Verify the download source. Organization policies may require your administrator to approve locally signed drivers.

## Settings

Open **Printing preferences → Label**. Defaults: **100 × 150 mm**, portrait, one copy, **5 ips**, density **10**, gap media, **3 mm** gap. Change the size to match your actual labels. Media choices are gap, black mark and continuous; black mark uses the gap field for mark height. **Restore defaults** takes effect when you press Apply or OK.

Restart printing applications after an update; Firefox can keep the previous driver loaded. Start with one test label and select actual size / 100% when appropriate.

## Local signing

Setup creates a dedicated **non-exportable, five-year code-signing certificate** on this PC, signs the supplied driver catalog and adds that certificate to the machine's **Trusted Root Certification Authorities** and **Trusted Publishers** stores. This trusts other code signed with the same key as well. The private key is never included in this ZIP.

This is a locally trusted driver, **not Microsoft/WHQL certified**. Signatures have no timestamp; renew or replace the installation before the certificate expires. Setup reports its expiry date. Secure Boot and Windows signature enforcement are not disabled. Installation state and the signed copy are kept in `%ProgramData%\PL80E Open Source`.

## Uninstall

Close printing applications and finish or cancel queued jobs. Open **PL80ESetup.exe**, click **Uninstall** and confirm.

Setup removes its recorded queues, the driver, its Driver Store packages and this installer's certificate/private key. It stops if other queues or Windows still use the driver. Resolve the reported issue and retry; a restart may be necessary. It does not remove the original PL80E driver.

The GUI recognizes installation state from the previous self-signing release scripts. It can also update or remove an older release registration without that state; signing certificates it does not own are retained. Keep the extracted setup files for later removal or updates.

The .NET host has license notices in DOTNET_LICENSE.txt and DOTNET_NOTICES.txt.
