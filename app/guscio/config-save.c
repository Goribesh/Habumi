/* SPDX-License-Identifier: GPL-2.0-only */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "config-save.h"
#include "config-edit.h"
#define CONFIG_LIMIT 65536u

static bool plain(const char *path)
{
    char full[MAX_PATH];
    DWORD size=GetFullPathNameA(path,MAX_PATH,full,NULL);
    if(!size || size>=MAX_PATH)return false;
    for(;;){
        DWORD attributes=GetFileAttributesA(full);
        if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
        char *slash=strrchr(full,'\\');
        if(!slash || slash<=full+2)break;
        *slash=0;
    }
    return true;
}

bool config_save(const char *path,const ConfigChange *changes,size_t count,
                 char *error,size_t error_size)
{
    HANDLE guard=INVALID_HANDLE_VALUE,source=INVALID_HANDLE_VALUE,target=INVALID_HANDLE_VALUE;
    char lock[MAX_PATH],temporary[MAX_PATH]={0};
    char *first=NULL,*second=NULL;bool ok=false,existed=false;
    DWORD code=ERROR_INVALID_PARAMETER,read=0,written=0;
    static LONG sequence;
    if(error && error_size)error[0]=0;
    if(!path || !changes || !count || count>16 || strlen(path)>MAX_PATH-65 || !plain(path))goto done;
    snprintf(lock,sizeof(lock),"%s.write.lock",path);
    if(!plain(lock))goto done;
    guard=CreateFileA(lock,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(guard==INVALID_HANDLE_VALUE){code=GetLastError();goto done;}
    first=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,CONFIG_LIMIT);
    second=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,CONFIG_LIMIT);
    if(!first || !second){code=ERROR_NOT_ENOUGH_MEMORY;goto done;}
    source=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(source!=INVALID_HANDLE_VALUE){
        LARGE_INTEGER size;
        existed=true;
        if(!GetFileSizeEx(source,&size)){code=GetLastError();goto done;}
        if(size.QuadPart>=CONFIG_LIMIT){code=ERROR_FILE_TOO_LARGE;goto done;}
        if(!ReadFile(source,first,(DWORD)size.QuadPart,&read,NULL) || read!=(DWORD)size.QuadPart){code=ERROR_READ_FAULT;goto done;}
        if(memchr(first,0,read)){code=ERROR_INVALID_DATA;goto done;}
    }else if(GetLastError()!=ERROR_FILE_NOT_FOUND){code=GetLastError();goto done;}
    size_t length=read;
    for(size_t i=0;i<count;i++){
        length=config_edit(first,changes[i].key,changes[i].value,second,CONFIG_LIMIT);
        if(!length){code=ERROR_INVALID_DATA;goto done;}
        char *swap=first;first=second;second=swap;
    }
    snprintf(temporary,sizeof(temporary),"%s.new-%lu-%ld",path,GetCurrentProcessId(),InterlockedIncrement(&sequence));
    target=CreateFileA(temporary,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(target==INVALID_HANDLE_VALUE){temporary[0]=0;code=GetLastError();goto done;}
    if(!WriteFile(target,first,(DWORD)length,&written,NULL) || written!=length || !FlushFileBuffers(target)){
        code=GetLastError();if(!code)code=ERROR_WRITE_FAULT;goto done;
    }
    CloseHandle(target);target=INVALID_HANDLE_VALUE;
    /* The live file is never opened for truncation or writing. Release the
     * read handle before Windows replaces the path; the writer gate stays. */
    if(source!=INVALID_HANDLE_VALUE){CloseHandle(source);source=INVALID_HANDLE_VALUE;}
    if(!MoveFileExA(temporary,path,(existed?MOVEFILE_REPLACE_EXISTING:0)|MOVEFILE_WRITE_THROUGH)){
        code=GetLastError();goto done;
    }
    temporary[0]=0;ok=true;
done:
    if(target!=INVALID_HANDLE_VALUE)CloseHandle(target);
    if(source!=INVALID_HANDLE_VALUE)CloseHandle(source);
    if(temporary[0])DeleteFileA(temporary);
    if(first)HeapFree(GetProcessHeap(),0,first);
    if(second)HeapFree(GetProcessHeap(),0,second);
    if(guard!=INVALID_HANDLE_VALUE)CloseHandle(guard);
    if(!ok && error && error_size)snprintf(error,error_size,"Settings were not saved (Windows error %lu).",code);
    return ok;
}
