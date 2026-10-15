/* Native configuration interface, independent of Unidrv private DEVMODEs. */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <prsht.h>
#include <winspool.h>
#include <compstui.h>
#include <winddiui.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "branding.h"

/* DOCUMENTPROPERTYHEADER is absent from the bundled MinGW winddiui header.
 * This layout follows Microsoft's winddiui DOCUMENTPROPERTYHEADER contract. */
typedef struct {
    WORD cbSize, Reserved;
    HANDLE hPrinter;
    LPWSTR pszPrinterName;
    DEVMODEW *pdmIn, *pdmOut;
    DWORD cbOut, fMode;
} CleanDocumentHeader;

#define CLEAN_FIELDS (DM_ORIENTATION|DM_PAPERSIZE|DM_PAPERLENGTH|DM_PAPERWIDTH|\
                      DM_SCALE|DM_COPIES|DM_DEFAULTSOURCE|DM_PRINTQUALITY|\
                      DM_COLOR|DM_DUPLEX|DM_YRESOLUTION|DM_TTOPTION|DM_COLLATE)

static void defaults(DEVMODEW *dm) {
    memset(dm,0,sizeof(*dm));
    lstrcpyW(dm->dmDeviceName,CLEAN_NAME_W);
    dm->dmSpecVersion=DM_SPECVERSION; dm->dmDriverVersion=0x0102;
    dm->dmSize=sizeof(*dm); dm->dmDriverExtra=sizeof(CleanPrivate); dm->dmFields=CLEAN_FIELDS;
    dm->dmOrientation=DMORIENT_PORTRAIT; dm->dmPaperSize=DMPAPER_USER;
    dm->dmPaperWidth=1000; dm->dmPaperLength=1500; dm->dmScale=100;
    dm->dmCopies=1; dm->dmDefaultSource=DMBIN_AUTO;
    dm->dmPrintQuality=203; dm->dmYResolution=203;
    dm->dmColor=DMCOLOR_MONOCHROME; dm->dmDuplex=DMDUP_SIMPLEX;
    dm->dmTTOption=DMTT_BITMAP; dm->dmCollate=DMCOLLATE_FALSE;
    CleanPrivate *settings=(CleanPrivate *)((BYTE *)dm+sizeof(*dm));
    *settings=(CleanPrivate){CLEAN_SETTINGS_MAGIC,5,10,0,30};
}

static BOOL merge(DEVMODEW *out,const DEVMODEW *in) {
    if(!in) return TRUE;
    if(in->dmSize < offsetof(DEVMODEW,dmYResolution)+sizeof(short)) {
        SetLastError(ERROR_INVALID_DATA); return FALSE;
    }
    if(in->dmFields & DM_ORIENTATION) {
        if(in->dmOrientation!=DMORIENT_PORTRAIT && in->dmOrientation!=DMORIENT_LANDSCAPE) goto invalid;
        out->dmOrientation=in->dmOrientation;
    }
    if(in->dmFields & DM_PAPERWIDTH) {
        if(in->dmPaperWidth<200 || in->dmPaperWidth>1000) goto invalid;
        out->dmPaperWidth=in->dmPaperWidth;
    }
    if(in->dmFields & DM_PAPERLENGTH) {
        if(in->dmPaperLength<100 || in->dmPaperLength>3000) goto invalid;
        out->dmPaperLength=in->dmPaperLength;
    }
    if(in->dmFields & DM_COPIES) {
        if(in->dmCopies<1 || in->dmCopies>99) goto invalid;
        out->dmCopies=in->dmCopies;
    }
    if((in->dmFields & DM_PRINTQUALITY) && in->dmPrintQuality>0 && in->dmPrintQuality!=203) goto invalid;
    if((in->dmFields & DM_YRESOLUTION) && in->dmYResolution>0 && in->dmYResolution!=203) goto invalid;
    const CleanPrivate *settings=clean_private(in);
    if(settings) {
        CleanPrivate *destination=(CleanPrivate *)((BYTE *)out+sizeof(*out));
        destination->speed=settings->speed;
        if(in->dmDriverExtra>=12) destination->density=settings->density;
        if(in->dmDriverExtra==sizeof(CleanPrivate)) { destination->media=settings->media; destination->gap=settings->gap; }
    }
    return TRUE;
invalid:
    SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
}

