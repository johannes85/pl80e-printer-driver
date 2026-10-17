using System.Diagnostics;
using System.Security.AccessControl;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace PL80E.Setup;

internal sealed class InstallationState
{
    public string DriverName {get;set;}=Installer.DriverName;
    public string Thumbprint {get;set;}="";
    public string[] Queues {get;set;}=[];
    public string? InfPath {get;set;}
    public string[] Packages {get;set;}=[];
    public bool OwnsCertificate {get;set;}=true;
}

internal sealed class Installer(Action<string> log)
{
#if SETUP_INTEGRATION_TEST
    internal const string DriverName="PL80E Open Source Setup Test";
    internal const string Subject="CN=PL80E Open Source Setup Test";
    internal static readonly string[] Payload=["PL80ESetupTest.dll","PL80ESetupTest.gpd","PL80ESetupTest.inf","PL80ESetupTest.cat"];
#else
    internal const string DriverName="PL80E Open Source";
    internal const string Subject="CN=PL80E Open Source Local Installation";
    internal static readonly string[] Payload=["PL80EClean.dll","PL80EClean.gpd","PL80EClean.inf","PL80EClean.cat"];
#endif
    internal static readonly string Root=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),DriverName);
    internal static readonly string StatePath=Path.Combine(Root,"installation.json");
    private static readonly JsonSerializerOptions JsonOptions=new() {WriteIndented=true,PropertyNameCaseInsensitive=true};

    internal static InstallationState? ReadState()
    {
        if(!File.Exists(StatePath)) return null;
        return ParseState(File.ReadAllText(StatePath));
    }
    internal static InstallationState ParseState(string json)
    {
        var state=JsonSerializer.Deserialize<InstallationState>(json,JsonOptions) ?? throw new InvalidDataException("Invalid installation state.");
        if(state.DriverName!=DriverName || (state.OwnsCertificate && !Regex.IsMatch(state.Thumbprint,@"\A[A-Fa-f0-9]{40}\z")) || (!state.OwnsCertificate && state.Thumbprint!="") || state.Queues is null || state.Packages is null ||
           state.Queues.Any(q=>string.IsNullOrWhiteSpace(q) || q.Contains("(DEV)",StringComparison.OrdinalIgnoreCase)))
            throw new InvalidDataException("Installation state does not belong to this installer.");
        foreach(var package in state.Packages) ValidatePublishedName(package);
        return state;
    }
    private static void ProtectDirectory(string path)
    {
        Directory.CreateDirectory(path);
        if((File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0) throw new IOException("Installation directory must not be a link.");
        var acl=new DirectorySecurity(); acl.SetAccessRuleProtection(true,false);
        acl.SetOwner(new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid,null));
        foreach(var sid in new[]{WellKnownSidType.LocalSystemSid,WellKnownSidType.BuiltinAdministratorsSid})
            acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(sid,null),FileSystemRights.FullControl,InheritanceFlags.ContainerInherit|InheritanceFlags.ObjectInherit,PropagationFlags.None,AccessControlType.Allow));
        new DirectoryInfo(path).SetAccessControl(acl);
    }
    private static void Save(InstallationState state)
    {
        string temporary=StatePath+".new";
        if(File.Exists(temporary)) File.Delete(temporary);
        using(var stream=new FileStream(temporary,FileMode.CreateNew,FileAccess.Write,FileShare.None))
            JsonSerializer.Serialize(stream,state,JsonOptions);
        File.Move(temporary,StatePath,true);
    }
    private static X509Certificate2? FindCertificate(string thumbprint)
    {
        using var store=new X509Store(StoreName.My,StoreLocation.LocalMachine);store.Open(OpenFlags.ReadOnly);
        var found=store.Certificates.Find(X509FindType.FindByThumbprint,thumbprint,false);
        return found.Count==0?null:found[0];
    }
    private static void ValidateCertificate(X509Certificate2 cert)
    {
        var eku=cert.Extensions.OfType<X509EnhancedKeyUsageExtension>().SingleOrDefault();
        var basic=cert.Extensions.OfType<X509BasicConstraintsExtension>().SingleOrDefault();
        if(cert.Subject!=Subject || basic?.CertificateAuthority==true || eku is null || eku.EnhancedKeyUsages.Count!=1 || eku.EnhancedKeyUsages[0].Value!="1.3.6.1.5.5.7.3.3")
            throw new InvalidDataException("Unexpected signing certificate. No certificate was removed or trusted.");
    }
    private static X509Certificate2 CreateCertificate()
    {
        using var key=CngKey.Create(CngAlgorithm.Rsa,"PL80E-"+Guid.NewGuid().ToString("N"),new CngKeyCreationParameters {
            Provider=CngProvider.MicrosoftSoftwareKeyStorageProvider,KeyCreationOptions=CngKeyCreationOptions.MachineKey,
            KeyUsage=CngKeyUsages.Signing,ExportPolicy=CngExportPolicies.None,
            Parameters={new CngProperty("Length",BitConverter.GetBytes(3072),CngPropertyOptions.None)}
        });
        try {
            using var rsa=new RSACng(key);
            var request=new CertificateRequest(Subject,rsa,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);
            request.CertificateExtensions.Add(new X509BasicConstraintsExtension(false,false,0,true));
            request.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature,true));
            request.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension(new OidCollection {new("1.3.6.1.5.5.7.3.3")},true));
            using var cert=request.CreateSelfSigned(DateTimeOffset.Now.AddMinutes(-5),DateTimeOffset.Now.AddYears(5));
            using var store=new X509Store(StoreName.My,StoreLocation.LocalMachine);store.Open(OpenFlags.ReadWrite);store.Add(cert);
            return FindCertificate(cert.Thumbprint) ?? throw new InvalidOperationException("Certificate persistence failed.");
        } catch {key.Delete();throw;}
    }
    private static string? PublishedName(string? storeInf)
    {
        if(string.IsNullOrWhiteSpace(storeInf)) return null;
        var buffer=new StringBuilder(32768);
        if(!Native.SetupGetInfPublishedNameW(storeInf,buffer,(uint)buffer.Capacity,out _)) return null;
        string name=Path.GetFileName(buffer.ToString());ValidatePublishedName(name);return name;
    }
    private static string? ExistingInf()
    {
        using var key=Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\Print\Environments\Windows x64\Drivers\Version-3\"+DriverName);
        return key?.GetValue("InfPath") as string;
    }
    private static void ValidatePublishedName(string name)
    {
        if(!Regex.IsMatch(name,@"\Aoem\d+\.inf\z",RegexOptions.IgnoreCase)) throw new InvalidDataException("Unexpected Driver Store package name.");
    }
    internal void Install(string port,string source)
    {
        bool allowed=Native.UsbPorts().Contains(port,StringComparer.OrdinalIgnoreCase);
#if SETUP_INTEGRATION_TEST
        allowed=port=="FILE:";
#endif
        if(!allowed) throw new InvalidOperationException("Select an existing USB printer port. Connect and power on the printer, then refresh.");
        foreach(string file in Payload) if(!File.Exists(Path.Combine(source,file))) throw new FileNotFoundException("Extract the complete setup ZIP beside PL80ESetup.exe.",file);
        var printers=Native.Printers(); var existing=printers.SingleOrDefault(p=>p.Name==DriverName);
        if(existing.Name!=null && (existing.Driver!=DriverName || existing.Port!=port))
            throw new InvalidOperationException("The printer already exists on a different port or driver. Uninstall it first; its existing settings have not been changed.");
        if(printers.Any(p=>p.Driver==DriverName && p.Jobs!=0)) throw new InvalidOperationException("Finish or cancel print jobs before updating.");
        ProtectDirectory(Root);
        var state=ReadState();
        string? previousInf=state?.InfPath ?? ExistingInf();
        var inheritedPackages=state?.Packages ?? [];
        if(state is {OwnsCertificate:false}) state=null;
        using var cert=state is null?CreateCertificate():FindCertificate(state.Thumbprint) ?? throw new InvalidOperationException("The recorded certificate is missing. Uninstall or recover the prior installation first.");
        ValidateCertificate(cert);
        if(!cert.HasPrivateKey || cert.NotAfter<DateTime.Now.AddDays(30)) throw new InvalidOperationException("The local signing identity is unavailable or expires soon. Uninstall before replacing it.");
        state ??=new InstallationState {Thumbprint=cert.Thumbprint,InfPath=previousInf,Packages=inheritedPackages,Queues=printers.Where(p=>p.Driver==DriverName).Select(p=>p.Name!).ToArray()};
        state.Queues=state.Queues.Append(DriverName).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        string? oldPackage=PublishedName(state.InfPath);
        if(oldPackage is not null) state.Packages=state.Packages.Append(oldPackage).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        Save(state); // Recovery after partial installation, before trusting or staging anything.
        string payload=Path.Combine(Root,"Driver");ProtectDirectory(payload);
        foreach(string file in Payload) {
            string destination=Path.Combine(payload,file);
            // Replace directory entries instead of following a pre-existing
            // file hard link or symbolic link during an elevated overwrite.
            if(File.Exists(destination)) File.Delete(destination);
            File.Copy(Path.Combine(source,file),destination,false);
        }
        log("Signing the driver catalog with this PC's local certificate...");
        string catalog=Path.Combine(payload,Payload[3]);CatalogSigner.Sign(catalog,cert);
        using(var publicCert=X509CertificateLoader.LoadCertificate(cert.RawData))
            foreach(string name in new[]{"Root","TrustedPublisher"}) {
                using var store=new X509Store(name,StoreLocation.LocalMachine);store.Open(OpenFlags.ReadWrite);store.Add(publicCert);
            }
        CatalogSigner.Verify(catalog);
        log("Staging the signed driver package...");
        var published=new StringBuilder(32768);
        Native.Check(Native.SetupCopyOEMInfW(Path.Combine(payload,Payload[2]),payload,1,0,published,(uint)published.Capacity,out _,0),"Windows rejected the signed driver package");
        string package=Path.GetFileName(published.ToString());ValidatePublishedName(package);
        state.Packages=state.Packages.Append(package).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();Save(state);
        var driverInf=new StringBuilder(32768);
        Native.Check(Native.SetupGetInfDriverStoreLocationW(published.ToString(),0,null,driverInf,(uint)driverInf.Capacity,out _),"Resolving Driver Store location failed");
        state.InfPath=driverInf.ToString();Save(state);
        log("Registering PL80E Open Source Driver...");
        Native.HResult(Native.InstallPrinterDriverFromPackageW(null,state.InfPath,DriverName,"Windows x64",1));
        if(existing.Name is null) {
            var info=new Native.PrinterInfo {Name=DriverName,Port=port,Driver=DriverName,Processor="winprint",DataType="RAW",Priority=1,DefaultPriority=1,Attributes=0x40};
            nint printer=Native.AddPrinterW(null,2,ref info);Native.Check(printer!=0,"Creating printer queue failed");Native.ClosePrinter(printer);
        }
        Save(state); log($"Installed on {port}. Certificate expires {cert.NotAfter:yyyy-MM-dd}.");
    }
    internal void Uninstall()
    {
        ProtectDirectory(Root);
        var printers=Native.Printers();
        var state=ReadState();
        if(state is null) {
            if(!Native.DriverInstalled()) throw new InvalidOperationException("No driver installation was found.");
            state=new InstallationState {OwnsCertificate=false,Queues=printers.Where(p=>p.Driver==DriverName).Select(p=>p.Name!).ToArray(),InfPath=ExistingInf()};
            log("Removing an existing driver registration. Its signing certificates are not owned by this setup and will be retained.");
        }
        if(printers.Any(p=>p.Driver==DriverName && !state.Queues.Contains(p.Name,StringComparer.OrdinalIgnoreCase)))
            throw new InvalidOperationException("Other printer queues still use this driver. Remove or migrate those queues first.");
        foreach(var queue in printers.Where(p=>state.Queues.Contains(p.Name,StringComparer.OrdinalIgnoreCase)))
            if(queue.Driver!=DriverName || queue.Jobs!=0) throw new InvalidOperationException("A managed queue changed driver or still contains print jobs. Nothing has been removed.");
        // Check identity before changing queues; never delete certificates merely by subject.
        using var cert=state.OwnsCertificate?FindCertificate(state.Thumbprint):null;
        if(cert is not null) ValidateCertificate(cert);
        foreach(string storeName in state.OwnsCertificate?new[]{"Root","TrustedPublisher"}:Array.Empty<string>()) {
            using var store=new X509Store(storeName,StoreLocation.LocalMachine);store.Open(OpenFlags.ReadOnly);
            foreach(var found in store.Certificates.Find(X509FindType.FindByThumbprint,state.Thumbprint,false)) ValidateCertificate(found);
        }
        string? previous=PublishedName(state.InfPath);
        if(previous is not null) state.Packages=state.Packages.Append(previous).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        Save(state);
        foreach(var queue in printers.Where(p=>state.Queues.Contains(p.Name,StringComparer.OrdinalIgnoreCase))) {
            log("Removing "+queue.Name+"...");Native.RemoveQueue(queue.Name!);
        }
        if(Native.DriverInstalled()) Native.Check(Native.DeletePrinterDriverExW(null,"Windows x64",DriverName,1,0),"The driver is still in use. Close printing applications and retry");
        foreach(string package in state.Packages) {
            ValidatePublishedName(package);
            string path=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows),"INF",package);
            if(!File.Exists(path)) continue;
            if(!File.ReadAllText(path).Contains('"'+DriverName+'"'+'=')) throw new InvalidDataException("The recorded INF no longer identifies this driver; certificate trust has been retained.");
            log("Removing Driver Store package "+package+"...");
            var start=new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"pnputil.exe")) {UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
            start.ArgumentList.Add("/delete-driver");start.ArgumentList.Add(package);
            using var process=Process.Start(start) ?? throw new IOException("Could not start Windows Driver Store cleanup.");
            var output=process.StandardOutput.ReadToEndAsync();var error=process.StandardError.ReadToEndAsync();process.WaitForExit();
            log(output.GetAwaiter().GetResult());
            if(process.ExitCode!=0) throw new IOException("Driver Store package is still in use. Certificate trust retained. Close printing applications and retry. "+error.GetAwaiter().GetResult());
        }
        foreach(string storeName in state.OwnsCertificate?new[]{"TrustedPublisher","Root","My"}:Array.Empty<string>()) {
            using var store=new X509Store(storeName,StoreLocation.LocalMachine);store.Open(OpenFlags.ReadWrite);
            foreach(var found in store.Certificates.Find(X509FindType.FindByThumbprint,state.Thumbprint,false)) {ValidateCertificate(found);store.Remove(found);}
        }
        if(cert is not null) {using var rsa=cert.GetRSAPrivateKey();if(rsa is RSACng cng) cng.Key.Delete();}
        foreach(string file in Payload) {
            string path=Path.Combine(Root,"Driver",file);
            if(File.Exists(path)) File.Delete(path);
        }
        File.Delete(StatePath);log("Driver, managed printer queues and signing certificate cleanup completed.");
    }
}
