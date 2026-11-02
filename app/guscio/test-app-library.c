/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app-library.h"
#include "app-target.h"
static int starts,closes;
static char tail[1024];
struct AdbCliente {int unused;};
static struct AdbCliente client;
AdbComando *adb_comando_avvia(const Config *c,const char *command){(void)c;starts++;strcpy(tail,command);return &client;}
AdbSpegniStato adb_comando_passo(AdbComando *c,char *out,int max,bool *cut){(void)c;snprintf(out,max,"Status: ok");*cut=false;return ADB_SPEGNI_OK;}
bool adb_comando_chiudi(AdbComando *c){(void)c;closes++;return true;}
bool adb_esegui(const Config *c,const char *s,char *out,int max){(void)c;(void)s;(void)out;(void)max;return false;}
bool adb_esegui_grezzo(const Config *c,const char *s,char *out,int max){return adb_esegui(c,s,out,max);}
VmStato vm_stato(void){return VM_PRONTO;}
void registro_riga(RegSorgente f,const char *format,...){(void)f;(void)format;}
int main(void)
{
    Config config={0};
    SetEnvironmentVariableW(L"HABUMI_LAUNCH_COMPONENT",L"com.example/.Nested$Activity");
    assert(apps_prepare() && apps_requested());
    apps_tick(&config,false,false);assert(starts==0 && apps_requested());
    apps_tick(&config,true,false);assert(starts==1 && !apps_requested());
    assert(!strcmp(tail,"\"am start -W -n 'com.example/.Nested$Activity'\""));
    apps_tick(&config,true,false);assert(closes==1);
    struct {wchar_t root[MAX_PATH];char component[APP_COMPONENT_MAX];} request={0};
    GetCurrentDirectoryW(MAX_PATH,request.root);strcpy(request.component,"com.android.settings/.Settings");
    COPYDATASTRUCT data={0x48415031u,sizeof(request),&request};
    assert(apps_receive(NULL)==0);data.cbData--;assert(apps_receive(&data)==0);data.cbData++;
    data.dwData++;assert(apps_receive(&data)==0);data.dwData--;
    request.root[0]=L'?';assert(apps_receive(&data)==0);GetCurrentDirectoryW(MAX_PATH,request.root);
    memset(request.component,'A',sizeof(request.component));assert(apps_receive(&data)==0);
    strcpy(request.component,"com.example/.Main;echo");assert(apps_receive(&data)==0);
    strcpy(request.component,"com.android.settings/.Settings");
    assert(apps_receive(&data)==1);assert(apps_receive(&data)==0);
    apps_tick(&config,true,false);assert(starts==2);
    apps_tick(&config,true,false);assert(closes==2);
    assert(apps_receive(&data)==1);apps_tick(&config,false,false);
    apps_tick(&config,true,false);assert(starts==2);
    apps_tick(&config,true,true);assert(apps_receive(&data)==0);
    assert(apps_shutdown());
    puts("PASS: app readiness, exact component quoting, IPC version/profile/payload validation, busy request, restart and shutdown cancellation.");
    return 0;
}
