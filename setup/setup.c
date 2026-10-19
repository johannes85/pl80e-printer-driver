#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winspool.h>
#include <setupapi.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <sddl.h>
#include <aclapi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include <io.h>
#include <fcntl.h>
#include "../src/version.h"

#define WIDEN_(x) L##x
#define WIDEN(x) WIDEN_(x)
#ifdef SETUP_INTEGRATION_TEST
#define DRIVER L"PL80E Open Source Setup Test"
#define SUBJECT L"CN=PL80E Open Source Setup Test"
#define BASE L"PL80ESetupTest"
#else
#define DRIVER L"PL80E Open Source"
#define SUBJECT L"CN=PL80E Open Source Local Installation"
#define BASE L"PL80EClean"
#endif
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
WINSETUPAPI BOOL WINAPI SetupGetInfPublishedNameW(PCWSTR,PWSTR,DWORD,PDWORD);
WINSETUPAPI BOOL WINAPI SetupGetInfDriverStoreLocationW(PCWSTR,PSP_ALTPLATFORM_INFO,PCWSTR,PWSTR,DWORD,PDWORD);
#define szOID_KP_CODE_SIGNING "1.3.6.1.5.5.7.3.3"
#define ITEMS 128
#define ENTRYLEN 512
#define PATHLEN 32768
#define WM_LOG (WM_APP+1)
#define WM_DONE (WM_APP+2)
static const wchar_t *payload[]={BASE L".dll",BASE L".gpd",BASE L".inf",BASE L".cat"};
static wchar_t root[PATHLEN], statepath[PATHLEN], source[PATHLEN], error[2048];
static HWND window, combo, output, statuslabel, installbutton, uninstallbutton, refreshbutton;
static HFONT font, titlefont;
static BOOL busy, installed;
static wchar_t portnames[ITEMS][ENTRYLEN];
static unsigned portcount;
typedef struct { wchar_t thumb[41], inf[PATHLEN]; wchar_t queues[ITEMS][ENTRYLEN], packages[ITEMS][ENTRYLEN]; unsigned nq,np; BOOL owns; } State;
static State state;
static BOOL fail(const wchar_t *message,DWORD code) {
    wchar_t detail[1024]=L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,code,0,detail,COUNT(detail),NULL);
    StringCchPrintfW(error,COUNT(error),L"%ls (0x%08lX)\r\n%ls",message,code,detail); return FALSE;
}
#define CHECK(x,msg) do {if(!(x)) return fail(msg,GetLastError());} while(0)
static BOOL join(wchar_t *out,const wchar_t *dir,const wchar_t *file) {
    return SUCCEEDED(StringCchPrintfW(out,PATHLEN,L"%ls\\%ls",dir,file));
}
static void logline(const wchar_t *s) {
    if(window) {wchar_t *copy=_wcsdup(s);if(copy && !PostMessageW(window,WM_LOG,0,(LPARAM)copy)) free(copy);}
}
static BOOL exists(const wchar_t *p) {return GetFileAttributesW(p)!=INVALID_FILE_ATTRIBUTES;}
static BOOL protected_dir(const wchar_t *p) {
    PSECURITY_DESCRIPTOR sd=NULL;
    CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",SDDL_REVISION_1,&sd,NULL),L"Creating directory security failed");
    SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};
    BOOL ok=CreateDirectoryW(p,&sa);DWORD e=GetLastError();
    if(!ok && e!=ERROR_ALREADY_EXISTS) {LocalFree(sd);return fail(L"Creating installation directory failed",e);}
    // Open the entry itself and hold its identity while applying the protected ACL.
    HANDLE h=CreateFileW(p,READ_CONTROL|WRITE_DAC|WRITE_OWNER,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(h==INVALID_HANDLE_VALUE) {e=GetLastError();LocalFree(sd);return fail(L"Opening installation directory failed",e);}
    FILE_ATTRIBUTE_TAG_INFO info;
    ok=GetFileInformationByHandleEx(h,FileAttributeTagInfo,&info,sizeof(info));
    if(!ok || (info.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {CloseHandle(h);LocalFree(sd);return fail(L"Installation directory must not be a link",ERROR_INVALID_DATA);}
    PSID owner=NULL;PACL dacl=NULL;BOOL present,def;
    GetSecurityDescriptorOwner(sd,&owner,&def);GetSecurityDescriptorDacl(sd,&present,&dacl,&def);
    e=SetSecurityInfo(h,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,owner,NULL,dacl,NULL);
    CloseHandle(h);LocalFree(sd);return e==ERROR_SUCCESS?TRUE:fail(L"Protecting installation directory failed",e);
}
static BOOL write_bytes(const wchar_t *path,const void *data,DWORD size) {
    HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE) return fail(L"Creating state file failed",GetLastError());
    DWORD wrote=0;BOOL ok=WriteFile(f,data,size,&wrote,NULL) && wrote==size && FlushFileBuffers(f);DWORD e=GetLastError();CloseHandle(f);
    return ok?TRUE:fail(L"Writing state file failed",e);
}
static wchar_t *read_text(const wchar_t *path) {
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(f==INVALID_HANDLE_VALUE) {fail(L"Reading file failed",GetLastError());return NULL;}
    FILE_ATTRIBUTE_TAG_INFO tag;LARGE_INTEGER length;
    if(!GetFileInformationByHandleEx(f,FileAttributeTagInfo,&tag,sizeof(tag)) || (tag.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || !GetFileSizeEx(f,&length) || length.QuadPart>1024*1024) {CloseHandle(f);fail(L"Invalid state or INF file",ERROR_INVALID_DATA);return NULL;}
    DWORD size=(DWORD)length.QuadPart,n=0;char *bytes=calloc(size+2,1);
    if(!bytes) {CloseHandle(f);fail(L"Out of memory",ERROR_OUTOFMEMORY);return NULL;}
    BOOL ok=ReadFile(f,bytes,size,&n,NULL)&&n==size;CloseHandle(f);
    wchar_t *text=NULL;
    if(ok) {
        if(size>=2 && (BYTE)bytes[0]==255 && (BYTE)bytes[1]==254) {text=calloc(size+2,1);if(text) memcpy(text,bytes+2,size-2);}
        else {int chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,size,NULL,0);if(chars>0) {text=calloc((size_t)chars+1,sizeof(wchar_t));if(text) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,size,text,chars);}}
    }
    free(bytes);if(!text) fail(L"Invalid text file",ERROR_INVALID_DATA);return text;
}
static BOOL published_valid(const wchar_t *s) {
    if(_wcsnicmp(s,L"oem",3)) return FALSE;s+=3;const wchar_t *start=s;while(*s>=L'0'&&*s<=L'9') s++;
    return s!=start && !_wcsicmp(s,L".inf");
}
static BOOL add_item(wchar_t items[ITEMS][ENTRYLEN],unsigned *n,const wchar_t *s) {
    if(!*s || wcslen(s)>=ENTRYLEN) return fail(L"Invalid installation state entry",ERROR_INVALID_DATA);
    for(unsigned i=0;i<*n;i++) if(!_wcsicmp(items[i],s)) return TRUE;
    if(*n==ITEMS) return fail(L"Too many installation state entries",ERROR_BUFFER_OVERFLOW);
    StringCchCopyW(items[(*n)++],ENTRYLEN,s);return TRUE;
}
// Bounded JSON reader accepts the previous C#/PowerShell state format. Unknown
// fields and duplicate fields are rejected instead of weakening removal checks.
static void whitespace(const wchar_t **p) {while(iswspace(**p)) (*p)++;}
static BOOL json_string(const wchar_t **p,wchar_t *out,size_t cap) {
    whitespace(p);if(**p!=L'"') return FALSE;(*p)++;size_t n=0;
    while(**p && **p!=L'"') {
        wchar_t c=*(*p)++;if(c<32) return FALSE;
        if(c==L'\\') {c=*(*p)++;switch(c) {case L'"':case L'\\':case L'/':break;case L'n':c=L'\n';break;case L'r':c=L'\r';break;case L't':c=L'\t';break;case L'b':c=L'\b';break;case L'f':c=L'\f';break;case L'u': {unsigned v=0;for(int i=0;i<4;i++) {if(!**p) return FALSE;wchar_t h=*(*p)++;unsigned d=h>=L'0'&&h<=L'9'?h-L'0':h>=L'a'&&h<=L'f'?h-L'a'+10:h>=L'A'&&h<=L'F'?h-L'A'+10:16;if(d==16) return FALSE;v=v*16+d;}if(!v) return FALSE;c=(wchar_t)v;break;}default:return FALSE;}}
        if(n+1>=cap) return FALSE;out[n++]=c;
    }
    if(**p!=L'"') return FALSE;(*p)++;out[n]=0;return TRUE;
}
static BOOL json_array(const wchar_t **p,wchar_t items[ITEMS][ENTRYLEN],unsigned *n) {
    whitespace(p);if(*(*p)++!=L'[') return FALSE;whitespace(p);if(**p==L']') {(*p)++;return TRUE;}
    for(;;) {wchar_t s[ENTRYLEN];if(!json_string(p,s,COUNT(s))||!add_item(items,n,s)) return FALSE;whitespace(p);wchar_t c=*(*p)++;if(c==L']') return TRUE;if(c!=L',') return FALSE;}
}
static BOOL parse_state(const wchar_t *p,State *s) {
    ZeroMemory(s,sizeof(*s));s->owns=TRUE;unsigned seen=0;whitespace(&p);if(*p++!=L'{') return FALSE;
    for(;;) {
        wchar_t key[64],name[ENTRYLEN];if(!json_string(&p,key,COUNT(key))) return FALSE;whitespace(&p);if(*p++!=L':') return FALSE;whitespace(&p);unsigned bit=0;
        if(!_wcsicmp(key,L"DriverName")) {bit=1;if(!json_string(&p,name,COUNT(name))||wcscmp(name,DRIVER)) return FALSE;}
        else if(!_wcsicmp(key,L"Thumbprint")) {bit=2;if(!json_string(&p,s->thumb,COUNT(s->thumb))) return FALSE;}
        else if(!_wcsicmp(key,L"Queues")) {bit=4;if(!json_array(&p,s->queues,&s->nq)) return FALSE;}
        else if(!_wcsicmp(key,L"Packages")) {bit=8;if(!json_array(&p,s->packages,&s->np)) return FALSE;}
        else if(!_wcsicmp(key,L"InfPath")) {bit=16;if(!wcsncmp(p,L"null",4)) p+=4;else if(!json_string(&p,s->inf,COUNT(s->inf))) return FALSE;}
        else if(!_wcsicmp(key,L"OwnsCertificate")) {bit=32;if(!wcsncmp(p,L"true",4)) {p+=4;s->owns=TRUE;}else if(!wcsncmp(p,L"false",5)) {p+=5;s->owns=FALSE;}else return FALSE;}
        else return FALSE;
        if(seen&bit) return FALSE;seen|=bit;whitespace(&p);wchar_t c=*p++;if(c==L'}') break;if(c!=L',') return FALSE;
    }
    whitespace(&p);if(*p || (seen&7)!=7) return FALSE;
    if(s->owns) {if(wcslen(s->thumb)!=40) return FALSE;for(unsigned i=0;i<40;i++) if(!iswxdigit(s->thumb[i])) return FALSE;} else if(*s->thumb) return FALSE;
    for(unsigned i=0;i<s->nq;i++) if(wcsstr(s->queues[i],L"(DEV)")||wcsstr(s->queues[i],L"(dev)")) return FALSE;
    for(unsigned i=0;i<s->np;i++) if(!published_valid(s->packages[i])) return FALSE;
    return TRUE;
}
static BOOL read_state(BOOL *found) {
    *found=exists(statepath);if(!*found) {ZeroMemory(&state,sizeof(state));return TRUE;}
    wchar_t *text=read_text(statepath);if(!text) return FALSE;
    const wchar_t *p=text;if(*p==0xfeff) p++;BOOL ok=parse_state(p,&state);free(text);
    return ok?TRUE:fail(L"Installation state does not belong to this installer",ERROR_INVALID_DATA);
}
static BOOL json_write_string(FILE *f,const wchar_t *s) {
    fputc('"',f);for(;*s;s++) {if(*s==L'"'||*s==L'\\') {fputc('\\',f);fputc((int)*s,f);}else if(*s<32||*s>126) fprintf(f,"\\u%04x",(unsigned)*s);else fputc((int)*s,f);}return fputc('"',f)!=EOF;
}
static BOOL save_state(void) {
    wchar_t temp[PATHLEN];join(temp,root,L"installation.json.new");
    if(exists(temp) && !DeleteFileW(temp)) return fail(L"Removing stale state file failed",GetLastError());
    HANDLE h=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE) return fail(L"Creating state file failed",GetLastError());
    int fd=_open_osfhandle((intptr_t)h,_O_WRONLY|_O_BINARY);
    if(fd<0) {CloseHandle(h);return fail(L"Opening state stream failed",ERROR_WRITE_FAULT);}
    FILE *f=_fdopen(fd,"wb");if(!f) {_close(fd);return fail(L"Opening state stream failed",ERROR_WRITE_FAULT);}
    fputs("{\n\"DriverName\":",f);json_write_string(f,DRIVER);fputs(",\n\"Thumbprint\":",f);json_write_string(f,state.thumb);
    fputs(",\n\"InfPath\":",f);json_write_string(f,state.inf);fputs(",\n\"Queues\":[",f);
    for(unsigned i=0;i<state.nq;i++) {if(i) fputc(',',f);json_write_string(f,state.queues[i]);}
    fputs("],\n\"Packages\":[",f);for(unsigned i=0;i<state.np;i++) {if(i) fputc(',',f);json_write_string(f,state.packages[i]);}
    fprintf(f,"],\n\"OwnsCertificate\":%s\n}\n",state.owns?"true":"false");
    BOOL ok=fflush(f)==0&&!ferror(f);if(ok) ok=FlushFileBuffers(h);ok=fclose(f)==0&&ok;
    if(!ok) return fail(L"Writing installation state failed",ERROR_WRITE_FAULT);
    CHECK(MoveFileExW(temp,statepath,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),L"Saving installation state failed");return TRUE;
}
static void *printers(DWORD *count) {
    DWORD bytes=0;*count=0;EnumPrintersW(PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS,NULL,2,NULL,0,&bytes,count);
    if(!bytes) {if(GetLastError()!=ERROR_SUCCESS) {*count=MAXDWORD;fail(L"Reading printers failed",GetLastError());}return NULL;}void *data=calloc(bytes,1);if(!data) {*count=MAXDWORD;fail(L"Out of memory",ERROR_OUTOFMEMORY);return NULL;}
    if(!EnumPrintersW(PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS,NULL,2,data,bytes,&bytes,count)) {free(data);*count=MAXDWORD;fail(L"Reading printers failed",GetLastError());return NULL;}return data;
}
static BOOL driver_installed(void) {
    DWORD n=0,bytes=0;EnumPrinterDriversW(NULL,L"Windows x64",3,NULL,0,&bytes,&n);if(!bytes) return FALSE;
    DRIVER_INFO_3W *data=calloc(bytes,1);if(!data) return FALSE;BOOL found=FALSE;
    if(EnumPrinterDriversW(NULL,L"Windows x64",3,(BYTE*)data,bytes,&bytes,&n)) for(DWORD i=0;i<n;i++) if(!wcscmp(data[i].pName,DRIVER)) found=TRUE;
    free(data);return found;
}
static BOOL usb_name(const wchar_t *s) {if(_wcsnicmp(s,L"USB",3)) return FALSE;s+=3;if(!*s) return FALSE;for(;*s;s++) if(*s<L'0'||*s>L'9') return FALSE;return TRUE;}
static BOOL load_ports(void) {
    DWORD bytes=0,n=0;portcount=0;EnumPortsW(NULL,2,NULL,0,&bytes,&n);if(!bytes) return TRUE;
    PORT_INFO_2W *data=calloc(bytes,1);if(!data) return fail(L"Out of memory",ERROR_OUTOFMEMORY);
    BOOL ok=EnumPortsW(NULL,2,(BYTE*)data,bytes,&bytes,&n);
    if(ok) for(DWORD i=0;i<n;i++) if(usb_name(data[i].pPortName)&&!add_item(portnames,&portcount,data[i].pPortName)) {ok=FALSE;break;}
    DWORD e=GetLastError();free(data);return ok?TRUE:fail(L"Enumerating USB printer ports failed",e);
}
static BOOL existing_inf(wchar_t *out) {
    wchar_t path[1024];StringCchPrintfW(path,COUNT(path),L"SYSTEM\\CurrentControlSet\\Control\\Print\\Environments\\Windows x64\\Drivers\\Version-3\\%ls",DRIVER);
    DWORD size=PATHLEN*sizeof(wchar_t);out[0]=0;LSTATUS e=RegGetValueW(HKEY_LOCAL_MACHINE,path,L"InfPath",RRF_RT_REG_SZ,NULL,out,&size);
    return e==ERROR_SUCCESS||e==ERROR_FILE_NOT_FOUND;
}
static BOOL remember_inf(const wchar_t *inf) {
    if(!*inf) return TRUE;wchar_t published[PATHLEN];DWORD required;
    if(!SetupGetInfPublishedNameW(inf,published,COUNT(published),&required)) {
        DWORD e=GetLastError();
        // An earlier uninstall attempt may already have removed this package.
        // Keep the recorded validated package list for the remaining cleanup.
        if(e==ERROR_FILE_NOT_FOUND || e==ERROR_PATH_NOT_FOUND) return TRUE;
        return fail(L"Resolving installed INF failed",e);
    }
    wchar_t *leaf=wcsrchr(published,L'\\');leaf=leaf?leaf+1:published;
    return published_valid(leaf)?add_item(state.packages,&state.np,leaf):fail(L"Unexpected Driver Store name",ERROR_INVALID_DATA);
}
static HCERTSTORE store_open(const wchar_t *name) {return CertOpenStore(CERT_STORE_PROV_SYSTEM_W,0,0,CERT_SYSTEM_STORE_LOCAL_MACHINE,name);}
static PCCERT_CONTEXT find_cert(const wchar_t *store,const wchar_t *thumb) {
    BYTE hash[20];if(wcslen(thumb)!=40) return NULL;
    for(unsigned i=0;i<20;i++) {wchar_t pair[]={thumb[i*2],thumb[i*2+1],0};hash[i]=(BYTE)wcstoul(pair,NULL,16);}
    CRYPT_HASH_BLOB blob={20,hash};HCERTSTORE s=store_open(store);if(!s) return NULL;
    PCCERT_CONTEXT c=CertFindCertificateInStore(s,X509_ASN_ENCODING,0,CERT_FIND_SHA1_HASH,&blob,NULL);CertCloseStore(s,0);return c;
}
static BOOL validate_cert(PCCERT_CONTEXT c) {
    wchar_t subject[256];CertNameToStrW(X509_ASN_ENCODING,&c->pCertInfo->Subject,CERT_X500_NAME_STR,subject,COUNT(subject));
    if(wcscmp(subject,SUBJECT)) return fail(L"Unexpected signing certificate subject",ERROR_INVALID_DATA);
    DWORD bytes=0;CertGetEnhancedKeyUsage(c,CERT_FIND_EXT_ONLY_ENHKEY_USAGE_FLAG,NULL,&bytes);
    PCERT_ENHKEY_USAGE eku=calloc(bytes?bytes:1,1);BOOL ok=eku && CertGetEnhancedKeyUsage(c,CERT_FIND_EXT_ONLY_ENHKEY_USAGE_FLAG,eku,&bytes) && eku->cUsageIdentifier==1 && !strcmp(eku->rgpszUsageIdentifier[0],szOID_KP_CODE_SIGNING);free(eku);
    PCERT_EXTENSION ext=CertFindExtension(szOID_BASIC_CONSTRAINTS2,c->pCertInfo->cExtension,c->pCertInfo->rgExtension);
    if(ext) {CERT_BASIC_CONSTRAINTS2_INFO basic;bytes=sizeof(basic);ok=ok&&CryptDecodeObject(X509_ASN_ENCODING,X509_BASIC_CONSTRAINTS2,ext->Value.pbData,ext->Value.cbData,0,&basic,&bytes)&&!basic.fCA;}
    return ok?TRUE:fail(L"Unexpected certificate usage; trust retained",ERROR_INVALID_DATA);
}
static PCCERT_CONTEXT create_cert(void) {
    NCRYPT_PROV_HANDLE provider=0;NCRYPT_KEY_HANDLE key=0;PCCERT_CONTEXT cert=NULL;GUID id;wchar_t guid[40],name[80];DWORD length=3072,usage=NCRYPT_ALLOW_SIGNING_FLAG;
    CoCreateGuid(&id);StringFromGUID2(&id,guid,COUNT(guid));StringCchPrintfW(name,COUNT(name),L"PL80E-%ls",guid);
    SECURITY_STATUS e=NCryptOpenStorageProvider(&provider,MS_KEY_STORAGE_PROVIDER,0);
    if(!e) e=NCryptCreatePersistedKey(provider,&key,NCRYPT_RSA_ALGORITHM,name,0,NCRYPT_MACHINE_KEY_FLAG);
    if(!e) e=NCryptSetProperty(key,NCRYPT_LENGTH_PROPERTY,(BYTE*)&length,sizeof(length),0);
    if(!e) e=NCryptSetProperty(key,NCRYPT_KEY_USAGE_PROPERTY,(BYTE*)&usage,sizeof(usage),0);
    if(!e) e=NCryptFinalizeKey(key,0);
    BYTE subject[512];DWORD size=sizeof(subject);CERT_NAME_BLOB blob={0,subject};
    BYTE basicbuf[64],usagebuf[64],ekubuf[128];DWORD bs=sizeof(basicbuf),us=sizeof(usagebuf),es=sizeof(ekubuf);
    CERT_BASIC_CONSTRAINTS2_INFO basic={0};BYTE signaturebit=CERT_DIGITAL_SIGNATURE_KEY_USAGE;CRYPT_BIT_BLOB bits={1,&signaturebit,0};
    LPSTR oid=szOID_KP_CODE_SIGNING;CERT_ENHKEY_USAGE eku={1,&oid};
    BOOL ok=!e && CertStrToNameW(X509_ASN_ENCODING,SUBJECT,CERT_X500_NAME_STR,NULL,subject,&size,NULL) &&
        CryptEncodeObject(X509_ASN_ENCODING,X509_BASIC_CONSTRAINTS2,&basic,basicbuf,&bs) &&
        CryptEncodeObject(X509_ASN_ENCODING,X509_KEY_USAGE,&bits,usagebuf,&us) &&
        CryptEncodeObject(X509_ASN_ENCODING,X509_ENHANCED_KEY_USAGE,&eku,ekubuf,&es);
    if(ok) {
        blob.cbData=size;CERT_EXTENSION exts[]={{szOID_BASIC_CONSTRAINTS2,TRUE,{bs,basicbuf}},{szOID_KEY_USAGE,TRUE,{us,usagebuf}},{szOID_ENHANCED_KEY_USAGE,TRUE,{es,ekubuf}}};CERT_EXTENSIONS extensions={COUNT(exts),exts};
        CRYPT_KEY_PROV_INFO info={name,MS_KEY_STORAGE_PROVIDER,0,NCRYPT_MACHINE_KEY_FLAG,0,NULL,CERT_NCRYPT_KEY_SPEC};
        CRYPT_ALGORITHM_IDENTIFIER algorithm={szOID_RSA_SHA256RSA,{0,NULL}};
        SYSTEMTIME start,end;GetSystemTime(&start);end=start;end.wYear+=5;if(end.wMonth==2&&end.wDay==29) end.wDay=28;
        cert=CertCreateSelfSignCertificate(key,&blob,0,&info,&algorithm,&start,&end,&extensions);
        if(cert) {HCERTSTORE store=store_open(L"MY");PCCERT_CONTEXT saved=NULL;if(!store || !CertAddCertificateContextToStore(store,cert,CERT_STORE_ADD_NEW,&saved)) {CertFreeCertificateContext(cert);cert=NULL;}else {CertFreeCertificateContext(cert);cert=saved;}if(store) CertCloseStore(store,0);}
    }
    if(!cert) {DWORD code=e?(DWORD)e:GetLastError();if(key) {NCryptDeleteKey(key,0);key=0;}fail(L"Creating local signing identity failed",code);}
    if(key) NCryptFreeObject(key);if(provider) NCryptFreeObject(provider);return cert;
}
static BOOL trust_cert(PCCERT_CONTEXT cert) {
    // Public-only contexts prevent private-key provider properties from being
    // copied into trust stores, matching the previous installer's behavior.
    PCCERT_CONTEXT public=CertCreateCertificateContext(X509_ASN_ENCODING,cert->pbCertEncoded,cert->cbCertEncoded);if(!public) return fail(L"Reading public certificate failed",GetLastError());
    const wchar_t *names[]={L"ROOT",L"TrustedPublisher"};BOOL ok=TRUE;
    for(unsigned i=0;i<COUNT(names)&&ok;i++) {HCERTSTORE store=store_open(names[i]);ok=store&&CertAddCertificateContextToStore(store,public,CERT_STORE_ADD_REPLACE_EXISTING,NULL);if(store) CertCloseStore(store,0);}
    DWORD e=GetLastError();CertFreeCertificateContext(public);return ok?TRUE:fail(L"Trusting local signing certificate failed",e);
}
// Authenticode signing entry points are loaded from Windows' system directory.
typedef struct {DWORD size;LPCWSTR path;HANDLE file;} SignFile;
typedef struct {DWORD size;DWORD *index;DWORD choice;SignFile *file;} SignSubject;
typedef struct {DWORD size;PCCERT_CONTEXT certificate;DWORD policy;HCERTSTORE store;} SignStore;
typedef struct {DWORD size,choice;SignStore *store;HWND window;} SignCert;
typedef struct {DWORD size,hash,choice;void *attributes,*authenticated,*unauthenticated;} SignSignature;
typedef HRESULT (WINAPI *SignEx)(DWORD,SignSubject*,SignCert*,SignSignature*,void*,LPCWSTR,void*,void*,void**);
typedef HRESULT (WINAPI *FreeSign)(void*);
static BOOL sign_catalog(const wchar_t *path,PCCERT_CONTEXT cert) {
    HMODULE dll=LoadLibraryExW(L"mssign32.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!dll) return fail(L"Loading Windows catalog signer failed",GetLastError());
    SignEx sign=(SignEx)GetProcAddress(dll,"SignerSignEx");FreeSign release=(FreeSign)GetProcAddress(dll,"SignerFreeSignerContext");
    DWORD index=0;SignFile file={sizeof(file),path,NULL};SignSubject subject={sizeof(subject),&index,1,&file};SignStore store={sizeof(store),cert,2,NULL};SignCert certificate={sizeof(certificate),2,&store,NULL};SignSignature signature={sizeof(signature),CALG_SHA_256,0,NULL,NULL,NULL};void *context=NULL;
    HRESULT hr=sign&&release?sign(0,&subject,&certificate,&signature,NULL,NULL,NULL,NULL,&context):E_NOTIMPL;
    if(context&&release) release(context);FreeLibrary(dll);return SUCCEEDED(hr)?TRUE:fail(L"Signing driver catalog failed",(DWORD)hr);
}
static BOOL verify_catalog(const wchar_t *path) {
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;WINTRUST_FILE_INFO file={sizeof(file),path,NULL,NULL};WINTRUST_DATA data={0};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    LONG result=WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,&action,&data);data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,&action,&data);
    return result==ERROR_SUCCESS?TRUE:fail(L"Verifying signed catalog failed",(DWORD)result);
}
static BOOL install_driver(const wchar_t *port) {
    BOOL allowed=FALSE;if(!load_ports()) return FALSE;for(unsigned i=0;i<portcount;i++) if(!_wcsicmp(port,portnames[i])) allowed=TRUE;
#ifdef SETUP_INTEGRATION_TEST
    allowed=!wcscmp(port,L"FILE:");
#endif
    if(!allowed) return fail(L"Select an existing USB printer port",ERROR_INVALID_PARAMETER);
    wchar_t *path=calloc(PATHLEN,sizeof(wchar_t)),*driverdir=calloc(PATHLEN,sizeof(wchar_t)),*published=calloc(PATHLEN,sizeof(wchar_t));
    if(!path||!driverdir||!published) {free(path);free(driverdir);free(published);return fail(L"Out of memory",ERROR_OUTOFMEMORY);}
    BOOL ok=FALSE,found=FALSE,queue_exists=FALSE;PCCERT_CONTEXT cert=NULL;DWORD count=0;PRINTER_INFO_2W *p=printers(&count);
    if(count==MAXDWORD) goto done;
    for(DWORD i=0;i<count;i++) {
        if(!wcscmp(p[i].pPrinterName,DRIVER)) {queue_exists=TRUE;if(wcscmp(p[i].pDriverName,DRIVER)||wcscmp(p[i].pPortName,port)) {fail(L"Printer already exists on another port or driver; uninstall it first",ERROR_ALREADY_EXISTS);goto done;}}
        if(!wcscmp(p[i].pDriverName,DRIVER)&&p[i].cJobs) {fail(L"Finish or cancel print jobs before updating",ERROR_BUSY);goto done;}
    }
    for(unsigned i=0;i<COUNT(payload);i++) {join(path,source,payload[i]);if(!exists(path)) {fail(L"Extract the complete setup ZIP beside PL80ESetup.exe",ERROR_FILE_NOT_FOUND);goto done;}}
    if(!protected_dir(root)||!read_state(&found)) goto done;
    if(!found) {state.owns=TRUE;if(!existing_inf(state.inf)) {fail(L"Reading existing driver failed",ERROR_INVALID_DATA);goto done;}for(DWORD i=0;i<count;i++) if(!wcscmp(p[i].pDriverName,DRIVER)&&!add_item(state.queues,&state.nq,p[i].pPrinterName)) goto done;}
    if(!found||!state.owns) {
        cert=create_cert();if(!cert) goto done;state.owns=TRUE;BYTE hash[20];DWORD size=sizeof(hash);
        if(!CertGetCertificateContextProperty(cert,CERT_SHA1_HASH_PROP_ID,hash,&size)) {fail(L"Reading signing identity failed",GetLastError());goto done;}
        for(unsigned i=0;i<20;i++) StringCchPrintfW(state.thumb+i*2,3,L"%02X",hash[i]);
    } else cert=find_cert(L"MY",state.thumb);
    if(!cert) {fail(L"Recorded signing certificate is missing; recover the prior installation first",ERROR_NOT_FOUND);goto done;}
    if(!validate_cert(cert)) goto done;
    FILETIME now;GetSystemTimeAsFileTime(&now);ULARGE_INTEGER expiry,current;expiry.LowPart=cert->pCertInfo->NotAfter.dwLowDateTime;expiry.HighPart=cert->pCertInfo->NotAfter.dwHighDateTime;current.LowPart=now.dwLowDateTime;current.HighPart=now.dwHighDateTime;
    if(expiry.QuadPart<current.QuadPart+30ULL*24*60*60*10000000) {fail(L"Signing certificate expires soon; uninstall before replacing it",ERROR_INVALID_TIME);goto done;}
    if(!add_item(state.queues,&state.nq,DRIVER)||!remember_inf(state.inf)||!save_state()) goto done;
    join(driverdir,root,L"Driver");if(!protected_dir(driverdir)) goto done;
    for(unsigned i=0;i<COUNT(payload);i++) {join(path,driverdir,payload[i]);if(exists(path)&&!DeleteFileW(path)) {fail(L"Replacing payload failed",GetLastError());goto done;}join(published,source,payload[i]);if(!CopyFileW(published,path,TRUE)) {fail(L"Copying driver payload failed",GetLastError());goto done;}}
    logline(L"Signing the driver catalog with this PC's local certificate...");join(path,driverdir,payload[3]);
    if(!sign_catalog(path,cert)||!trust_cert(cert)||!verify_catalog(path)) goto done;
    logline(L"Staging the signed driver package...");join(path,driverdir,payload[2]);DWORD required=0;
    if(!SetupCopyOEMInfW(path,driverdir,SPOST_PATH,0,published,PATHLEN,&required,NULL)) {fail(L"Windows rejected the signed driver package",GetLastError());goto done;}
    wchar_t *leaf=wcsrchr(published,L'\\');leaf=leaf?leaf+1:published;
    if(!published_valid(leaf)||!add_item(state.packages,&state.np,leaf)||!save_state()) goto done;
    if(!SetupGetInfDriverStoreLocationW(published,NULL,NULL,state.inf,COUNT(state.inf),&required)||!save_state()) {if(!*error) fail(L"Resolving Driver Store location failed",GetLastError());goto done;}
    logline(L"Registering PL80E Open Source Driver...");
    typedef HRESULT (WINAPI *InstallPackage)(LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,DWORD);
    HMODULE spool=LoadLibraryExW(L"winspool.drv",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);InstallPackage install=spool?(InstallPackage)GetProcAddress(spool,"InstallPrinterDriverFromPackageW"):NULL;
    HRESULT hr=install?install(NULL,state.inf,DRIVER,L"Windows x64",1):E_NOTIMPL;if(spool) FreeLibrary(spool);
    if(FAILED(hr)) {fail(L"Registering printer driver failed",(DWORD)hr);goto done;}
    if(!queue_exists) {PRINTER_INFO_2W info={0};info.pPrinterName=DRIVER;info.pPortName=(LPWSTR)port;info.pDriverName=DRIVER;info.pPrintProcessor=L"winprint";info.pDatatype=L"RAW";info.Priority=1;info.DefaultPriority=1;info.Attributes=PRINTER_ATTRIBUTE_LOCAL;HANDLE printer=AddPrinterW(NULL,2,(BYTE*)&info);if(!printer) {fail(L"Creating printer queue failed",GetLastError());goto done;}ClosePrinter(printer);}
    ok=save_state();if(ok) {SYSTEMTIME end;FileTimeToSystemTime(&cert->pCertInfo->NotAfter,&end);wchar_t message[256];StringCchPrintfW(message,COUNT(message),L"Installed on %ls. Certificate expires %04u-%02u-%02u.",port,end.wYear,end.wMonth,end.wDay);logline(message);}
done:
    if(cert) CertFreeCertificateContext(cert);free(p);free(path);free(driverdir);free(published);return ok;
}
static BOOL queue_owned(const wchar_t *name) {for(unsigned i=0;i<state.nq;i++) if(!_wcsicmp(name,state.queues[i])) return TRUE;return FALSE;}
static BOOL remove_queue(const wchar_t *name) {
    HANDLE h;PRINTER_DEFAULTSW defaults={NULL,NULL,DELETE|PRINTER_ACCESS_ADMINISTER|PRINTER_ACCESS_USE};CHECK(OpenPrinterW((LPWSTR)name,&h,&defaults),L"Opening queue for removal failed");
    DWORD bytes=0;GetPrinterW(h,2,NULL,0,&bytes);PRINTER_INFO_2W *info=calloc(bytes?bytes:1,1);
    BOOL ok=info && GetPrinterW(h,2,(BYTE*)info,bytes,&bytes) && !wcscmp(info->pDriverName,DRIVER) && !info->cJobs;
    if(ok) ok=DeletePrinter(h);DWORD e=GetLastError();free(info);ClosePrinter(h);return ok?TRUE:fail(L"Queue changed or still has print jobs; retry after closing print applications",e?e:ERROR_BUSY);
}
static BOOL delete_package(const wchar_t *name) {
    if(!published_valid(name)) return fail(L"Unexpected Driver Store package",ERROR_INVALID_DATA);
    wchar_t *path=calloc(PATHLEN,sizeof(wchar_t));if(!path) return fail(L"Out of memory",ERROR_OUTOFMEMORY);
    GetWindowsDirectoryW(path,PATHLEN);StringCchCatW(path,PATHLEN,L"\\INF\\");StringCchCatW(path,PATHLEN,name);
    if(!exists(path)) {free(path);return TRUE;}wchar_t *text=read_text(path);wchar_t match[ENTRYLEN];StringCchPrintfW(match,COUNT(match),L"\"%ls\"=",DRIVER);
    BOOL valid=text&&wcsstr(text,match);free(text);if(!valid) {free(path);return fail(L"Recorded INF no longer identifies this driver; certificate trust retained",ERROR_INVALID_DATA);}
    GetSystemDirectoryW(path,PATHLEN);StringCchCatW(path,PATHLEN,L"\\pnputil.exe");wchar_t command[1024];StringCchPrintfW(command,COUNT(command),L"\"%ls\" /delete-driver %ls",path,name);
    STARTUPINFOW start={0};start.cb=sizeof(start);PROCESS_INFORMATION process={0};BOOL ok=CreateProcessW(path,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&start,&process);DWORD e=GetLastError();free(path);
    if(!ok) return fail(L"Starting Driver Store cleanup failed",e);
    WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);CloseHandle(process.hThread);
    return !code?TRUE:fail(L"Driver Store package is still in use; certificate trust retained. Close printing applications and retry",code);
}
static BOOL uninstall_driver(void) {
    BOOL found;CHECK(protected_dir(root),L"Protecting installation directory failed");if(!read_state(&found)) return FALSE;
    DWORD count=0;PRINTER_INFO_2W *p=printers(&count);BOOL ok=FALSE;PCCERT_CONTEXT cert=NULL;
    if(count==MAXDWORD) goto done;
    if(!found) {if(!driver_installed()) {fail(L"No driver installation was found",ERROR_NOT_FOUND);goto done;}state.owns=FALSE;existing_inf(state.inf);for(DWORD i=0;i<count;i++) if(!wcscmp(p[i].pDriverName,DRIVER)&&!add_item(state.queues,&state.nq,p[i].pPrinterName)) goto done;logline(L"Existing signing certificates are not owned by this setup and will be retained.");}
    for(DWORD i=0;i<count;i++) {
        if(!wcscmp(p[i].pDriverName,DRIVER)&&!queue_owned(p[i].pPrinterName)) {fail(L"Other queues still use this driver; remove or migrate them first",ERROR_BUSY);goto done;}
        if(queue_owned(p[i].pPrinterName)&&(wcscmp(p[i].pDriverName,DRIVER)||p[i].cJobs)) {fail(L"A managed queue changed driver or contains print jobs; nothing removed",ERROR_BUSY);goto done;}
    }
    const wchar_t *stores[]={L"MY",L"ROOT",L"TrustedPublisher"};
    if(state.owns) for(unsigned i=0;i<COUNT(stores);i++) {PCCERT_CONTEXT c=find_cert(stores[i],state.thumb);if(c) {BOOL valid=validate_cert(c);if(i==0) cert=c;else CertFreeCertificateContext(c);if(!valid) goto done;}}
    if(!remember_inf(state.inf)||!save_state()) goto done;
    for(DWORD i=0;i<count;i++) if(queue_owned(p[i].pPrinterName)&&!remove_queue(p[i].pPrinterName)) goto done;
    if(driver_installed()&&!DeletePrinterDriverExW(NULL,L"Windows x64",DRIVER,DPD_DELETE_UNUSED_FILES,0)) {fail(L"Driver is still in use; close printing applications and retry",GetLastError());goto done;}
    for(unsigned i=0;i<state.np;i++) if(!delete_package(state.packages[i])) goto done;
    // Delete the private key only after Windows has released all packages.
    if(cert) {HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key=0;DWORD spec=0;BOOL caller=FALSE;
        if(!CryptAcquireCertificatePrivateKey(cert,CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG|CRYPT_ACQUIRE_SILENT_FLAG,NULL,&key,&spec,&caller)) {
            DWORD e=GetLastError();if(e!=(DWORD)NTE_BAD_KEYSET && e!=(DWORD)NTE_NOT_FOUND) {fail(L"Opening managed private key failed; trust retained",e);goto done;}
        } else {SECURITY_STATUS e=NCryptDeleteKey(key,0);if(e) {if(caller) NCryptFreeObject(key);fail(L"Removing managed private key failed",(DWORD)e);goto done;}}
    }
    if(state.owns) for(unsigned i=0;i<COUNT(stores);i++) {PCCERT_CONTEXT c=find_cert(stores[i],state.thumb);if(c&&!CertDeleteCertificateFromStore(c)) {fail(L"Removing signing certificate failed",GetLastError());goto done;}}
    wchar_t *path=calloc(PATHLEN,sizeof(wchar_t)),*dir=calloc(PATHLEN,sizeof(wchar_t));
    if(!path||!dir) {free(path);free(dir);fail(L"Out of memory",ERROR_OUTOFMEMORY);goto done;}
    join(dir,root,L"Driver");for(unsigned i=0;i<COUNT(payload);i++) {join(path,dir,payload[i]);if(exists(path)&&!DeleteFileW(path)) {free(path);free(dir);fail(L"Removing driver payload failed",GetLastError());goto done;}}free(path);free(dir);
    if(!DeleteFileW(statepath)) {fail(L"Removing installation state failed",GetLastError());goto done;}
    logline(L"Driver, managed printer queues and signing certificate cleanup completed.");ok=TRUE;
done:if(cert) CertFreeCertificateContext(cert);free(p);return ok;
}
static void buttons(void) {
    EnableWindow(combo,!busy);EnableWindow(refreshbutton,!busy);EnableWindow(installbutton,!busy&&SendMessageW(combo,CB_GETCURSEL,0,0)!=CB_ERR);EnableWindow(uninstallbutton,!busy&&(installed||exists(statepath)));
}
static void refresh(void) {
    wchar_t preferred[ENTRYLEN]=L"",message[1024]=L"Driver: not installed.";LRESULT selection=SendMessageW(combo,CB_GETCURSEL,0,0);
    if(selection!=CB_ERR) {LRESULT index=SendMessageW(combo,CB_GETITEMDATA,selection,0);if(index>=0&&(unsigned)index<portcount) StringCchCopyW(preferred,COUNT(preferred),portnames[index]);}
    DWORD n=0;PRINTER_INFO_2W *p=printers(&n);installed=driver_installed();
    if(n==MAXDWORD) {logline(error);return;}
    for(DWORD i=0;i<n;i++) if(!wcscmp(p[i].pDriverName,DRIVER)) {StringCchPrintfW(message,COUNT(message),L"Driver: installed on %ls.",p[i].pPortName);if(!*preferred) StringCchCopyW(preferred,COUNT(preferred),p[i].pPortName);}
    if(!*preferred) for(DWORD i=0;i<n;i++) if(!wcscmp(p[i].pPrinterName,L"PL80E")) {StringCchCopyW(preferred,COUNT(preferred),p[i].pPortName);break;}
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    if(!load_ports()) logline(error);
    for(unsigned i=0;i<portcount;i++) {
        BOOL here=FALSE;for(DWORD j=0;j<n;j++) if(!wcscmp(p[j].pDriverName,DRIVER)&&!_wcsicmp(p[j].pPortName,portnames[i])) here=TRUE;
        wchar_t display[ENTRYLEN+80];StringCchPrintfW(display,COUNT(display),L"%ls%ls",portnames[i],here?L" — PL80E Open Source Driver installed":L"");
        LRESULT item=SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)display);SendMessageW(combo,CB_SETITEMDATA,item,i);if(!_wcsicmp(preferred,portnames[i])) SendMessageW(combo,CB_SETCURSEL,item,0);
    }
    if(SendMessageW(combo,CB_GETCURSEL,0,0)==CB_ERR&&portcount) SendMessageW(combo,CB_SETCURSEL,0,0);
    if(!portcount) StringCchCatW(message,COUNT(message),L" No USB printer ports found. Connect the printer and refresh.");
    SetWindowTextW(statuslabel,message);free(p);buttons();
}
typedef struct {BOOL remove;wchar_t port[ENTRYLEN];} Operation;
static DWORD WINAPI worker(void *arg) {Operation *op=arg;error[0]=0;BOOL ok=op->remove?uninstall_driver():install_driver(op->port);free(op);if(!ok) logline(error);else logline(L"Completed successfully.");PostMessageW(window,WM_DONE,ok,0);return 0;}
static HWND control(const wchar_t *type,const wchar_t *text,DWORD style,int id) {HWND c=CreateWindowExW(!wcscmp(type,L"EDIT")?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;}
static HWND heading,instruction,notice,versionlabel;
static void layout(void) {RECT r;GetClientRect(window,&r);int w=r.right,h=r.bottom;MoveWindow(heading,20,16,w-40,36,TRUE);MoveWindow(instruction,20,60,w-40,24,TRUE);MoveWindow(combo,20,92,w-180,300,TRUE);MoveWindow(refreshbutton,w-150,91,130,30,TRUE);MoveWindow(statuslabel,20,134,w-40,46,TRUE);MoveWindow(notice,20,184,w-40,48,TRUE);MoveWindow(installbutton,20,244,160,34,TRUE);MoveWindow(uninstallbutton,192,244,130,34,TRUE);MoveWindow(versionlabel,20,292,w-40,24,TRUE);MoveWindow(output,20,324,w-40,h-344,TRUE);}
static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
    switch(m) {
    case WM_CREATE:
        window=h;font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");titlefont=CreateFontW(-24,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        heading=control(L"STATIC",L"PL80E Open Source Driver",0,0);SendMessageW(heading,WM_SETFONT,(WPARAM)titlefont,TRUE);
        instruction=control(L"STATIC",L"Connect and power on your printer, then select its USB port.",0,0);
        combo=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_SORT|WS_VSCROLL|WS_TABSTOP,100);
        refreshbutton=control(L"BUTTON",L"Refresh ports",WS_TABSTOP,101);statuslabel=control(L"STATIC",L"",0,0);
        notice=control(L"STATIC",L"Installation creates and trusts a local code-signing certificate on this PC.\r\nUninstall removes the driver and its managed signing certificate.",0,0);
        installbutton=control(L"BUTTON",L"Install / update",WS_TABSTOP|BS_DEFPUSHBUTTON,102);uninstallbutton=control(L"BUTTON",L"Uninstall",WS_TABSTOP,103);
        versionlabel=control(L"STATIC",L"Setup " WIDEN(CLEAN_VERSION_TEXT),0,0);output=control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,104);
        layout();refresh();return 0;
    case WM_SIZE:layout();return 0;
    case WM_GETMINMAXINFO:((MINMAXINFO*)l)->ptMinTrackSize=(POINT){720,540};return 0;
    case WM_COMMAND:
        if(LOWORD(w)==100) {buttons();return 0;}
        if(busy) return 0;
        if(LOWORD(w)==101) {refresh();return 0;}
        if(LOWORD(w)==102||LOWORD(w)==103) {
            BOOL remove=LOWORD(w)==103;if(remove&&MessageBoxW(h,L"Remove PL80E Open Source Driver, its printer queues and managed signing certificate?",L"Uninstall",MB_YESNO|MB_ICONQUESTION)!=IDYES) return 0;
            Operation *op=calloc(1,sizeof(*op));if(!op) return 0;op->remove=remove;
            if(!remove) {LRESULT selected=SendMessageW(combo,CB_GETCURSEL,0,0);if(selected==CB_ERR) {free(op);return 0;}LRESULT index=SendMessageW(combo,CB_GETITEMDATA,selected,0);if(index<0||(unsigned)index>=portcount) {free(op);return 0;}StringCchCopyW(op->port,COUNT(op->port),portnames[index]);}
            busy=TRUE;buttons();HANDLE thread=CreateThread(NULL,0,worker,op,0,NULL);if(thread) CloseHandle(thread);else {busy=FALSE;free(op);buttons();}return 0;
        }break;
    case WM_LOG: {wchar_t *text=(wchar_t*)l;SendMessageW(output,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);SendMessageW(output,EM_REPLACESEL,FALSE,(LPARAM)text);SendMessageW(output,EM_REPLACESEL,FALSE,(LPARAM)L"\r\n");free(text);return 0;}
    case WM_DONE:busy=FALSE;if(!w) MessageBoxW(h,error,L"Setup could not complete",MB_OK|MB_ICONERROR);refresh();return 0;
    case WM_CLOSE:if(!busy) DestroyWindow(h);return 0;
    case WM_DESTROY:DeleteObject(font);DeleteObject(titlefont);PostQuitMessage(0);return 0;
    }return DefWindowProcW(h,m,w,l);
}
static BOOL initialize(void) {
    wchar_t common[PATHLEN];if(!SUCCEEDED(SHGetFolderPathW(NULL,CSIDL_COMMON_APPDATA,NULL,SHGFP_TYPE_CURRENT,common))) return fail(L"ProgramData is unavailable",GetLastError());
    join(root,common,DRIVER);join(statepath,root,L"installation.json");GetModuleFileNameW(NULL,source,COUNT(source));wchar_t *leaf=wcsrchr(source,L'\\');if(!leaf) return FALSE;*leaf=0;return TRUE;
}
static BOOL preview_image(const wchar_t *report) {
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW cls={0};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(NULL);cls.hCursor=LoadCursorW(NULL,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);cls.lpszClassName=L"PL80E.Setup.Preview";RegisterClassW(&cls);
    HWND h=CreateWindowExW(0,cls.lpszClassName,L"PL80E Open Source Driver Setup",WS_OVERLAPPEDWINDOW,0,0,760,590,NULL,NULL,cls.hInstance,NULL);
    if(!h) return FALSE;
    ShowWindow(h,SW_SHOWNOACTIVATE);UpdateWindow(h);
    RECT r;GetWindowRect(h,&r);LONG width=r.right-r.left,height=r.bottom-r.top;
    BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    DWORD pixels=(DWORD)(width*height*4);BYTE *bits=NULL;HDC screen=GetDC(h),memory=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,(void**)&bits,NULL,0);HGDIOBJ old=bitmap?SelectObject(memory,bitmap):NULL;
    BOOL ok=bitmap&&PrintWindow(h,memory,0);
    if(ok) {
        BITMAPFILEHEADER header={0};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+pixels;
        BYTE *data=calloc(header.bfSize,1);if(!data) ok=FALSE;
        else {memcpy(data,&header,sizeof(header));memcpy(data+sizeof(header),&info.bmiHeader,sizeof(BITMAPINFOHEADER));memcpy(data+header.bfOffBits,bits,pixels);wchar_t path[PATHLEN];StringCchPrintfW(path,COUNT(path),L"%ls.bmp",report);DeleteFileW(path);ok=write_bytes(path,data,header.bfSize);free(data);}
    }
    if(old) SelectObject(memory,old);if(bitmap) DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(h,screen);DestroyWindow(h);window=NULL;return ok;
}
static int self_test(const wchar_t *report) {
    BOOL ok=usb_name(L"USB001")&&!usb_name(L"USB")&&!usb_name(L"USB001x")&&published_valid(L"oem123.inf")&&!published_valid(L"..\\oem1.inf")&&load_ports();
    State *sample=calloc(1,sizeof(*sample));wchar_t json[1024];
    StringCchPrintfW(json,COUNT(json),L"{\"DriverName\":\"%ls\",\"Thumbprint\":\"0123456789012345678901234567890123456789\",\"Queues\":[\"test\"],\"InfPath\":null}",DRIVER);
    ok=ok&&sample&&parse_state(json,sample)&&sample->owns&&sample->nq==1&&sample->np==0;
    if(sample) {
        StringCchPrintfW(json,COUNT(json),L"{\"DriverName\":\"%ls\",\"Thumbprint\":\"\",\"Queues\":[],\"OwnsCertificate\":false,\"Packages\":[\"..\\\\bad.inf\"]}",DRIVER);ok=ok&&!parse_state(json,sample);
        StringCchPrintfW(json,COUNT(json),L"{\"DriverName\":\"%ls\",\"Thumbprint\":\"\",\"Queues\":[],\"OwnsCertificate\":false}",DRIVER);ok=ok&&parse_state(json,sample)&&!sample->owns;
        ok=ok&&!parse_state(L"{\"DriverName\":\"wrong\",\"Thumbprint\":\"\",\"Queues\":[]}",sample);
        ok=ok&&!parse_state(L"{\"DriverName\":\"\\u",sample);free(sample);
    }
    BOOL found=FALSE;ok=ok&&read_state(&found)&&preview_image(report);
    DeleteFileW(report);const char *result=ok?"PASS: native USB enumeration, port validation, legacy and installed JSON state, malformed state rejection and GUI rendering.\r\n":"FAIL\r\n";write_bytes(report,result,(DWORD)strlen(result));return ok?0:1;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR command,int show) {
    (void)previous;(void)command;if(!initialize()) {MessageBoxW(NULL,error,L"Setup",MB_OK|MB_ICONERROR);return 1;}
    int argc=0;LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc==3&&!wcscmp(argv[1],L"--self-test")) {int result=self_test(argv[2]);LocalFree(argv);return result;}
