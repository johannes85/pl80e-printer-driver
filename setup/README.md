# C# Windows Forms setup

The release setup is a Windows x64, .NET 10 WinForms application. Build it with
`Build-Setup.ps1`, or build the whole driver release with `Build.ps1`.
The default publish is self-contained and single-file. Its approximately 50 MB
EXE includes .NET and extracts native runtime libraries as required by .NET.
It requires elevation through its Windows application manifest.

Production setup does not run PowerShell. It uses the spooler and SetupAPI
through P/Invoke, CNG/X509 certificate APIs, and the Windows Authenticode signer.
Driver Store cleanup calls Windows' pnputil.exe with a validated `oemNN.inf`
argument; it never uses `/force` or removes the separate DEV registration.

Main files:

- `Program.cs`: GUI, USB selection, responsive operation log and self-tests.
- `Installer.cs`: installation state, certificate lifecycle, install/update/remove.
- `CatalogSigner.cs`: catalog signing and Windows trust validation.
- `Native.cs`: native printing/SetupAPI declarations and bounded enumeration.

The unsigned catalog produced by Build.ps1 covers the unchanged driver payload.
The setup signs that catalog on the target computer; it does not change the DLL.
Installation state is compatible with the former release PowerShell installer.
Older release registrations without state can be adopted or removed, but their
certificates are retained because they may be shared with a development driver.

## Validation

`--self-test <report.json> <disposable-catalog-copy>` runs native structure,
enumeration, state compatibility, GUI rendering and catalog signing tests. It
uses a temporary current-user signing key which is deleted afterwards. It does
not install drivers or trust certificates. Invoke the build DLL through the
portable dotnet host when elevation is not needed.

For integration testing only, build with `-p:IntegrationTest=true` into a separate
output directory. That compile-time switch changes the driver, certificate and
payload identities to `PL80E Open Source Setup Test`, permits the FILE port and
adds `--integration-test <test-payload-folder> <result.txt>`. Test payload files
are named PL80ESetupTest.dll/.inf/.gpd/.cat. The test checks installation, update,
removal and preservation of unowned certificates. It requires elevation and
must never be used as the release publish. Normal builds do not include that
test command or permit FILE installation.

Tests for this migration passed using an isolated FILE test queue. The existing
release and DEV driver hashes were checked before and after and were unchanged.
No physical labels were printed by the tests.