static void stored_defaults(HANDLE printer,DEVMODEW *dm) {
    defaults(dm);
    if(!printer) return;
    DWORD speed=5,type=0,size=sizeof(speed);
    if(GetPrinterDataW(printer,L"CleanSpeed",&type,(BYTE *)&speed,sizeof(speed),&size)==ERROR_SUCCESS && type==REG_DWORD && speed>=2 && speed<=6)
        ((CleanPrivate *)((BYTE *)dm+sizeof(*dm)))->speed=speed;
    DWORD density=10; type=0; size=sizeof(density);
    if(GetPrinterDataW(printer,L"CleanDensity",&type,(BYTE *)&density,sizeof(density),&size)==ERROR_SUCCESS && type==REG_DWORD && density>=1 && density<=15)
        ((CleanPrivate *)((BYTE *)dm+sizeof(*dm)))->density=density;
    DWORD gap=30; type=0; size=sizeof(gap);
    if(GetPrinterDataW(printer,L"CleanGapTenthsMm",&type,(BYTE *)&gap,sizeof(gap),&size)==ERROR_SUCCESS && type==REG_DWORD && gap<=100)
        ((CleanPrivate *)((BYTE *)dm+sizeof(*dm)))->gap=gap;
    DWORD needed=0;
    GetPrinterW(printer,9,NULL,0,&needed);
    if(needed<sizeof(PRINTER_INFO_9W) || needed>65536) return;
    BYTE *data=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,needed);
    if(!data) return;
    if(GetPrinterW(printer,9,data,needed,&needed)) {
        DEVMODEW *stored=((PRINTER_INFO_9W *)data)->pDevMode;
        if(!merge(dm,stored)) defaults(dm);
    }
    HeapFree(GetProcessHeap(),0,data);
}

typedef struct {
    CleanDocumentHeader *header;
    CleanDevMode data;
    HANDLE group, page;
    PFNCOMPROPSHEET callback;
} DocumentUi;

LONG WINAPI DrvDocumentPropertySheets(PPROPSHEETUI_INFO info,LPARAM parameter);

static INT_PTR CALLBACK device_page_proc(HWND dialog,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_INITDIALOG) return TRUE;
    if(message==WM_COMMAND && LOWORD(wparam)==122 && HIWORD(wparam)==BN_CLICKED) {
        WCHAR url[2048];
        GetDlgItemTextW(dialog,122,url,2048);
        if(wcsncmp(url,L"https://",8)!=0 && wcsncmp(url,L"http://",7)!=0) return TRUE;
        HINSTANCE result=ShellExecuteW(dialog,L"open",url,NULL,NULL,SW_SHOWNORMAL);
        if((INT_PTR)result<=32)
            MessageBoxW(dialog,L"The project page could not be opened. Please open the displayed address in your browser.",CLEAN_NAME_W,MB_OK|MB_ICONWARNING);
        return TRUE;
    }
    if(message==WM_NOTIFY && ((NMHDR *)lparam)->code==PSN_APPLY) {
        SetWindowLongPtrW(dialog,DWLP_MSGRESULT,PSNRET_NOERROR); return TRUE;
    }
    return FALSE;
}

/* Windows printer properties require this entry point independently of
 * DrvDocumentPropertySheets (printing preferences). No per-document options
 * are written from this informational device page. */