#ifdef SETUP_INTEGRATION_TEST
    if(argc==4&&!wcscmp(argv[1],L"--integration-test")) {
        StringCchCopyW(source,COUNT(source),argv[2]);BOOL ok=install_driver(L"FILE:");wchar_t thumb[41];StringCchCopyW(thumb,COUNT(thumb),state.thumb);
        if(ok) ok=driver_installed()&&install_driver(L"FILE:")&&!wcscmp(thumb,state.thumb)&&uninstall_driver()&&!driver_installed()&&!exists(statepath);
        if(ok) {const wchar_t *stores[]={L"MY",L"ROOT",L"TrustedPublisher"};for(unsigned i=0;i<COUNT(stores);i++) {PCCERT_CONTEXT c=find_cert(stores[i],thumb);if(c) {ok=FALSE;CertFreeCertificateContext(c);}}}
        // Simulate an old installation without installer state. Its unowned
        // signing identity must survive removal; restore state only for cleanup.
        if(ok) {
            ok=install_driver(L"FILE:");wchar_t *saved=ok?read_text(statepath):NULL;StringCchCopyW(thumb,COUNT(thumb),state.thumb);
            if(ok) ok=saved&&DeleteFileW(statepath)&&uninstall_driver();
            if(ok) {PCCERT_CONTEXT c=find_cert(L"MY",thumb);ok=c!=NULL;if(c) CertFreeCertificateContext(c);else fail(L"Unowned signing identity was removed",ERROR_INVALID_DATA);}
            if(saved) {
                BOOL retained=ok;BOOL parsed=parse_state(saved,&state);free(saved);
                if(parsed) {BOOL cleaned=save_state()&&uninstall_driver();ok=retained&&cleaned;}
                else ok=FALSE;
            }
        }
        DeleteFileW(argv[3]);if(ok) {const char *result="PASS: native C certificate creation, signing, FILE installation, update, uninstall, legacy state and unowned certificate preservation.\r\n";write_bytes(argv[3],result,(DWORD)strlen(result));}else {if(!*error) fail(L"Integration assertion failed",ERROR_INVALID_DATA);int bytes=WideCharToMultiByte(CP_UTF8,0,error,-1,NULL,0,NULL,NULL);char *text=calloc(bytes+1,1);WideCharToMultiByte(CP_UTF8,0,error,-1,text,bytes,NULL,NULL);write_bytes(argv[3],text,(DWORD)strlen(text));free(text);}LocalFree(argv);return ok?0:1;
    }
#endif
    LocalFree(argv);HANDLE mutex=CreateMutexW(NULL,TRUE,L"Global\\PL80E-Open-Source-Setup");if(!mutex||GetLastError()==ERROR_ALREADY_EXISTS) {MessageBoxW(NULL,L"Setup is already running.",L"Setup",MB_OK);if(mutex) CloseHandle(mutex);return 1;}
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW cls={0};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.hCursor=LoadCursorW(NULL,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);cls.lpszClassName=L"PL80E.Native.Setup";RegisterClassW(&cls);
    HWND h=CreateWindowExW(0,cls.lpszClassName,L"PL80E Open Source Driver Setup",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,760,590,NULL,NULL,instance,NULL);if(!h) {CloseHandle(mutex);return 1;}ShowWindow(h,show);UpdateWindow(h);MSG message;while(GetMessageW(&message,NULL,0,0)>0) if(!IsDialogMessageW(h,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}CloseHandle(mutex);return 0;
}
