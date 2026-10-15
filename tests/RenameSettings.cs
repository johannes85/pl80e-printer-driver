using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class RenameSettings {
    [DllImport("winspool.drv",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool OpenPrinterW(string name,out IntPtr handle,IntPtr defaults);
    [DllImport("winspool.drv")] static extern bool ClosePrinter(IntPtr handle);
    [DllImport("winspool.drv",SetLastError=true)] static extern bool GetPrinterW(IntPtr handle,uint level,IntPtr buffer,uint size,out uint needed);
    [DllImport("winspool.drv",SetLastError=true)] static extern bool SetPrinterW(IntPtr handle,uint level,IntPtr buffer,uint command);
    static IntPtr Open(string name) {
        IntPtr d=Marshal.AllocHGlobal(IntPtr.Size*3),h;
        try { for(int i=0;i<IntPtr.Size*3;i++) Marshal.WriteByte(d,i,0); Marshal.WriteInt32(d,IntPtr.Size*2,12);
            if(!OpenPrinterW(name,out h,d)) throw new Win32Exception(); return h;
        } finally {Marshal.FreeHGlobal(d);}
    }
    public static byte[] Read(string name,uint level) {
        IntPtr h=Open(name),buffer=IntPtr.Zero;
        try {uint needed; GetPrinterW(h,level,IntPtr.Zero,0,out needed); buffer=Marshal.AllocHGlobal((int)needed);
            if(!GetPrinterW(h,level,buffer,needed,out needed)) throw new Win32Exception();
            IntPtr dm=Marshal.ReadIntPtr(buffer); if(dm==IntPtr.Zero) return null;
            int size=(ushort)Marshal.ReadInt16(dm,68)+(ushort)Marshal.ReadInt16(dm,70);
            byte[] bytes=new byte[size]; Marshal.Copy(dm,bytes,0,size); return bytes;
        } finally {if(buffer!=IntPtr.Zero) Marshal.FreeHGlobal(buffer); ClosePrinter(h);}
    }
    public static void Write(string name,uint level,byte[] bytes) {
        IntPtr h=Open(name),dm=IntPtr.Zero,info=Marshal.AllocHGlobal(IntPtr.Size);
        try { if(bytes!=null) {
                dm=Marshal.AllocHGlobal(bytes.Length); Marshal.Copy(bytes,0,dm,bytes.Length);
                char[] device=new char[32]; name.CopyTo(0,device,0,Math.Min(31,name.Length)); Marshal.Copy(device,0,dm,32);
            }
            Marshal.WriteIntPtr(info,dm); if(!SetPrinterW(h,level,info,0)) throw new Win32Exception();
        } finally {if(dm!=IntPtr.Zero) Marshal.FreeHGlobal(dm);Marshal.FreeHGlobal(info);ClosePrinter(h);}
    }
}
