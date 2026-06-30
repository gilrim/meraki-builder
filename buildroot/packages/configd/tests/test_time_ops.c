#define _GNU_SOURCE
#include "time_ops.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
  char temp[]="/tmp/configd-time-XXXXXX"; int fd=mkstemp(temp); assert(fd>=0);
  FILE *file=fdopen(fd,"w"); assert(file);
  fputs("{\"timezone\":\"America/Moncton\",\"standard_offset_minutes\":-240,\"dst\":{\"enabled\":true,\"offset_minutes\":-180,\"start\":{\"month\":3,\"week\":2,\"weekday\":0,\"hour\":2,\"minute\":0},\"end\":{\"month\":11,\"week\":1,\"weekday\":0,\"hour\":2,\"minute\":0}},\"ntp_enabled\":true,\"servers\":[\"pool.ntp.org\"]}\n",file);
  assert(fclose(file)==0); setenv("CONFIGD_TIME_POLICY",temp,1); setenv("CONFIGD_TIME_POLICY_DEFAULT",temp,1); setenv("CONFIGD_CLOCK_SET_DRY_RUN","1",1);
  char error[256]={0}; assert(time_set_local("12:34:56 - 15:01:2026",error,sizeof(error))==0);
  error[0]='\0'; assert(time_set_local("01:30:00 - 01:11:2026",error,sizeof(error))==0);
  error[0]='\0'; assert(time_set_local("02:30:00 - 08:03:2026",error,sizeof(error))!=0);
  error[0]='\0'; assert(time_set_local("2026-01-15 12:34:56",error,sizeof(error))!=0);
  error[0]='\0'; assert(time_set_local("12:34:56 - 31:02:2026",error,sizeof(error))!=0);
  char zones[]="/tmp/configd-zones-XXXXXX"; int zfd=mkstemp(zones); assert(zfd>=0);
  FILE *zf=fdopen(zfd,"w"); assert(zf); fputs("[{\"label\":\"Test\",\"zones\":[{\"id\":\"Etc/UTC\"}]}]\n",zf); assert(fclose(zf)==0);
  setenv("CONFIGD_TIMEZONES_FILE",zones,1); struct json_object *catalogue=timezones_json(); assert(catalogue); assert(json_object_array_length(catalogue)==1); json_object_put(catalogue);
  unlink(zones); unlink(temp); puts("time operation tests passed"); return 0;
}
