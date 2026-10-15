using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;

public static class CleanPrintApi {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    struct DOCINFO {
        public int cbSize;
        public string docName, outputFile, dataType;
        public uint flags;
    }
    [DllImport("winspool.drv",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern bool OpenPrinterW(string name,out IntPtr handle,IntPtr defaults);
    [DllImport("winspool.drv",SetLastError=true)]
    static extern bool ClosePrinter(IntPtr handle);
    [DllImport("winspool.drv",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern int DocumentPropertiesW(IntPtr hwnd,IntPtr printer,string name,IntPtr output,IntPtr input,uint mode);
    [DllImport("winspool.drv",CharSet=CharSet.Unicode)]
    static extern uint SetPrinterDataW(IntPtr printer,string name,uint type,byte[] data,uint size);
    [DllImport("gdi32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern IntPtr CreateDCW(string driver,string name,string output,IntPtr devmode);
    [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr dc);
    [DllImport("gdi32.dll")] static extern int GetDeviceCaps(IntPtr dc,int cap);
    [DllImport("gdi32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern int StartDocW(IntPtr dc,ref DOCINFO info);
    [DllImport("gdi32.dll",SetLastError=true)] static extern int StartPage(IntPtr dc);
    [DllImport("gdi32.dll",SetLastError=true)] static extern int EndPage(IntPtr dc);
    [DllImport("gdi32.dll",SetLastError=true)] static extern int EndDoc(IntPtr dc);
    [DllImport("gdi32.dll")] static extern int AbortDoc(IntPtr dc);
    [DllImport("gdi32.dll",SetLastError=true)] static extern bool PatBlt(IntPtr dc,int x,int y,int width,int height,uint rop);
    [DllImport("gdi32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern IntPtr CreateFontW(int height,int width,int escapement,int orientation,int weight,
        uint italic,uint underline,uint strikeout,uint charset,uint outPrecision,
        uint clipPrecision,uint quality,uint pitch,string face);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc,IntPtr value);
    [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr value);
    [DllImport("gdi32.dll")] static extern uint SetTextColor(IntPtr dc,uint color);
    [DllImport("gdi32.dll")] static extern int SetBkMode(IntPtr dc,int mode);
    [DllImport("gdi32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern bool TextOutW(IntPtr dc,int x,int y,string text,int count);

    static Exception Error(string operation) {
        int code=Marshal.GetLastWin32Error();
        return new Win32Exception(code,operation+" (Win32 "+code+"): "+new Win32Exception(code).Message);
    }
    public static void Configure(string printerName,uint gap,uint density,uint speed) {
        if(gap>100 || density<1 || density>15 || speed<2 || speed>6)
            throw new ArgumentOutOfRangeException("settings");
        IntPtr printer;
        // PRINTER_ACCESS_ADMINISTER is needed for SetPrinterData.
        IntPtr defaults=Marshal.AllocHGlobal(IntPtr.Size*3);
        try {
            for(int i=0;i<IntPtr.Size*3;i++) Marshal.WriteByte(defaults,i,0);
            Marshal.WriteInt32(defaults,IntPtr.Size*2,4);
            if(!OpenPrinterW(printerName,out printer,defaults)) throw Error("OpenPrinterW (administer)");
        } finally { Marshal.FreeHGlobal(defaults); }
        try {
            string[] names={"CleanGapTenthsMm","CleanDensity","CleanSpeed"};
            uint[] values={gap,density,speed};
            for(int i=0;i<names.Length;i++) {
                uint result=SetPrinterDataW(printer,names[i],4,BitConverter.GetBytes(values[i]),4);
                if(result!=0) throw new Win32Exception((int)result,"SetPrinterDataW "+names[i]);
            }
        } finally { ClosePrinter(printer); }
    }

    public static int[] RenderFixture(string printerName,string output,bool landscape,int pages,int speed=5,int density=10,int media=0,int gapTenthsMm=30) {
        if(media<0 || media>2 || gapTenthsMm<0 || gapTenthsMm>100) throw new ArgumentOutOfRangeException("media/gap");
        if(density<1 || density>15) throw new ArgumentOutOfRangeException("density");
        if(speed<2 || speed>6) throw new ArgumentOutOfRangeException("speed");
        if(pages<1 || pages>2) throw new ArgumentOutOfRangeException("pages");
        if(!Path.IsPathRooted(output) || File.Exists(output))
            throw new ArgumentException("Capture needs a new absolute output path.");
        IntPtr printer,dm=IntPtr.Zero,dc=IntPtr.Zero;
        bool started=false;
        if(!OpenPrinterW(printerName,out printer,IntPtr.Zero)) throw Error("OpenPrinterW");
        try {
            int size=DocumentPropertiesW(IntPtr.Zero,printer,printerName,IntPtr.Zero,IntPtr.Zero,0);
            if(size<220 || size>65536) throw new InvalidOperationException("DEVMODE/GPD initialization failed: "+size);
            dm=Marshal.AllocHGlobal(size);
            if(DocumentPropertiesW(IntPtr.Zero,printer,printerName,dm,IntPtr.Zero,2)!=1)
                throw Error("DocumentPropertiesW default");
            int publicSize=(ushort)Marshal.ReadInt16(dm,68);
            int extraSize=(ushort)Marshal.ReadInt16(dm,70);
            if(extraSize!=20 || publicSize+extraSize>size || Marshal.ReadInt32(dm,publicSize)!=0x43535044)
                throw new InvalidOperationException("Speed settings missing from DEVMODE.");
            Marshal.WriteInt32(dm,publicSize+4,speed);
            Marshal.WriteInt32(dm,publicSize+8,density);
            Marshal.WriteInt32(dm,publicSize+12,media);
            Marshal.WriteInt32(dm,publicSize+16,gapTenthsMm);
            // Public DEVMODEW offsets are fixed by the Windows ABI.
            uint fields=(uint)Marshal.ReadInt32(dm,72);
            fields |= 1|2|4|8|0x100|0x400|0x2000;
            fields &= ~0x10000u; // explicit dimensions, no conflicting form name
            Marshal.WriteInt32(dm,72,(int)fields);
            Marshal.WriteInt16(dm,76,(short)(landscape?2:1));
            Marshal.WriteInt16(dm,78,256); // DMPAPER_USER
            Marshal.WriteInt16(dm,80,1500);
            Marshal.WriteInt16(dm,82,1000);
            Marshal.WriteInt16(dm,86,1);
            Marshal.WriteInt16(dm,90,203);
            Marshal.WriteInt16(dm,96,203);
            if(DocumentPropertiesW(IntPtr.Zero,printer,printerName,dm,dm,10)!=1)
                throw Error("DocumentPropertiesW merge");
            dc=CreateDCW("WINSPOOL",printerName,null,dm);
            if(dc==IntPtr.Zero) throw Error("CreateDCW");
            int width=GetDeviceCaps(dc,8),height=GetDeviceCaps(dc,10);
            int dpiX=GetDeviceCaps(dc,88),dpiY=GetDeviceCaps(dc,90);
            if(width!=(landscape?1199:799) || height!=(landscape?799:1199) || dpiX!=203 || dpiY!=203)
                throw new InvalidOperationException("Unexpected capabilities: "+width+" x "+height+" @ "+dpiX+","+dpiY);
            DOCINFO doc=new DOCINFO(); doc.cbSize=Marshal.SizeOf(typeof(DOCINFO));
            doc.docName="PL80E Clean offline raster validation"; doc.outputFile=output;
            if(StartDocW(dc,ref doc)<=0) throw Error("StartDocW");
            started=true;
            for(int page=0;page<pages;page++) {
                if(StartPage(dc)<=0) throw Error("StartPage");
                if(!PatBlt(dc,0,0,width,height,0x00FF0062)) throw Error("PatBlt WHITE");
                // Encode the exact source row number in the first 16 columns.
                // On page 2 invert the marker, so page-buffer clearing is tested.
                for(int y=0;y<height;y++) for(int x=0;x<16;x++) {
                    bool black=(((y>>(15-x))&1)!=0) ^ (page==1);
                    if(black && !PatBlt(dc,x,y,1,1,0x00000042)) throw Error("PatBlt marker");
                }
                SetBkMode(dc,1); SetTextColor(dc,0);
                string title="PL80E Clean - 100 x 150 mm";
                DrawText(dc,40,35,12,title);
                int yy=100;
                foreach(int pt in new int[]{6,8,10,12,16}) {
                    DrawText(dc,40,yy,pt,"AaBb 0123456789 - "+pt+" pt"); yy+=70;
                }
                DrawText(dc,40,yy+20,10,"Native 203 dpi / alle Rasterzeilen");
                if(!PatBlt(dc,32,height-2,width-32,1,0x00000042)) throw Error("PatBlt bottom marker");
                if(EndPage(dc)<=0) throw Error("EndPage");
            }
            if(EndDoc(dc)<=0) throw Error("EndDoc");
            started=false;
            return new int[]{width,height,dpiX,dpiY};
        } finally {
            if(started && dc!=IntPtr.Zero) AbortDoc(dc);
            if(dc!=IntPtr.Zero) DeleteDC(dc);
            if(dm!=IntPtr.Zero) Marshal.FreeHGlobal(dm);
            ClosePrinter(printer);
        }
    }
    static void DrawText(IntPtr dc,int x,int y,int pt,string text) {
        IntPtr font=CreateFontW(-(pt*203+36)/72,0,0,0,400,0,0,0,1,0,0,3,0,"Arial");
        if(font==IntPtr.Zero) throw Error("CreateFontW");
        IntPtr old=SelectObject(dc,font);
        try { if(!TextOutW(dc,x,y,text,text.Length)) throw Error("TextOutW"); }
        finally { SelectObject(dc,old); DeleteObject(font); }
    }
}
