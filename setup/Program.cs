using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text.Json;

namespace PL80E.Setup;

internal static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
#if SETUP_INTEGRATION_TEST
        if(args.Length==3 && args[0]=="--integration-test") {
            try {
                var backend=new Installer(_=>{});
                backend.Install("FILE:",args[1]);
                if(!Native.DriverInstalled() || !Native.Printers().Any(p=>p.Name==Installer.DriverName && p.Port=="FILE:")) throw new Exception("Installation did not create the test queue.");
                string? thumbprint=Installer.ReadState()?.Thumbprint;
                backend.Install("FILE:",args[1]);
                if(Installer.ReadState()?.Thumbprint!=thumbprint) throw new Exception("Update did not retain signing identity.");
                backend.Uninstall();
                if(Native.DriverInstalled() || Native.Printers().Any(p=>p.Name==Installer.DriverName) || File.Exists(Installer.StatePath)) throw new Exception("Uninstall was incomplete.");
                // Reproduce an older registration without installer state.
                // Its certificate must survive adoption/removal. Restore our
                // owned state afterwards solely to clean up this test's key.
                backend.Install("FILE:",args[1]);
                string ownedState=File.ReadAllText(Installer.StatePath);
                string ownedThumbprint=Installer.ReadState()!.Thumbprint;
                File.Delete(Installer.StatePath);backend.Uninstall();
                using(var store=new X509Store(StoreName.My,StoreLocation.LocalMachine)) {
                    store.Open(OpenFlags.ReadOnly);
                    if(store.Certificates.Find(X509FindType.FindByThumbprint,ownedThumbprint,false).Count!=1) throw new Exception("Unmanaged uninstall removed a certificate it did not own.");
                }
                File.WriteAllText(Installer.StatePath,ownedState);backend.Uninstall();
                File.WriteAllText(args[2],"PASS: native certificate creation, catalog signing, staging, registration, FILE queue, update, uninstall and older-registration removal with shared-certificate preservation.");return 0;
            } catch(Exception ex) {File.WriteAllText(args[2],ex.ToString());return 1;}
        }
#endif
        if(args.Length>0 && args[0]=="--self-test") return SelfTest.Run(args);
        ApplicationConfiguration.Initialize();
        using var mutex=new Mutex(true,@"Global\PL80E-Open-Source-Setup",out bool first);
        if(!first) {MessageBox.Show("Setup is already running.",Installer.DriverName);return 1;}
        Application.Run(new SetupForm());return 0;
    }
}

