# Native Windows setup

The setup is written in C using the Windows API. `Build-Setup.ps1` builds it
with the same portable Zig 0.13.0 compiler and resource compiler as the driver.
It has no .NET dependency. `Build.ps1` includes the EXE in the release ZIP.

`setup.c` contains the GUI, USB printer-port enumeration, bounded JSON state
reader, certificate lifecycle and install/update/uninstall operations.
`setup.rc` embeds version information and the administrator manifest.
Operations run on a worker thread while the GUI displays progress.

Setup uses the Windows spooler, SetupAPI, CNG, Crypt32 and Authenticode APIs.
It generates a non-exportable machine RSA signing key and a five-year code
signing certificate, signs the catalog and verifies trust before registration.
It does not modify the driver DLL. Driver Store cleanup calls the system
pnputil.exe with a validated oemNN.inf name, without /force.

Installation state remains compatible with the previous C# and release-script
format in `%ProgramData%\PL80E Open Source\installation.json`. Directories have
protected administrator/SYSTEM ACLs. Removal validates queue, package and
certificate identities. Certificates belonging to older registrations without
state are retained. Unrelated printer drivers are preserved.

## Validation

`PL80ESetup.exe --self-test <report.txt>` checks USB enumeration, accepted port
names, legacy state, malformed JSON and invalid package rejection. It does not
install anything. It also validates existing installation state and saves a GUI
snapshot as `<report.txt>.bmp`. The EXE requires elevation through its manifest.

For isolated integration testing, use:

```powershell
.\Build-Setup.ps1 -IntegrationTest -OutputDirectory build\setup-native-test
```

Only this test build accepts `--integration-test <payload-folder> <report.txt>`
and the FILE: port. Its driver name, certificate subject, state directory and
payload filenames are separate: `PL80E Open Source Setup Test` and
`PL80ESetupTest.dll/.gpd/.inf/.cat`. Use a catalog generated for that identity.
The elevated test checks install, update, signing-identity reuse, removal and
preservation of an unowned certificate. It cleans up its own test installation.
Normal builds do not include the integration command or permit FILE: installs.
