using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace PL80E.Setup;

internal static class Native
{
    [StructLayout(LayoutKind.Sequential)] internal struct PortInfo { public nint Name, Monitor, Description; public uint Type, Reserved; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] internal struct PrinterInfo
    {
        public string? Server, Name, Share, Port, Driver, Comment, Location;
        public nint DevMode;
        public string? Separator, Processor, DataType, Parameters;
        public nint Security;
        public uint Attributes, Priority, DefaultPriority, StartTime, UntilTime, Status, Jobs, AveragePpm;
    }
    [StructLayout(LayoutKind.Sequential)] internal struct DriverInfo
    { public uint Version; public nint Name, Environment, Path, Data, Config, Help, Dependent, Monitor, DataType; }
    internal static string Text(nint p) => Marshal.PtrToStringUni(p) ?? "";
    internal static void Check(bool ok, string operation) { if(!ok) throw new Win32Exception(Marshal.GetLastWin32Error(), operation); }
    internal static void HResult(int result) { if(result<0) Marshal.ThrowExceptionForHR(result); }
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)] internal static extern bool EnumPortsW(string? server,uint level,nint data,uint size,out uint needed,out uint count);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)] internal static extern bool EnumPrintersW(uint flags,string? name,uint level,nint data,uint size,out uint needed,out uint count);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)] internal static extern bool EnumPrinterDriversW(string? server,string environment,uint level,nint data,uint size,out uint needed,out uint count);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)] internal static extern nint AddPrinterW(string? server,uint level,ref PrinterInfo info);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)] internal static extern bool OpenPrinterW(string name,out nint printer,nint defaults);
    [DllImport("winspool.drv", SetLastError=true)] internal static extern bool GetPrinterW(nint printer,uint level,nint data,uint size,out uint needed);
    [DllImport("winspool.drv", SetLastError=true)] internal static extern bool DeletePrinter(nint printer);
    [DllImport("winspool.drv")] internal static extern bool ClosePrinter(nint printer);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)] internal static extern bool DeletePrinterDriverExW(string? server,string environment,string name,uint flags,uint version);
    [DllImport("winspool.drv", CharSet=CharSet.Unicode)] internal static extern int InstallPrinterDriverFromPackageW(string? server,string inf,string name,string environment,uint flags);
    [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] internal static extern bool SetupCopyOEMInfW(string source,string? media,uint type,uint flags,StringBuilder destination,uint size,out uint required,nint component);
    [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] internal static extern bool SetupGetInfDriverStoreLocationW(string inf,nint platform,string? locale,StringBuilder path,uint size,out uint required);
    [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] internal static extern bool SetupGetInfPublishedNameW(string inf,StringBuilder path,uint size,out uint required);

    internal static List<PrinterInfo> Printers()
    {
        EnumPrintersW(2|4,null,2,0,0,out uint needed,out _);
        if(needed==0) return [];
        nint data=Marshal.AllocHGlobal(checked((int)needed));
        try {
            Check(EnumPrintersW(2|4,null,2,data,needed,out _,out uint count),"Enumerating printers failed");
            return Enumerable.Range(0,checked((int)count)).Select(i=>Marshal.PtrToStructure<PrinterInfo>(data+i*Marshal.SizeOf<PrinterInfo>())).ToList();
        } finally {Marshal.FreeHGlobal(data);}
    }
    internal static List<string> UsbPorts()
    {
        EnumPortsW(null,2,0,0,out uint needed,out _);
        if(needed==0) return [];
        nint data=Marshal.AllocHGlobal(checked((int)needed));
        try {
            Check(EnumPortsW(null,2,data,needed,out _,out uint count),"Enumerating printer ports failed");
            return Enumerable.Range(0,checked((int)count))
                .Select(i=>Text(Marshal.PtrToStructure<PortInfo>(data+i*Marshal.SizeOf<PortInfo>()).Name))
                .Where(p=>System.Text.RegularExpressions.Regex.IsMatch(p,@"^USB\d+$",System.Text.RegularExpressions.RegexOptions.IgnoreCase))
                .Distinct(StringComparer.OrdinalIgnoreCase).Order().ToList();
        } finally {Marshal.FreeHGlobal(data);}
    }
    internal static bool DriverInstalled()
    {
        EnumPrinterDriversW(null,"Windows x64",3,0,0,out uint needed,out _);
        if(needed==0) return false;
        nint data=Marshal.AllocHGlobal(checked((int)needed));
        try {
            Check(EnumPrinterDriversW(null,"Windows x64",3,data,needed,out _,out uint count),"Reading printer drivers failed");
            return Enumerable.Range(0,checked((int)count)).Any(i=>Text(Marshal.PtrToStructure<DriverInfo>(data+i*Marshal.SizeOf<DriverInfo>()).Name)==Installer.DriverName);
        } finally {Marshal.FreeHGlobal(data);}
    }
    internal static void RemoveQueue(string name)
    {
        nint defaults=Marshal.AllocHGlobal(24);
        try {
            Marshal.Copy(new byte[24],0,defaults,24); Marshal.WriteInt32(defaults,16,0x1000c); // DELETE | ADMINISTER | USE
            Check(OpenPrinterW(name,out nint printer,defaults),"Opening queue for removal failed");
            try {
                GetPrinterW(printer,2,0,0,out uint needed);
                nint data=Marshal.AllocHGlobal(checked((int)needed));
                try {
                    Check(GetPrinterW(printer,2,data,needed,out _),"Reading queue failed");
                    var info=Marshal.PtrToStructure<PrinterInfo>(data);
                    if(info.Driver!=Installer.DriverName || info.Jobs!=0) throw new InvalidOperationException("The queue changed or still has print jobs. Close printing applications and retry.");
                    Check(DeletePrinter(printer),"Removing printer queue failed");
                } finally {Marshal.FreeHGlobal(data);}
            } finally {ClosePrinter(printer);}
        } finally {Marshal.FreeHGlobal(defaults);}
    }
}
