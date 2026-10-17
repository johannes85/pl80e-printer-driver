# PL80E Open Source Driver

The original POLONO PL80E printer driver (v4.1) has some serious problems with rasterizing the documents before printing.
This results in mangeled text and wrong dimensions.

I took the opportunity to test if ChatGPTs Codex is capable of generating a functioning open source driver for me,
which it did successfully thanks to [REA](https://rea.tools/).

I didn't review the code in depth, but the driver itself seems to work - at least for my usecase
which is the printing of shipping labels.

The provided release version isn't officially signed but can be installed with the provided setup application,
which self signes it for the current machine it is installed on.

Author: Johannes Zinnau and (mostly) ChatGPT  
Project Page: https://github.com/johannes85/pl80e-printer-driver

## Requirements

- Windows x64 with **64-bit PowerShell 7**.
- Internet access for the first download of the build tools.
- The complete project source folder.

You do not need to install Visual Studio, Zig, .NET or the Windows SDK manually.
Building does not require administrator privileges or a connected printer.

## 1. Open the project folder

Download or clone the source, then open PowerShell 7 in the project folder.
Alternatively, change to it:

```powershell
cd 'C:\path\to\PL80E-Open-Source'
```

Replace the example path with your actual project folder.

## 2. Download the build tools

Run this once:

```powershell
.\Prepare-BuildTools.ps1
```

This downloads Zig 0.13.0, the required MinGW-w64 headers, Microsoft SDK/WDK
build tools and the portable .NET 10 SDK into `tools/`. The first setup publish
also restores Windows runtime packs. If you already prepared the native tools,
run `Prepare-SetupTools.ps1` once to add the .NET SDK.

## 3. Build the driver

```powershell
.\Build.ps1
```

The script builds both variants, runs the raster and driver-dialog tests, and
creates the release catalog. Wait for all tests to pass and the script to finish.
After changing the source, repeat this step; the tools do not need downloading again.

## Build output

| Output | Location |
| --- | --- |
| Release installer package | `dist/PL80E-Open-Source-<version>.zip` |
| Unpacked release package | `dist/PL80E-Open-Source/` |
| Compact installer package | `dist/PL80E-Open-Source-<version>-compact.zip` |
| Unpacked compact package | `dist/PL80E-Open-Source-Compact/` |
| Development build | `dist/PL80E-Open-Source-DEV/` |

The release ZIP contains the driver, **PL80ESetup.exe**, README and license
notices. Extract it, open PL80ESetup.exe and choose a USB port, then click
**Install / update**. The same GUI uninstalls the release. It creates a local
signing certificate and signs the catalog on the target PC, without PowerShell.
The WinForms application includes .NET 10, so no separate runtime is needed.

Setup source is in `setup/`. To build only the setup EXE, run `Build-Setup.ps1`.
For a smaller variant requiring the .NET 10 Windows Desktop Runtime, use
`Build-Setup.ps1 -FrameworkDependent -OutputDirectory build/setup-framework-dependent`.
Build.ps1 automatically creates both release ZIPs. The normal release includes
the runtime; the compact package requires the .NET 10 Windows Desktop Runtime (x64)
on the target PC. Its README includes the download link.

The development driver uses **PL80E Open Source (DEV)** and separate driver files,
so it can coexist with the release. Build.ps1 does not sign it. For local DEV
signing, run:

```powershell
.\Prepare-Signing.ps1
```

This creates the signed DEV package and a local development identity. It does
not install the driver or trust the certificate. Keep `build/signing/` private.
The installation scripts in the repository root target DEV; release packages
use the setup EXE.

## Notes

- If PowerShell blocks downloaded scripts, verify their source and review your
  system's execution policy or ask your administrator.
- Build tests do not print labels or install a driver.
- Original project code and build scripts use the [MIT license](LICENSE).
  Downloaded tools retain their own licenses.
- [README_LONG.md](README_LONG.md) preserves the earlier development notes.
  Historical names and paths there may refer to older versions.