LONG WINAPI DrvDevicePropertySheets(PPROPSHEETUI_INFO info,LPARAM parameter) {
    if(!info) return 0;
    switch(info->Reason) {
    case PROPSHEETUI_REASON_INIT: {
        HMODULE module=NULL;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)(uintptr_t)DrvDevicePropertySheets,&module)) return -1;
        PROPSHEETPAGEW page={0}; page.dwSize=sizeof(page); page.dwFlags=PSP_USETITLE;
        page.hInstance=module; page.pszTemplate=MAKEINTRESOURCEW(201); page.pszTitle=L"Device";
        page.pfnDlgProc=device_page_proc;
        return info->pfnComPropSheet(info->hComPropSheet,CPSFUNC_ADD_PROPSHEETPAGEW,(LPARAM)&page,0)?1:-1;
    }
    case PROPSHEETUI_REASON_GET_INFO_HEADER: {
        PPROPSHEETUI_INFO_HEADER header=(PPROPSHEETUI_INFO_HEADER)parameter;
        if(header) header->pTitle=CLEAN_NAME_W;
        return TRUE;
    }
    case PROPSHEETUI_REASON_SET_RESULT:
        if(parameter) info->Result=(ULONG_PTR)((PSETRESULT_INFO)parameter)->Result;
        return TRUE;
    default: return TRUE;
    }
}

static void set_mm(HWND dialog,int id,short value) {
    WCHAR text[32];
    _snwprintf(text,32,L"%u.%u",(unsigned)value/10,(unsigned)value%10);
    SetDlgItemTextW(dialog,id,text);
}
static BOOL read_mm(HWND dialog,int id,int minimum,int maximum,short *out) {
    WCHAR text[32]; GetDlgItemTextW(dialog,id,text,32);
    for(unsigned i=0;text[i];i++) if(text[i]==L',') text[i]=L'.';
    WCHAR *end=NULL; double value=wcstod(text,&end);
    if(!end || end==text || *end || !(value*10>=minimum && value*10<=maximum)) return FALSE;
    *out=(short)(value*10+0.5); return TRUE;
}

