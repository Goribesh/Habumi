/* SPDX-License-Identifier: GPL-2.0-only */
/* Prova il downloader reale con WinHTTP finto e filesystem locale isolato. */
#define _WINHTTP_INTERNAL_
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "scarica.c"

static const unsigned char corpo[] = "Habumi download test payload";
static const char sha_corpo[] =
    "70fad64d70157462dd1267bbc0301f9e2732f2449ba5fe1a1fc8d174fee88956";
static size_t corpo_pos;
static int errore_lettura_dopo_primo_blocco;
static DWORD limite_blocco;
static int letture;

static int fallimenti;
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); fallimenti++; \
} } while (0)

WINBOOL WINAPI WinHttpCrackUrl(LPCWSTR url, DWORD n, DWORD flags,
                               LPURL_COMPONENTS c)
{
    (void)n; (void)flags;
    if (wcsncmp(url, L"https://", 8) != 0) return FALSE;
    wcscpy(c->lpszScheme, L"https"); c->dwSchemeLength = 5;
    wcscpy(c->lpszHostName, L"unit.test"); c->dwHostNameLength = 9;
    wcscpy(c->lpszUrlPath, L"/payload"); c->dwUrlPathLength = 8;
    c->lpszExtraInfo[0] = L'\0'; c->dwExtraInfoLength = 0;
    c->nScheme = INTERNET_SCHEME_HTTPS; c->nPort = INTERNET_DEFAULT_HTTPS_PORT;
    return TRUE;
}
HINTERNET WINAPI WinHttpOpen(LPCWSTR a, DWORD b, LPCWSTR c, LPCWSTR d, DWORD e)
{ (void)a;(void)b;(void)c;(void)d;(void)e; return (HINTERNET)1; }
WINBOOL WINAPI WinHttpSetTimeouts(HINTERNET h,int a,int b,int c,int d)
{ (void)h;(void)a;(void)b;(void)c;(void)d; return TRUE; }
HINTERNET WINAPI WinHttpConnect(HINTERNET h,LPCWSTR host,INTERNET_PORT p,DWORD f)
{ (void)h;(void)host;(void)p;(void)f; return (HINTERNET)2; }
HINTERNET WINAPI WinHttpOpenRequest(HINTERNET h,LPCWSTR a,LPCWSTR b,LPCWSTR c,
                                    LPCWSTR d,LPCWSTR *e,DWORD f)
{ (void)h;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f; corpo_pos=0;letture=0;return (HINTERNET)3; }
WINBOOL WINAPI WinHttpSetOption(HINTERNET h,DWORD o,LPVOID v,DWORD n)
{ (void)h;(void)o;(void)v;(void)n; return TRUE; }
WINBOOL WINAPI WinHttpAddRequestHeaders(HINTERNET h,LPCWSTR s,DWORD n,DWORD f)
{ (void)h;(void)s;(void)n;(void)f; return TRUE; }
WINBOOL WINAPI WinHttpSendRequest(HINTERNET h,LPCWSTR a,DWORD b,LPVOID c,
                                  DWORD d,DWORD e,DWORD_PTR f)
{ (void)h;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f; return TRUE; }
WINBOOL WINAPI WinHttpReceiveResponse(HINTERNET h,LPVOID r)
{ (void)h;(void)r; return TRUE; }
WINBOOL WINAPI WinHttpQueryHeaders(HINTERNET h,DWORD q,LPCWSTR n,LPVOID out,
                                   LPDWORD bytes,LPDWORD index)
{
    (void)h;(void)n;(void)index;
    if (q & WINHTTP_QUERY_FLAG_NUMBER) {
        *(DWORD *)out = 200; *bytes = sizeof(DWORD); return TRUE;
    }
    {
        wchar_t value[32];
        _snwprintf(value, 32, L"%u", (unsigned)(sizeof(corpo)-1));
        if (*bytes < (wcslen(value)+1)*sizeof(wchar_t)) return FALSE;
        memcpy(out,value,(wcslen(value)+1)*sizeof(wchar_t));
        *bytes=(DWORD)((wcslen(value)+1)*sizeof(wchar_t)); return TRUE;
    }
}
WINBOOL WINAPI WinHttpQueryDataAvailable(HINTERNET h,LPDWORD available)
{
    (void)h;
    if (errore_lettura_dopo_primo_blocco && letture > 0) return FALSE;
    *available=(DWORD)(sizeof(corpo)-1-corpo_pos);
    if(limite_blocco&&*available>limite_blocco)*available=limite_blocco;
    return TRUE;
}
WINBOOL WINAPI WinHttpReadData(HINTERNET h,LPVOID out,DWORD count,LPDWORD read)
{
    size_t n=(sizeof(corpo)-1-corpo_pos); (void)h;
    if(n>count)n=count;
    if(limite_blocco&&n>limite_blocco)n=limite_blocco;
    memcpy(out,corpo+corpo_pos,n);corpo_pos+=n;*read=(DWORD)n;letture++;return TRUE;
}
WINBOOL WINAPI WinHttpCloseHandle(HINTERNET h) { (void)h; return TRUE; }

