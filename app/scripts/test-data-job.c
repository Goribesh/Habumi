/* SPDX-License-Identifier: GPL-2.0-only */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#include "../release/data-job.h"
static void print_utf8(const wchar_t *text)
{
    char out[32768];
    int bytes=WideCharToMultiByte(CP_UTF8,0,text,-1,out,sizeof(out),NULL,NULL);
    if(bytes>0) puts(out);
}
int wmain(int argc,wchar_t **argv)
{
    if(argc==1){
        const wchar_t *inputs[]={L"plain",L"C:\\space folder\\",L"a\"b",L"a&b$`'",L"C:\\unicode \x00e8\\file"};
        for(unsigned i=0;i<sizeof(inputs)/sizeof(inputs[0]);++i){
            wchar_t quoted[512],command[1024];int count=0;
            if(!data_quote(quoted,512,inputs[i]))return 10;
            swprintf(command,1024,L"program.exe %ls",quoted);
            wchar_t **parsed=CommandLineToArgvW(command,&count);
            if(!parsed||count!=2||wcscmp(parsed[1],inputs[i]))return 11;
            LocalFree(parsed);
        }
        wchar_t small[4]=L"xxx";
        if(data_quote(small,4,L"long value")||small[0])return 12;
        puts("PASS: Windows argument quoting round-trip and truncation");return 0;
    }
    if(argc!=6){printf("Expected 6 arguments, got %d\n",argc);return 2;}
    DataJob job={0};
    if(!data_job_start(&job,argv[1],argv[2],argv[3],argv[4],_wtoi(argv[5]))){printf("Worker start error %lu\n",GetLastError());print_utf8(job.message);print_utf8(job.directory);return 3;}
    DWORD start=GetTickCount();int result;
    do {result=data_job_poll(&job);if(!result)Sleep(25);}while(!result&&GetTickCount()-start<60000);
    print_utf8(job.directory);print_utf8(job.message);
    if(!result){puts("FAIL: worker exceeded test deadline; retained for inspection");return 4;}
    return result==1?0:1;
}
