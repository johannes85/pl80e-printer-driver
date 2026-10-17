using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;

namespace PL80E.Setup;

internal static class CatalogSigner
{
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct FileInfo { public uint Size; [MarshalAs(UnmanagedType.LPWStr)] public string Path; public nint Handle; }
    [StructLayout(LayoutKind.Sequential)] struct SubjectInfo { public uint Size; public nint Index; public uint Choice; public nint File; }
    [StructLayout(LayoutKind.Sequential)] struct StoreInfo { public uint Size; public nint Certificate; public uint Policy; public nint Store; }
    [StructLayout(LayoutKind.Sequential)] struct CertInfo { public uint Size, Choice; public nint Store, Window; }
    [StructLayout(LayoutKind.Sequential)] struct SignatureInfo { public uint Size, Hash, Choice; public nint Attributes, Authenticated, Unauthenticated; }
    [DllImport("mssign32.dll",CharSet=CharSet.Unicode)] static extern int SignerSignEx(uint flags,ref SubjectInfo subject,ref CertInfo cert,ref SignatureInfo signature,nint provider,string? timestamp,nint request,nint sip,out nint context);
    [DllImport("mssign32.dll")] static extern int SignerFreeSignerContext(nint context);
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct TrustFile { public uint Size; [MarshalAs(UnmanagedType.LPWStr)] public string Path; public nint File, KnownSubject; }
    [StructLayout(LayoutKind.Sequential)] struct TrustData {
        public uint Size; public nint Policy, Sip; public uint Ui, Revocation, Choice; public nint File;
        public uint StateAction; public nint State, Url; public uint Flags, UiContext; public nint Signature;
    }
    [DllImport("wintrust.dll",ExactSpelling=true)] static extern int WinVerifyTrust(nint window,ref Guid action,ref TrustData data);
    static nint Allocate<T>(T value) where T:struct {nint p=Marshal.AllocHGlobal(Marshal.SizeOf<T>());Marshal.StructureToPtr(value,p,false);return p;}
    internal static void Sign(string path,X509Certificate2 certificate)
    {
        nint file=Allocate(new FileInfo {Size=(uint)Marshal.SizeOf<FileInfo>(),Path=path});
        nint store=Allocate(new StoreInfo {Size=(uint)Marshal.SizeOf<StoreInfo>(),Certificate=certificate.Handle,Policy=2});
        nint index=Marshal.AllocHGlobal(4); Marshal.WriteInt32(index,0);
        try {
            var subject=new SubjectInfo {Size=(uint)Marshal.SizeOf<SubjectInfo>(),Index=index,Choice=1,File=file};
            var cert=new CertInfo {Size=(uint)Marshal.SizeOf<CertInfo>(),Choice=2,Store=store};
            var signature=new SignatureInfo {Size=(uint)Marshal.SizeOf<SignatureInfo>(),Hash=0x800c}; // CALG_SHA_256
            int hr=SignerSignEx(0,ref subject,ref cert,ref signature,0,null,0,0,out nint context);
            if(context!=0) SignerFreeSignerContext(context);
            Native.HResult(hr);
        } finally {Marshal.DestroyStructure<FileInfo>(file);Marshal.FreeHGlobal(file);Marshal.FreeHGlobal(store);Marshal.FreeHGlobal(index);}
    }
    internal static void Verify(string path)
    {
        nint file=Allocate(new TrustFile {Size=(uint)Marshal.SizeOf<TrustFile>(),Path=path});
        var action=new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE");
        var data=new TrustData {Size=(uint)Marshal.SizeOf<TrustData>(),Ui=2,Choice=1,File=file,StateAction=1,Flags=0x1000};
        try {Native.HResult(WinVerifyTrust(new nint(-1),ref action,ref data));}
        finally {data.StateAction=2;WinVerifyTrust(new nint(-1),ref action,ref data);Marshal.DestroyStructure<TrustFile>(file);Marshal.FreeHGlobal(file);}
    }
}
