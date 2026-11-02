/* SPDX-License-Identifier: GPL-2.0-only */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "config-save.h"
static int checks,failed;
#define CHECK(x) do{checks++;if(!(x)){failed++;printf("FAIL: line %d\n",__LINE__);}}while(0)
static void put(const char *text){FILE *f=fopen("config.txt","wb");if(f){fwrite(text,1,strlen(text),f);fclose(f);}}
static int same(const char *text){char buffer[1024]={0};FILE*f=fopen("config.txt","rb");if(!f)return 0;size_t n=fread(buffer,1,sizeof(buffer)-1,f);fclose(f);return n==strlen(text)&&!memcmp(buffer,text,n);}
int main(int argc,char **argv)
{
    char error[256];ConfigChange change={"memory","4096"};
    if(argc!=2 || !SetCurrentDirectoryA(argv[1]))return 2;
    const char *original="# comment\r\nmemory = 2048\r\nunknown = preserve me\r\n";
    put(original);
    CHECK(config_save("config.txt",&change,1,error,sizeof(error)));
    CHECK(same("# comment\r\nmemory = 4096\r\nunknown = preserve me\r\n"));
    ConfigChange multiple[]={{"memory","6144"},{"width","800"},{"height","1280"}};
    CHECK(config_save("config.txt",multiple,3,error,sizeof(error)));
    CHECK(same("# comment\r\nmemory = 6144\r\nunknown = preserve me\r\nwidth=800\nheight=1280\n"));
    put(original);
    HANDLE held=CreateFileA("config.txt.write.lock",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,0,NULL);
    CHECK(held!=INVALID_HANDLE_VALUE);
    CHECK(!config_save("config.txt",&change,1,error,sizeof(error)));
    CHECK(same(original));if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
    held=CreateFileA("config.txt",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    CHECK(held!=INVALID_HANDLE_VALUE);
    CHECK(!config_save("config.txt",&change,1,error,sizeof(error)));
    CHECK(same(original));if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
    ConfigChange invalid={"memory","4096\nwidth=999"};
    CHECK(!config_save("config.txt",&invalid,1,error,sizeof(error)));
    CHECK(same(original));CHECK(error[0]!=0);
    ConfigChange unknown={"private_unknown","overwrite"};
    CHECK(!config_save("config.txt",&unknown,1,error,sizeof(error)));CHECK(same(original));
    CHECK(SetFileAttributesA("config.txt",FILE_ATTRIBUTE_READONLY));
    CHECK(!config_save("config.txt",&change,1,error,sizeof(error)));CHECK(same(original));
    SetFileAttributesA("config.txt",FILE_ATTRIBUTE_NORMAL);
    FILE*f=fopen("config.txt","wb");if(!f)return 3;for(int i=0;i<65536;i++)fputc('x',f);fclose(f);
    CHECK(!config_save("config.txt",&change,1,error,sizeof(error)));
    WIN32_FILE_ATTRIBUTE_DATA info;CHECK(GetFileAttributesExA("config.txt",GetFileExInfoStandard,&info)&&info.nFileSizeLow==65536);
    CHECK(DeleteFileA("config.txt"));
    CHECK(config_save("config.txt",&change,1,error,sizeof(error)));CHECK(same("memory=4096\n"));
    /* A stale partial file is never used as the live configuration. */
    f=fopen("config.txt.new-stale","wb");if(f){fputs("partial",f);fclose(f);}
    CHECK(config_save("config.txt",multiple,3,error,sizeof(error)));
    CHECK(same("memory=6144\nwidth=800\nheight=1280\n"));
    printf("Atomic config: %d checks, %d failed\n",checks,failed);return failed?1:0;
}