static bool cancella(uint64_t fatto,uint64_t totale,void *dato)
{ (void)fatto;(void)totale;(void)dato; return false; }

static void esegui(const char *root)
{
    char dest[MAX_PATH], parziale[MAX_PATH], sha[65], errore[256];
    FILE *f;
    char file_sorgente[MAX_PATH];

    snprintf(file_sorgente,sizeof(file_sorgente),"%s\\test-payload.bin",root);
    f=fopen(file_sorgente,"wb"); CHECK(f!=NULL);
    if(f){fwrite(corpo,1,sizeof(corpo)-1,f);fclose(f);}
    CHECK(scarica_sha256_file(file_sorgente,sha,errore,sizeof(errore)));
    CHECK(strcmp(sha,sha_corpo)==0);
    snprintf(dest,sizeof(dest),"%s\\success.zip",root);
    CHECK(scarica_file("https://unit.test/payload",dest,sha,sizeof(corpo)-1,
                       NULL,NULL,errore,sizeof(errore)));
    if (errore[0]) fprintf(stderr, "Download success case: %s (win32=%lu)\n", errore, (unsigned long)GetLastError());
    CHECK(GetFileAttributesA(dest)!=INVALID_FILE_ATTRIBUTES);
    snprintf(parziale,sizeof(parziale),"%s.parziale",dest);
    CHECK(GetFileAttributesA(parziale)==INVALID_FILE_ATTRIBUTES);

    snprintf(dest,sizeof(dest),"%s\\bad-hash.zip",root);
    CHECK(!scarica_file("https://unit.test/payload",dest,
                        "0000000000000000000000000000000000000000000000000000000000000000",
                        sizeof(corpo)-1,NULL,NULL,errore,sizeof(errore)));
    snprintf(parziale,sizeof(parziale),"%s.parziale",dest);
    CHECK(GetFileAttributesA(parziale)==INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesA(dest)==INVALID_FILE_ATTRIBUTES);

    snprintf(dest,sizeof(dest),"%s\\read-error.zip",root);
    errore_lettura_dopo_primo_blocco=1;
    limite_blocco=8;
    CHECK(!scarica_file("https://unit.test/payload",dest,sha,sizeof(corpo)-1,
                        NULL,NULL,errore,sizeof(errore)));
    errore_lettura_dopo_primo_blocco=0;
    limite_blocco=0;
    CHECK(strstr(errore,"read interrupted")!=NULL);
    snprintf(parziale,sizeof(parziale),"%s.parziale",dest);
    CHECK(GetFileAttributesA(parziale)!=INVALID_FILE_ATTRIBUTES);
    {
        WIN32_FILE_ATTRIBUTE_DATA info;
        CHECK(GetFileAttributesExA(parziale,GetFileExInfoStandard,&info));
        CHECK(info.nFileSizeLow==8&&info.nFileSizeHigh==0);
    }
    DeleteFileA(parziale);

    snprintf(dest,sizeof(dest),"%s\\cancel.zip",root);
    CHECK(!scarica_file("https://unit.test/payload",dest,sha,sizeof(corpo)-1,
                        cancella,NULL,errore,sizeof(errore)));
    CHECK(strstr(errore,"cancelled")!=NULL);
    snprintf(parziale,sizeof(parziale),"%s.parziale",dest);
    CHECK(GetFileAttributesA(parziale)!=INVALID_FILE_ATTRIBUTES);
    DeleteFileA(parziale);
    CHECK(GetFileAttributesA(dest)==INVALID_FILE_ATTRIBUTES);
}

int main(void)
{
    char root[MAX_PATH];
    snprintf(root,sizeof(root),"download-test-%lu",
             (unsigned long)GetCurrentProcessId());
    if(!CreateDirectoryA(root,NULL)){
        fprintf(stderr,"FAIL: refuse existing or unavailable test directory (%lu)\n",
                (unsigned long)GetLastError());
        return 2;
    }
    esegui(root);
    /* Rimuove solo i nomi creati da questa esecuzione; non enumera la cartella. */
    {
        static const char *nomi[] = {
            "test-payload.bin", "success.zip", "success.zip.parziale",
            "bad-hash.zip", "bad-hash.zip.parziale", "read-error.zip",
            "read-error.zip.parziale", "cancel.zip", "cancel.zip.parziale"
        };
        size_t i;
        for(i=0;i<sizeof(nomi)/sizeof(nomi[0]);i++){
            char path[MAX_PATH];
            snprintf(path,sizeof(path),"%s\\%s",root,nomi[i]);
            DeleteFileA(path);
        }
    }
    RemoveDirectoryA(root);
    printf("%s: valid hash, wrong-hash cleanup, interrupted network read, and cancellation %s\n",
           fallimenti?"FAIL":"PASS",fallimenti?"failed":"passed");
    return fallimenti?1:0;
}