internal sealed class SetupForm:Form
{
    private readonly ComboBox ports=new() {DropDownStyle=ComboBoxStyle.DropDownList,Dock=DockStyle.Fill};
    private readonly Button refresh=new() {Text="Refresh ports",AutoSize=true};
    private readonly Button install=new() {Text="Install / update",AutoSize=true,Padding=new Padding(12,4,12,4)};
    private readonly Button uninstall=new() {Text="Uninstall",AutoSize=true,Padding=new Padding(12,4,12,4)};
    private readonly Label status=new() {AutoSize=true,MaximumSize=new Size(650,0)};
    private readonly TextBox output=new() {Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Vertical,Dock=DockStyle.Fill};
    private bool busy;
    private bool releaseInstalled;
    private sealed record PortChoice(string Port,string Display) {public override string ToString()=>Display;}
    internal SetupForm()
    {
        Text="PL80E Open Source Driver Setup";ClientSize=new Size(700,490);MinimumSize=new Size(640,480);
        StartPosition=FormStartPosition.CenterScreen;Font=new Font("Segoe UI",10);AutoScaleMode=AutoScaleMode.Dpi;
        var layout=new TableLayoutPanel {Dock=DockStyle.Fill,Padding=new Padding(20),ColumnCount=1,RowCount=8};
        for(int i=0;i<7;i++) layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));layout.RowStyles.Add(new RowStyle(SizeType.Percent,100));
        layout.Controls.Add(new Label {Text="PL80E Open Source Driver",Font=new Font(Font.FontFamily,17,FontStyle.Bold),AutoSize=true,Margin=new Padding(0,0,0,12)});
        layout.Controls.Add(new Label {Text="Connect and power on your printer, then select its USB port.",AutoSize=true,Margin=new Padding(0,0,0,8)});
        var selector=new TableLayoutPanel {Dock=DockStyle.Top,ColumnCount=2,AutoSize=true};
        selector.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));selector.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));selector.Controls.Add(ports,0,0);selector.Controls.Add(refresh,1,0);layout.Controls.Add(selector);
        layout.Controls.Add(status);
        layout.Controls.Add(new Label {Text="Installation creates and trusts a local code-signing certificate on this PC.\nUninstall removes the driver and its managed signing certificate.",AutoSize=true,MaximumSize=new Size(650,0),Margin=new Padding(0,12,0,12)});
        var actions=new FlowLayoutPanel {AutoSize=true,Dock=DockStyle.Top};actions.Controls.Add(install);actions.Controls.Add(uninstall);layout.Controls.Add(actions);
        string version=Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion.Split('+')[0] ?? "";
        layout.Controls.Add(new Label {Text="Setup "+version,AutoSize=true,Margin=new Padding(0,4,0,10)});
        layout.Controls.Add(output);Controls.Add(layout);
        refresh.Click+=(_,_)=>RefreshState();ports.SelectedIndexChanged+=(_,_)=>UpdateButtons();
        install.Click+=async(_,_)=> {
            if(ports.SelectedItem is not PortChoice port) return;
            await RunOperation(()=>new Installer(Log).Install(port.Port,AppContext.BaseDirectory));
        };
        uninstall.Click+=async(_,_)=> {
            if(MessageBox.Show(this,"Remove PL80E Open Source Driver, its printer queues and managed signing certificate?","Uninstall",MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes) return;
            await RunOperation(()=>new Installer(Log).Uninstall());
        };
        Shown+=(_,_)=>RefreshState();
        FormClosing+=(_,e)=> {if(busy) e.Cancel=true;};
    }
    private void Log(string message)
    {
        if(IsDisposed) return;
        if(InvokeRequired) {BeginInvoke(()=>Log(message));return;}
        output.AppendText(message.TrimEnd()+Environment.NewLine);
    }
    private void UpdateButtons()
    {
        refresh.Enabled=!busy;ports.Enabled=!busy;install.Enabled=!busy && ports.SelectedIndex>=0;
        uninstall.Enabled=!busy && (File.Exists(Installer.StatePath) || releaseInstalled);
    }
    private void RefreshState()
    {
        try {
            string? previous=(ports.SelectedItem as PortChoice)?.Port;
            var printers=Native.Printers();var release=printers.FirstOrDefault(p=>p.Name==Installer.DriverName);
            releaseInstalled=Native.DriverInstalled();
            string? selected=previous ?? release.Port ?? printers.FirstOrDefault(p=>p.Name=="PL80E").Port;
            ports.Items.Clear();
            foreach(string port in Native.UsbPorts()) {
                bool installedHere=printers.Any(p=>p.Port==port && p.Driver==Installer.DriverName);
                var choice=new PortChoice(port,installedHere?port+" — PL80E Open Source Driver installed":port);
                ports.Items.Add(choice);if(port==selected) ports.SelectedItem=choice;
            }
            if(ports.SelectedIndex<0 && ports.Items.Count>0) ports.SelectedIndex=0;
            status.Text=release.Name is null?"Driver: not installed.":"Driver: installed on "+release.Port+".";
            if(ports.Items.Count==0) status.Text+=" No USB printer ports found. Connect the printer and click Refresh ports.";
            var state=Installer.ReadState();
            if(state is null && releaseInstalled) status.Text+=" Existing signing certificates will be retained.";
        } catch(Exception ex) {Log("Could not read printer status: "+ex.Message);}
        UpdateButtons();
    }
    internal void SavePreview(string path)
    {
        Show();RefreshState();PerformLayout();Application.DoEvents();
        using var bitmap=new Bitmap(Width,Height);
        DrawToBitmap(bitmap,new Rectangle(0,0,Width,Height));
        bitmap.Save(path,System.Drawing.Imaging.ImageFormat.Png);
        Hide();
    }
    private async Task RunOperation(Action action)
    {
        busy=true;UseWaitCursor=true;UpdateButtons();
        try {await Task.Run(action);Log("Completed successfully.");}
        catch(Exception ex) {Log("Operation failed: "+ex);MessageBox.Show(this,ex.Message,"Setup could not complete",MessageBoxButtons.OK,MessageBoxIcon.Error);}
        finally {busy=false;UseWaitCursor=false;RefreshState();}
    }
}

