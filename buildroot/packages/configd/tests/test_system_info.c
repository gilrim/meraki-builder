#define _GNU_SOURCE
#include "system_info.h"
#include <assert.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_text(const char *path, const char *text) {
  FILE *file=fopen(path,"w"); assert(file); assert(fputs(text,file)>=0); assert(fclose(file)==0);
}
static struct json_object *path_get(struct json_object *root,const char *a,const char *b) {
  struct json_object *one=NULL,*two=NULL;
  assert(json_object_object_get_ex(root,a,&one));
  assert(json_object_object_get_ex(one,b,&two));
  return two;
}
int main(void) {
  char temp[]="/tmp/configd-system-info-XXXXXX"; assert(mkdtemp(temp));
  char board[256],cpu[256],mem[256],uptime[256],load[256],cmdline[256],mtd[256],mounts[256],proc[256];
  snprintf(board,sizeof(board),"%s/boardinfo",temp); snprintf(cpu,sizeof(cpu),"%s/cpuinfo",temp);
  snprintf(mem,sizeof(mem),"%s/meminfo",temp); snprintf(uptime,sizeof(uptime),"%s/uptime",temp);
  snprintf(load,sizeof(load),"%s/loadavg",temp); snprintf(cmdline,sizeof(cmdline),"%s/cmdline",temp);
  snprintf(mtd,sizeof(mtd),"%s/mtd",temp); snprintf(mounts,sizeof(mounts),"%s/mounts",temp);
  snprintf(proc,sizeof(proc),"%s/proc",temp); assert(mkdir(proc,0700)==0);
  char pid1[280],pid2[280]; snprintf(pid1,sizeof(pid1),"%s/1",proc); snprintf(pid2,sizeof(pid2),"%s/42",proc);
  assert(mkdir(pid1,0700)==0); assert(mkdir(pid2,0700)==0);
  write_text(board,"MODEL=MS42P\nPRODUCT_NUMBER=MS42P-HW\nSERIAL=Q2TESTSERIAL\nMAC=00:11:22:33:44:55\n");
  write_text(cpu,"system type : VCore-III\nprocessor : 0\ncpu model : MIPS 24Kc V7.4\nBogoMIPS : 416.00\nprocessor : 1\n");
  write_text(mem,"MemTotal: 131072 kB\nMemFree: 32768 kB\nMemAvailable: 65536 kB\nCached: 8192 kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\n");
  write_text(uptime,"12345.67 100.00\n"); write_text(load,"0.10 0.20 0.30 1/42 123\n");
  write_text(cmdline,"console=ttyS0 root=/dev/mtdblock2\n");
  write_text(mtd,"dev:    size   erasesize  name\nmtd0: 00040000 00010000 \"RedBoot\"\nmtd1: 002c0000 00010000 \"kernel\"\n");
  write_text(mounts,"");
  setenv("CONFIGD_BOARDINFO",board,1); setenv("CONFIGD_CPUINFO_FILE",cpu,1);
  setenv("CONFIGD_MEMINFO_FILE",mem,1); setenv("CONFIGD_UPTIME_FILE",uptime,1);
  setenv("CONFIGD_LOADAVG_FILE",load,1); setenv("CONFIGD_CMDLINE_FILE",cmdline,1);
  setenv("CONFIGD_MTD_FILE",mtd,1); setenv("CONFIGD_MOUNTS_FILE",mounts,1);
  setenv("CONFIGD_PROC_ROOT",proc,1); setenv("CONFIGD_ROOT_PATH",temp,1);
  setenv("CONFIGD_OVERLAY_PATH",temp,1); setenv("CONFIGD_CONFIG_PATH",temp,1); setenv("CONFIGD_TMP_PATH",temp,1);
  struct json_object *root=system_info_json(); assert(root);
  assert(!strcmp(json_object_get_string(path_get(root,"identity","serial_number")),"Q2TESTSERIAL"));
  assert(json_object_get_int(path_get(root,"processor","logical_processors"))==2);
  assert(!strcmp(json_object_get_string(path_get(root,"processor","model")),"MIPS 24Kc V7.4"));
  assert(json_object_get_int64(path_get(root,"runtime","uptime_seconds"))==12345);
  assert(json_object_get_int(path_get(root,"runtime","process_count"))==2);
  assert(json_object_get_int64(path_get(root,"memory","available_bytes"))==65536LL*1024LL);
  struct json_object *flash=NULL,*parts=NULL; assert(json_object_object_get_ex(root,"flash",&flash));
  assert(json_object_object_get_ex(flash,"partitions",&parts)); assert(json_object_array_length(parts)==2);
  assert(json_object_get_int64(path_get(root,"flash","total_bytes"))==0x300000);
  struct json_object *storage=NULL,*overlay=NULL,*available=NULL;
  assert(json_object_object_get_ex(root,"storage",&storage)); assert(json_object_object_get_ex(storage,"overlay",&overlay));
  assert(json_object_object_get_ex(overlay,"available",&available)); assert(json_object_get_boolean(available));
  json_object_put(root);
  char command[600]; snprintf(command,sizeof(command),"rm -rf '%s'",temp); assert(system(command)==0);
  puts("system info tests passed"); return 0;
}
