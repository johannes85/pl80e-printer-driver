#define UNICODE
#include <windows.h>
#include <winddi.h>
#include <commctrl.h>
#include <prsht.h>
#include <compstui.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/settings.h"
#include "../src/version.h"
#define CLEAN_WIDEN_INNER(text) L##text
#define CLEAN_WIDEN(text) CLEAN_WIDEN_INNER(text)

typedef BOOL (APIENTRY *Enable)(ULONG,ULONG,DRVENABLEDATA *);
typedef BOOL (APIENTRY *Query)(DWORD,PVOID,DWORD,PDWORD);
typedef struct {
    WORD cbSize,Reserved;
    HANDLE printer; LPWSTR name; DEVMODEW *input,*output;
    DWORD out_size,mode;
} DocumentHeader;
typedef LONG (WINAPI *Document)(void *,LPARAM);
typedef DWORD (WINAPI *Capabilities)(HANDLE,PWSTR,WORD,PVOID,DEVMODEW *);
static PROPSHEETPAGEW ui_page;
static PROPSHEETPAGEW about_page;
static int applied;
static LONG_PTR CALLBACK mock_property_sheet(HANDLE group,UINT action,LPARAM first,LPARAM second) {
    (void)group;
    if(action==CPSFUNC_ADD_PROPSHEETPAGEW) {
        PROPSHEETPAGEW *added=(PROPSHEETPAGEW *)first;
        if(added->pszTemplate==MAKEINTRESOURCEW(202)) {
            about_page=*added;
            assert(wcscmp(about_page.pszTitle,L"About")==0);
            return 2;
        }
        ui_page=*added;
        assert(ui_page.dwSize==sizeof(ui_page) && ui_page.pfnDlgProc);
        assert(FindResourceW(ui_page.hInstance,ui_page.pszTemplate,RT_DIALOG));
        return 1;
    }
    if(action==CPSFUNC_SET_RESULT) { assert(second==CPSUI_OK); applied++; return 1; }
    return 0;
}
int main(int argc,char **argv) {
    assert(argc==2);
    HMODULE dll=LoadLibraryA(argv[1]);
    if(!dll) { fprintf(stderr,"LoadLibrary failed: %lu\n",GetLastError()); return 1; }
    Enable enable=(Enable)GetProcAddress(dll,"DrvEnableDriver");
    Query query=(Query)GetProcAddress(dll,"DrvQueryDriverInfo");
    assert(enable && query && GetProcAddress(dll,"DrvDisableDriver"));
    DRVENABLEDATA data={0};
    assert(!enable(DDI_DRIVER_VERSION_NT5,1,&data));
    assert(!enable(DDI_DRIVER_VERSION_NT4,sizeof(data),&data));
    assert(enable(DDI_DRIVER_VERSION_NT5_01,sizeof(data),&data));
    assert(data.iDriverVersion==DDI_DRIVER_VERSION_NT5 && data.c==10 && data.pdrvfn);
    for(ULONG i=0;i<data.c;i++) {
        assert(data.pdrvfn[i].pfn);
        for(ULONG j=0;j<i;j++) assert(data.pdrvfn[i].iFunc!=data.pdrvfn[j].iFunc);
    }
    DWORD needed=0,usermode=0;
    assert(!query(DRVQUERY_USERMODE,NULL,0,&needed) && needed==4);
    assert(query(DRVQUERY_USERMODE,&usermode,sizeof(usermode),&needed) && usermode==TRUE);
    assert(!query(0xffffffffu,&usermode,sizeof(usermode),&needed));
    Document document=(Document)GetProcAddress(dll,"DrvDocumentPropertySheets");
    Capabilities capabilities=(Capabilities)GetProcAddress(dll,"DrvDeviceCapabilities");
    assert(document && capabilities && GetProcAddress(dll,"DrvConvertDevMode"));
    DocumentHeader h={0}; h.cbSize=sizeof(h);
    assert(document(NULL,(LPARAM)&h)==sizeof(CleanDevMode) && h.out_size==sizeof(CleanDevMode));
    CleanDevMode storage={0};
#define dm storage.publicMode
    h.output=&dm; h.mode=DM_OUT_BUFFER;
    assert(document(NULL,(LPARAM)&h)==IDOK);
    assert(dm.dmSize==sizeof(DEVMODEW) && dm.dmPaperWidth==1000 && dm.dmPaperLength==1500 &&
        dm.dmPrintQuality==203 && dm.dmCopies==1 && dm.dmOrientation==DMORIENT_PORTRAIT);
    assert(wcscmp(dm.dmDeviceName,strstr(argv[1],"PL80ECleanDev.dll")?L"PL80E Open Source (DEV)":L"PL80E Open Source")==0);
    CleanDevMode input_storage=storage;
#define in input_storage.publicMode
    in.dmOrientation=DMORIENT_LANDSCAPE; in.dmCopies=7;
    h.input=&in; h.mode=DM_IN_BUFFER|DM_OUT_BUFFER;
    assert(document(NULL,(LPARAM)&h)==IDOK && dm.dmOrientation==DMORIENT_LANDSCAPE && dm.dmCopies==7);
    in.dmPaperWidth=1100;
    assert(document(NULL,(LPARAM)&h)==-1);
    assert(capabilities(NULL,NULL,DC_COPIES,NULL,NULL)==99 && capabilities(NULL,NULL,DC_COLORDEVICE,NULL,NULL)==0);
    in.dmPaperWidth=1000;
    h.mode=DM_IN_BUFFER|DM_OUT_BUFFER|DM_IN_PROMPT;
    PROPSHEETUI_INFO ui={0}; ui.cbSize=sizeof(ui); ui.Reason=PROPSHEETUI_REASON_INIT;
    ui.lParamInit=(LPARAM)&h; ui.pfnComPropSheet=mock_property_sheet;
    assert(document(&ui,(LPARAM)&h)==1 && ui.UserData);
    /* Instantiate the real native resource in a hidden parent, so control IDs,
     * initial values, parsing and Apply can be tested without showing a UI. */
    HWND parent=CreateWindowExW(0,L"STATIC",L"",0,0,0,240,160,NULL,NULL,NULL,NULL);
    assert(parent);
    HWND dialog=CreateDialogParamW(ui_page.hInstance,ui_page.pszTemplate,parent,ui_page.pfnDlgProc,(LPARAM)&ui_page);
    assert(dialog);
    assert(about_page.pfnDlgProc);
    HWND about=CreateDialogParamW(about_page.hInstance,about_page.pszTemplate,parent,about_page.pfnDlgProc,0);
    assert(about);
    WCHAR about_text[128]; GetDlgItemTextW(about,120,about_text,128);
    assert(wcscmp(about_text,CLEAN_WIDEN(CLEAN_VERSION_TEXT))==0);
    /* Author and project URL are editable project metadata, not fixed fixtures. */
    assert(GetDlgItem(about,121) && GetWindowTextLengthW(GetDlgItem(about,121))>0);
    assert(GetDlgItem(about,122) && GetWindowTextLengthW(GetDlgItem(about,122))>0);
    DestroyWindow(about);
    SetDlgItemTextW(dialog,101,L"45,5"); SetDlgItemTextW(dialog,102,L"25.0");
    SetDlgItemInt(dialog,103,3,FALSE); CheckRadioButton(dialog,104,105,104);
    assert(SendDlgItemMessageW(dialog,106,CB_GETCOUNT,0,0)==5);
    SendDlgItemMessageW(dialog,106,CB_SETCURSEL,0,0);
    assert(SendDlgItemMessageW(dialog,107,CB_GETCOUNT,0,0)==15);
    assert(SendDlgItemMessageW(dialog,107,CB_GETCURSEL,0,0)==9);
    SendDlgItemMessageW(dialog,107,CB_SETCURSEL,14,0);
    assert(SendDlgItemMessageW(dialog,108,CB_GETCOUNT,0,0)==3);
    assert(SendDlgItemMessageW(dialog,108,CB_GETCURSEL,0,0)==0);
    SendDlgItemMessageW(dialog,108,CB_SETCURSEL,1,0);
    SetDlgItemTextW(dialog,109,L"4,5");
    NMHDR notify={0}; notify.hwndFrom=parent; notify.code=PSN_APPLY;
    SendMessageW(dialog,WM_NOTIFY,0,(LPARAM)&notify);
    assert(applied==1 && dm.dmPaperWidth==455 && dm.dmPaperLength==250 && dm.dmCopies==3 && dm.dmOrientation==DMORIENT_PORTRAIT);
    assert(clean_private(&dm) && clean_private(&dm)->speed==2);
    assert(clean_private(&dm)->density==15);
    assert(clean_private(&dm)->media==1 && clean_private(&dm)->gap==45);
    POINT paper_size={0}; WCHAR paper_name[64]={0}; WORD paper_id=0;
    assert(capabilities(NULL,NULL,DC_PAPERS,&paper_id,&dm)==1 && paper_id==DMPAPER_USER);
    assert(capabilities(NULL,NULL,DC_PAPERSIZE,&paper_size,&dm)==1 && paper_size.x==455 && paper_size.y==250);
    assert(capabilities(NULL,NULL,DC_PAPERNAMES,paper_name,&dm)==1 && wcscmp(paper_name,L"Label 45.5 x 25.0 mm")==0);
    dm.dmOrientation=DMORIENT_LANDSCAPE;
    assert(capabilities(NULL,NULL,DC_PAPERSIZE,&paper_size,&dm)==1 && paper_size.x==455 && paper_size.y==250);
    assert(capabilities(NULL,NULL,DC_MEDIAREADY,paper_name,&dm)==1 && wcscmp(paper_name,L"Label 45.5 x 25.0 mm")==0);
    dm.dmOrientation=DMORIENT_PORTRAIT;
    assert(capabilities(NULL,NULL,DC_PAPERNAMES,paper_name,NULL)==1 && wcscmp(paper_name,L"Label 100 x 150 mm")==0);
    input_storage=storage; h.mode=DM_IN_BUFFER|DM_OUT_BUFFER;
    assert(document(NULL,(LPARAM)&h)==IDOK && clean_private(&dm)->speed==2);
    assert(clean_private(&dm)->density==15);
    input_storage.settings.density=0;
    assert(!clean_private(&in));
    /* Older saved settings have only magic+speed; retain their speed and
     * migrate density from the current default without reading beyond them. */
    in.dmDriverExtra=8;
    assert(document(NULL,(LPARAM)&h)==IDOK && clean_private(&dm)->speed==2 && clean_private(&dm)->density==10);
    /* Reset edits the controls only; cancelling still preserves the caller's
     * DEVMODE. Applying then restores every field, including disabled Gap. */
    SendDlgItemMessageW(dialog,108,CB_SETCURSEL,2,0);
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(108,CBN_SELCHANGE),0);
    assert(!IsWindowEnabled(GetDlgItem(dialog,109)));
    CleanDevMode before_reset=storage;
    assert(GetDlgItem(dialog,110));
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0);
    assert(memcmp(&storage,&before_reset,sizeof(storage))==0);
    assert(IsWindowEnabled(GetDlgItem(dialog,109)));
    SendMessageW(dialog,WM_NOTIFY,0,(LPARAM)&notify);
    assert(applied==2 && dm.dmPaperWidth==1000 && dm.dmPaperLength==1500 && dm.dmCopies==1);
    assert(dm.dmOrientation==DMORIENT_PORTRAIT && clean_private(&dm)->speed==5 && clean_private(&dm)->density==10);
    assert(clean_private(&dm)->media==0 && clean_private(&dm)->gap==30);
    DestroyWindow(dialog); DestroyWindow(parent);
    ui.Reason=PROPSHEETUI_REASON_DESTROY; assert(document(&ui,0)==TRUE && !ui.UserData);
    Document device=(Document)GetProcAddress(dll,"DrvDevicePropertySheets");
    assert(device);
    ui=(PROPSHEETUI_INFO){0}; ui.cbSize=sizeof(ui); ui.Reason=PROPSHEETUI_REASON_INIT;
    ui.pfnComPropSheet=mock_property_sheet;
    assert(device(&ui,0)==1);
    parent=CreateWindowExW(0,L"STATIC",L"",0,0,0,280,160,NULL,NULL,NULL,NULL);
    assert(parent);
    dialog=CreateDialogParamW(ui_page.hInstance,ui_page.pszTemplate,parent,ui_page.pfnDlgProc,0);
    assert(dialog);
    SendMessageW(dialog,WM_NOTIFY,0,(LPARAM)&notify);
    assert(GetWindowLongPtrW(dialog,DWLP_MSGRESULT)==PSNRET_NOERROR);
    DestroyWindow(dialog); DestroyWindow(parent);
    ui.Reason=PROPSHEETUI_REASON_DESTROY; assert(device(&ui,0)==TRUE);
    assert(FreeLibrary(dll));
    DWORD ignored=0,version_size=GetFileVersionInfoSizeA(argv[1],&ignored);
    assert(version_size>0);
    void *version=malloc(version_size); assert(version);
    assert(GetFileVersionInfoA(argv[1],0,version_size,version));
    VS_FIXEDFILEINFO *fixed=NULL; UINT fixed_size=0;
    assert(VerQueryValueA(version,"\\",(void **)&fixed,&fixed_size));
    assert(fixed_size>=sizeof(*fixed) && fixed->dwFileType==VFT_DRV &&
        fixed->dwFileSubtype==VFT2_DRV_VERSIONED_PRINTER && fixed->dwFileVersionMS==3);
    free(version);
    puts("PASS: DLL, DDI, Type-3 metadata, DEVMODE, native settings dialog and Apply");
    return 0;
}
