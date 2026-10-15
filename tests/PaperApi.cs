using System;
using System.Runtime.InteropServices;
using System.ComponentModel;
public static class PaperApi {
    [DllImport("winspool.drv",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool OpenPrinterW(string name,out IntPtr handle,IntPtr defaults);
    [DllImport("winspool.drv")] static extern bool ClosePrinter(IntPtr handle);
    [DllImport("winspool.drv",SetLastError=true)] static extern bool GetPrinterW(IntPtr handle,uint level,IntPtr buffer,uint size,out uint needed);
    [DllImport("winspool.drv",SetLastError=true)] static extern bool SetPrinterW(IntPtr handle,uint level,IntPtr data,uint command);
    [DllImport("winspool.drv",CharSet=CharSet.Unicode)] static extern int DocumentPropertiesW(IntPtr window,IntPtr handle,string name,IntPtr output,IntPtr input,uint mode);
    [DllImport("winspool.drv",CharSet=CharSet.Unicode)] static extern int DeviceCapabilitiesW(string name,string port,ushort cap,IntPtr output,IntPtr dm);
    static void Check(bool value) { if(!value) throw new Win32Exception(Marshal.GetLastWin32Error()); }
    public static void Test(string name,string port) {
        IntPtr handle; Check(OpenPrinterW(name,out handle,IntPtr.Zero));
        IntPtr original=IntPtr.Zero,working=IntPtr.Zero,info=Marshal.AllocHGlobal(IntPtr.Size),sizes=Marshal.AllocHGlobal(8),names=Marshal.AllocHGlobal(128);
        bool changed=false;
        try {
            uint needed; GetPrinterW(handle,9,IntPtr.Zero,0,out needed);
            original=Marshal.AllocHGlobal((int)needed); Check(GetPrinterW(handle,9,original,needed,out needed));
            int length=DocumentPropertiesW(IntPtr.Zero,handle,name,IntPtr.Zero,IntPtr.Zero,0);
            if(length<220) throw new Exception("Missing DEVMODE");
            working=Marshal.AllocHGlobal(length);
            if(DocumentPropertiesW(IntPtr.Zero,handle,name,working,IntPtr.Zero,2)!=1) throw new Exception("Defaults failed");
            foreach(int width in new int[]{455,600,1000}) {
                int height=width==1000?1500:250;
                Marshal.WriteInt16(working,78,(short)256);
                Marshal.WriteInt16(working,80,(short)height); Marshal.WriteInt16(working,82,(short)width);
                Marshal.WriteIntPtr(info,working); Check(SetPrinterW(handle,9,info,0)); changed=true;
                if(DeviceCapabilitiesW(name,port,3,sizes,IntPtr.Zero)!=1 || Marshal.ReadInt32(sizes)!=width || Marshal.ReadInt32(sizes,4)!=height)
                    throw new Exception("Wrong enumerated paper dimensions");
                if(DeviceCapabilitiesW(name,port,16,names,IntPtr.Zero)!=1) throw new Exception("Paper name query failed");
                string expected=width==455?"Label 45.5 x 25.0 mm":width==600?"Label 60 x 25 mm":"Label 100 x 150 mm";
                if(Marshal.PtrToStringUni(names)!=expected) throw new Exception("Wrong paper name: "+Marshal.PtrToStringUni(names));
                Console.WriteLine("PASS: Windows DeviceCapabilities / saved preferences: "+expected);
            }
        } finally {
            if(changed) Check(SetPrinterW(handle,9,original,0));
            if(original!=IntPtr.Zero) Marshal.FreeHGlobal(original);
            if(working!=IntPtr.Zero) Marshal.FreeHGlobal(working);
            Marshal.FreeHGlobal(info); Marshal.FreeHGlobal(sizes); Marshal.FreeHGlobal(names); ClosePrinter(handle);
        }
    }
}