static INT_PTR CALLBACK page_proc(HWND dialog,UINT message,WPARAM wparam,LPARAM lparam) {
    DocumentUi *ui=(DocumentUi *)GetWindowLongPtrW(dialog,GWLP_USERDATA);
    if(message==WM_INITDIALOG) {
        ui=(DocumentUi *)((PROPSHEETPAGEW *)lparam)->lParam;
        SetWindowLongPtrW(dialog,GWLP_USERDATA,(LONG_PTR)ui);
        set_mm(dialog,101,ui->data.publicMode.dmPaperWidth); set_mm(dialog,102,ui->data.publicMode.dmPaperLength);
        SetDlgItemInt(dialog,103,ui->data.publicMode.dmCopies,FALSE);
        CheckRadioButton(dialog,104,105,ui->data.publicMode.dmOrientation==DMORIENT_LANDSCAPE?105:104);
        for(unsigned speed=2;speed<=6;speed++) {
            WCHAR label[32]; _snwprintf(label,32,L"%u ips (%u mm/s)",speed,speed*254/10);
            SendDlgItemMessageW(dialog,106,CB_ADDSTRING,0,(LPARAM)label);
        }
        SendDlgItemMessageW(dialog,106,CB_SETCURSEL,ui->data.settings.speed-2,0);
        for(unsigned density=1;density<=15;density++) {
            WCHAR label[16]; _snwprintf(label,16,L"%u",density);
            SendDlgItemMessageW(dialog,107,CB_ADDSTRING,0,(LPARAM)label);
        }
        SendDlgItemMessageW(dialog,107,CB_SETCURSEL,ui->data.settings.density-1,0);
        SendDlgItemMessageW(dialog,108,CB_ADDSTRING,0,(LPARAM)L"Labels with gap");
        SendDlgItemMessageW(dialog,108,CB_ADDSTRING,0,(LPARAM)L"Black mark");
        SendDlgItemMessageW(dialog,108,CB_ADDSTRING,0,(LPARAM)L"Continuous");
        SendDlgItemMessageW(dialog,108,CB_SETCURSEL,ui->data.settings.media,0);
        set_mm(dialog,109,(short)ui->data.settings.gap);
        EnableWindow(GetDlgItem(dialog,109),ui->data.settings.media!=2);
        return TRUE;
    }
    if(!ui) return FALSE;
    if(message==WM_COMMAND && LOWORD(wparam)==110 && HIWORD(wparam)==BN_CLICKED) {
        CleanDevMode standard; defaults(&standard.publicMode);
        set_mm(dialog,101,standard.publicMode.dmPaperWidth);
        set_mm(dialog,102,standard.publicMode.dmPaperLength);
        SetDlgItemInt(dialog,103,standard.publicMode.dmCopies,FALSE);
        CheckRadioButton(dialog,104,105,104);
        SendDlgItemMessageW(dialog,106,CB_SETCURSEL,standard.settings.speed-2,0);
        SendDlgItemMessageW(dialog,107,CB_SETCURSEL,standard.settings.density-1,0);
        SendDlgItemMessageW(dialog,108,CB_SETCURSEL,standard.settings.media,0);
        set_mm(dialog,109,(short)standard.settings.gap);
        EnableWindow(GetDlgItem(dialog,109),TRUE);
        SendMessageW(GetParent(dialog),PSM_CHANGED,(WPARAM)dialog,0);
        return TRUE;
    }
    if(message==WM_COMMAND && (HIWORD(wparam)==EN_CHANGE || HIWORD(wparam)==BN_CLICKED || HIWORD(wparam)==CBN_SELCHANGE)) {
        if(LOWORD(wparam)==108 && HIWORD(wparam)==CBN_SELCHANGE)
            EnableWindow(GetDlgItem(dialog,109),SendDlgItemMessageW(dialog,108,CB_GETCURSEL,0,0)!=2);
        SendMessageW(GetParent(dialog),PSM_CHANGED,(WPARAM)dialog,0); return TRUE;
    }
    if(message==WM_NOTIFY && ((NMHDR *)lparam)->code==PSN_APPLY) {
        BOOL translated=FALSE;
        UINT copies=GetDlgItemInt(dialog,103,&translated,FALSE);
        short width=0,height=0;
        if(!read_mm(dialog,101,200,1000,&width) || !read_mm(dialog,102,100,3000,&height) ||
            !translated || copies<1 || copies>99) {
            MessageBoxW(dialog,L"Width: 20-100 mm; Height: 10-300 mm; Copies: 1-99.",CLEAN_NAME_W,MB_OK|MB_ICONWARNING);
            SetWindowLongPtrW(dialog,DWLP_MSGRESULT,PSNRET_INVALID_NOCHANGEPAGE); return TRUE;
        }
        LRESULT speed=SendDlgItemMessageW(dialog,106,CB_GETCURSEL,0,0);
        if(speed<0 || speed>4) { SetWindowLongPtrW(dialog,DWLP_MSGRESULT,PSNRET_INVALID_NOCHANGEPAGE); return TRUE; }
        ui->data.settings.speed=(DWORD)speed+2;
        LRESULT density=SendDlgItemMessageW(dialog,107,CB_GETCURSEL,0,0);
        if(density<0 || density>14) { SetWindowLongPtrW(dialog,DWLP_MSGRESULT,PSNRET_INVALID_NOCHANGEPAGE); return TRUE; }
        ui->data.settings.density=(DWORD)density+1;
        LRESULT media=SendDlgItemMessageW(dialog,108,CB_GETCURSEL,0,0);
        short gap=0;
        if(media<0 || media>2 || !read_mm(dialog,109,0,100,&gap)) {
            MessageBoxW(dialog,L"Gap / black mark: 0-10 mm.",CLEAN_NAME_W,MB_OK|MB_ICONWARNING);
            SetWindowLongPtrW(dialog,DWLP_MSGRESULT,PSNRET_INVALID_NOCHANGEPAGE); return TRUE;
        }
        ui->data.settings.media=(DWORD)media; ui->data.settings.gap=(DWORD)gap;
        ui->data.publicMode.dmPaperWidth=width; ui->data.publicMode.dmPaperLength=height; ui->data.publicMode.dmCopies=(short)copies;
        ui->data.publicMode.dmOrientation=IsDlgButtonChecked(dialog,105)==BST_CHECKED?DMORIENT_LANDSCAPE:DMORIENT_PORTRAIT;
        if(ui->header->pdmOut && (ui->header->fMode & DM_OUT_BUFFER))
            memcpy(ui->header->pdmOut,&ui->data.publicMode,sizeof(ui->data));
        ui->callback(ui->group,CPSFUNC_SET_RESULT,(LPARAM)ui->page,CPSUI_OK);
        SetWindowLongPtrW(dialog,DWLP_MSGRESULT,PSNRET_NOERROR); return TRUE;
    }
    return FALSE;
}