internal static class SelfTest
{
    private static void Check(bool ok,string message) {if(!ok) throw new InvalidOperationException(message);}
    internal static int Run(string[] args)
    {
        string report=args.Length>1?args[1]:Path.Combine(Path.GetTempPath(),"pl80e-setup-selftest.json");
        try {
            Check(Environment.Is64BitProcess,"Requires x64");
            Check(Marshal.SizeOf<Native.PrinterInfo>()==136,"PRINTER_INFO_2 layout");
            Check(Marshal.SizeOf<Native.PortInfo>()==32,"PORT_INFO_2 layout");
            Check(Marshal.SizeOf<Native.DriverInfo>()==80,"DRIVER_INFO_3 layout");
            var legacy=Installer.ParseState(JsonSerializer.Serialize(new {DriverName=Installer.DriverName,Thumbprint=new string('A',40),Queues=new[]{Installer.DriverName},InfPath=(string?)null}));
            Check(legacy.Packages.Length==0 && legacy.Queues.Length==1,"Legacy PowerShell state migration failed");
            bool rejected=false;
            try {Installer.ParseState(JsonSerializer.Serialize(new {DriverName="PL80E Open Source (DEV)",Thumbprint=new string('A',40),Queues=new[]{"PL80E Open Source (DEV)"}}));}
            catch(InvalidDataException) {rejected=true;}
            Check(rejected,"DEV state must never be managed by release setup");
            var ports=Native.UsbPorts();var printers=Native.Printers();bool installed=Native.DriverInstalled();
            ApplicationConfiguration.Initialize();using var form=new SetupForm();form.CreateControl();
            form.SavePreview(Path.ChangeExtension(report,".png"));
            var state=Installer.ReadState();
            // Sign a disposable copy with a disposable CURRENT USER key. This
            // does not trust a certificate, install a printer, or modify HKLM.
            if(args.Length>2) {
                string catalog=Path.GetFullPath(args[2]);
                using var key=CngKey.Create(CngAlgorithm.Rsa,"PL80E-Setup-Test-"+Guid.NewGuid().ToString("N"));
                try {
                    using var rsa=new RSACng(key);
                    var request=new CertificateRequest("CN=PL80E Setup Signing Test",rsa,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);
                    request.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature,true));
                    request.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension(new OidCollection {new("1.3.6.1.5.5.7.3.3")},true));
                    using var cert=request.CreateSelfSigned(DateTimeOffset.Now.AddMinutes(-1),DateTimeOffset.Now.AddDays(1));
                    CatalogSigner.Sign(catalog,cert);
#pragma warning disable SYSLIB0057 // The X509CertificateLoader API does not extract Authenticode signers from catalog files.
                    using var embedded=X509Certificate.CreateFromSignedFile(catalog);
#pragma warning restore SYSLIB0057
                    Check(embedded.GetCertHashString()==cert.Thumbprint,"Catalog signing certificate mismatch");
                } finally {key.Delete();}
            }
            File.WriteAllText(report,JsonSerializer.Serialize(new {Status="PASS",Ports=ports,Printers=printers.Select(p=>new {p.Name,p.Port,p.Driver}),ReleaseDriverInstalled=installed,LegacyStateRecognized=state is not null,NativeCatalogSigning=args.Length>2},new JsonSerializerOptions {WriteIndented=true}));
            return 0;
        } catch(Exception ex) {File.WriteAllText(report,JsonSerializer.Serialize(new {Status="FAILED",Error=ex.ToString()}));return 1;}
    }
}