LONG WINAPI DrvDocumentPropertySheets(PPROPSHEETUI_INFO info,LPARAM parameter) {
    CleanDocumentHeader *h=(CleanDocumentHeader *)(info?info->lParamInit:parameter);
    if(!h || h->cbSize<sizeof(*h)) { SetLastError(ERROR_INVALID_PARAMETER); return -1; }
    if(!info) {
        if(!h->fMode || !h->pdmOut) { h->cbOut=sizeof(CleanDevMode); return sizeof(CleanDevMode); }
        CleanDevMode data; stored_defaults(h->hPrinter,&data.publicMode);
        if((h->fMode & DM_IN_BUFFER) && !merge(&data.publicMode,h->pdmIn)) return -1;
        if(h->fMode & DM_OUT_BUFFER) memcpy(h->pdmOut,&data,sizeof(data));
        h->cbOut=sizeof(data); return IDOK;
    }
    switch(info->Reason) {
    case PROPSHEETUI_REASON_INIT: {
        DocumentUi *ui=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*ui));
        if(!ui) return -1;
        ui->header=h; stored_defaults(h->hPrinter,&ui->data.publicMode);
        if((h->fMode & DM_IN_BUFFER) && !merge(&ui->data.publicMode,h->pdmIn)) { HeapFree(GetProcessHeap(),0,ui); return -1; }
        ui->group=info->hComPropSheet; ui->callback=info->pfnComPropSheet;
        info->UserData=(ULONG_PTR)ui;
        HMODULE module=NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)(uintptr_t)DrvDocumentPropertySheets,&module);
        PROPSHEETPAGEW page={0}; page.dwSize=sizeof(page); page.dwFlags=PSP_USETITLE;
        page.hInstance=module; page.pszTemplate=MAKEINTRESOURCEW(200); page.pszTitle=L"Label";
        page.pfnDlgProc=page_proc; page.lParam=(LPARAM)ui;
        ui->page=(HANDLE)info->pfnComPropSheet(info->hComPropSheet,CPSFUNC_ADD_PROPSHEETPAGEW,(LPARAM)&page,0);
        if(!ui->page) return -1;
        page.pszTemplate=MAKEINTRESOURCEW(202); page.pszTitle=L"About";
        page.pfnDlgProc=device_page_proc; page.lParam=0;
        return info->pfnComPropSheet(info->hComPropSheet,CPSFUNC_ADD_PROPSHEETPAGEW,(LPARAM)&page,0)?1:-1;
    }
    case PROPSHEETUI_REASON_GET_INFO_HEADER: {
        PPROPSHEETUI_INFO_HEADER header=(PPROPSHEETUI_INFO_HEADER)parameter;
        if(header) header->pTitle=CLEAN_NAME_W;
        return TRUE;
    }
    case PROPSHEETUI_REASON_SET_RESULT:
        if(parameter) info->Result=(ULONG_PTR)((PSETRESULT_INFO)parameter)->Result;
        return TRUE;
    case PROPSHEETUI_REASON_DESTROY:
        if(info->UserData) HeapFree(GetProcessHeap(),0,(PVOID)info->UserData);
        info->UserData=0; return TRUE;
    default: return TRUE;
    }
}

BOOL WINAPI DrvConvertDevMode(LPWSTR name,DEVMODEW *input,DEVMODEW *output,LONG *needed,DWORD mode) {
    if(!needed) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    LONG size=*needed; *needed=sizeof(CleanDevMode);
    if(!output || size<(LONG)sizeof(CleanDevMode)) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    CleanDevMode data; defaults(&data.publicMode);
    if(mode==4) { /* CDM_DRIVER_DEFAULT */
        HANDLE printer=NULL;
        if(name && OpenPrinterW(name,&printer,NULL)) { stored_defaults(printer,&data.publicMode); ClosePrinter(printer); }
    } else if(!merge(&data.publicMode,input)) return FALSE;
    memcpy(output,&data,sizeof(data)); return TRUE;
}

DWORD WINAPI DrvDeviceCapabilities(HANDLE printer,PWSTR name,WORD cap,PVOID output,DEVMODEW *dm) {
    CleanDevMode paper;
    /* Enumerated media describe the queue's current stock, rather than a
     * browser's cached DEVMODE from an earlier print preview. */
    HANDLE opened=NULL;
    if(!printer && name) OpenPrinterW(name,&opened,NULL);
    stored_defaults(printer?printer:opened,&paper.publicMode);
    if(!printer && !opened && !merge(&paper.publicMode,dm)) return (DWORD)-1;
    if(opened) ClosePrinter(opened);
    switch(cap) {
    case DC_FIELDS: return CLEAN_FIELDS;
    case DC_SIZE: return sizeof(DEVMODEW);
    case DC_EXTRA: return sizeof(CleanPrivate);
    case DC_VERSION: return DM_SPECVERSION;
    case DC_DRIVER: return 0x0102;
    case DC_MINEXTENT: return MAKELONG(200,100);
    case DC_MAXEXTENT: return MAKELONG(1000,3000);
    case DC_PAPERS: if(output) *(WORD *)output=DMPAPER_USER; return 1;
    case DC_PAPERSIZE:
        if(output) *(POINT *)output=(POINT){paper.publicMode.dmPaperWidth,paper.publicMode.dmPaperLength};
        return 1;
    case DC_PAPERNAMES: case DC_MEDIAREADY:
        if(output) {
            unsigned width=paper.publicMode.dmPaperWidth,height=paper.publicMode.dmPaperLength;
            memset(output,0,64*sizeof(WCHAR));
            if(width%10==0 && height%10==0)
                _snwprintf(output,64,L"Label %u x %u mm",width/10,height/10);
            else
                _snwprintf(output,64,L"Label %u.%u x %u.%u mm",width/10,width%10,height/10,height%10);
        }
        return 1;
    case DC_BINS: if(output) *(WORD *)output=DMBIN_AUTO; return 1;
    case DC_BINNAMES: if(output) { memset(output,0,24*sizeof(WCHAR)); lstrcpyW(output,L"Label roll"); } return 1;
    case DC_ENUMRESOLUTIONS: if(output) { ((LONG *)output)[0]=203; ((LONG *)output)[1]=203; } return 1;
    case DC_COPIES: return 99;
    case DC_ORIENTATION: return 90;
    case DC_DUPLEX: case DC_COLORDEVICE: case DC_COLLATE: return 0;
    case DC_TRUETYPE: return DCTT_BITMAP;
    case DC_NUP: if(output) *(DWORD *)output=1; return 1;
    case DC_MEDIATYPES: if(output) *(DWORD *)output=DMMEDIA_STANDARD; return 1;
    case DC_MEDIATYPENAMES: if(output) { memset(output,0,64*sizeof(WCHAR)); lstrcpyW(output,L"Thermal label"); } return 1;
    case DC_FILEDEPENDENCIES: return 0;
    default: return (DWORD)-1;
    }
}
BOOL WINAPI DrvDriverEvent(DWORD event,DWORD level,LPBYTE data,LPARAM parameter) {
    (void)event; (void)level; (void)data; (void)parameter; return TRUE;
}
BOOL WINAPI DrvPrinterEvent(LPWSTR name,INT event,DWORD flags,LPARAM parameter) {
    (void)name; (void)event; (void)flags; (void)parameter; return TRUE;
}
int WINAPI DrvDocumentEvent(HANDLE printer,HDC dc,int event,ULONG in_size,PVOID in,ULONG out_size,PVOID out) {
    (void)printer;(void)dc;(void)event;(void)in_size;(void)in;(void)out_size;(void)out;
    return DOCUMENTEVENT_UNSUPPORTED;
}
