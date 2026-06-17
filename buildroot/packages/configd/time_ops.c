#define _GNU_SOURCE
#include "time_ops.h"
#include "service_ops.h"

#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TIME_POLICY_PATH "/config/postmerkos/time.json"
#define TIME_POLICY_DEFAULT "/usr/share/postmerkos/defaults/time.json"

static void set_error(char *error, size_t size, const char *message) {
  if (error && size) snprintf(error, size, "%s", message ? message : "error");
}
static struct json_object *member(struct json_object *o, const char *k) {
  struct json_object *v = NULL;
  return o && json_object_is_type(o, json_type_object) &&
      json_object_object_get_ex(o, k, &v) ? v : NULL;
}
static int integer(struct json_object *o, const char *k, int fallback) {
  struct json_object *v = member(o, k);
  return v && json_object_is_type(v, json_type_int) ? json_object_get_int(v) : fallback;
}
static bool boolean(struct json_object *o, const char *k, bool fallback) {
  struct json_object *v = member(o, k);
  return v && json_object_is_type(v, json_type_boolean) ? json_object_get_boolean(v) : fallback;
}
static int atomic_write(const char *path, struct json_object *o) {
  mkdir("/config", 0755); mkdir("/config/postmerkos", 0700);
  char temp[320]; snprintf(temp, sizeof(temp), "%s.tmp", path);
  int fd = open(temp, O_WRONLY|O_CREAT|O_TRUNC, 0600); if (fd < 0) return -errno;
  const char *text = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY);
  size_t left = strlen(text); const char *p = text; int rc = 0;
  while (left) { ssize_t n = write(fd,p,left); if(n<0){if(errno==EINTR)continue;rc=-errno;break;}p+=n;left-=n; }
  if (!rc && write(fd, "\n", 1) != 1) rc = -EIO;
  if (!rc && fsync(fd) != 0) rc = -errno;
  if (close(fd) != 0 && !rc) rc = -errno;
  if (!rc && rename(temp, path) != 0) rc = -errno;
  if (rc) unlink(temp);
  return rc;
}

static int days_in_month(int year, int month) {
  static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int value = days[month-1];
  if (month == 2 && ((year%4==0 && year%100!=0) || year%400==0)) value++;
  return value;
}
static int weekday_utc(int year, int month, int day) {
  struct tm tmv = {0}; tmv.tm_year=year-1900; tmv.tm_mon=month-1; tmv.tm_mday=day;
  time_t epoch = timegm(&tmv); struct tm out; gmtime_r(&epoch,&out); return out.tm_wday;
}
static int nth_weekday(int year,int month,int week,int weekday) {
  if (week == 5) {
    int last=days_in_month(year,month); int w=weekday_utc(year,month,last);
    return last-((w-weekday+7)%7);
  }
  int first=weekday_utc(year,month,1);
  int day=1+((weekday-first+7)%7)+(week-1)*7;
  int max=days_in_month(year,month); return day<=max?day:max;
}
static time_t transition_utc(int year, struct json_object *rule, int before_offset) {
  int month=integer(rule,"month",3), week=integer(rule,"week",2);
  int weekday=integer(rule,"weekday",0), hour=integer(rule,"hour",2);
  int minute=integer(rule,"minute",0);
  int day=nth_weekday(year,month,week,weekday);
  struct tm tmv={0}; tmv.tm_year=year-1900; tmv.tm_mon=month-1; tmv.tm_mday=day;
  tmv.tm_hour=hour; tmv.tm_min=minute;
  return timegm(&tmv) - before_offset*60;
}
static int active_offset(struct json_object *policy, time_t now, bool *dst_active) {
  int standard=integer(policy,"standard_offset_minutes",0);
  struct json_object *dst=member(policy,"dst");
  if (!dst || !boolean(dst,"enabled",false)) { if(dst_active)*dst_active=false; return standard; }
  int daylight=integer(dst,"offset_minutes",standard+60);
  struct json_object *start=member(dst,"start"), *end=member(dst,"end");
  if(!start || !end){if(dst_active)*dst_active=false;return standard;}
  struct tm utc; gmtime_r(&now,&utc); int year=utc.tm_year+1900;
  time_t begin=transition_utc(year,start,standard);
  time_t finish=transition_utc(year,end,daylight);
  bool active = begin < finish ? now>=begin && now<finish : now>=begin || now<finish;
  if (dst_active) *dst_active = active;
  return active ? daylight : standard;
}

struct json_object *time_policy_load(void) {
  struct json_object *p=json_object_from_file(TIME_POLICY_PATH);
  if(!p) p=json_object_from_file(TIME_POLICY_DEFAULT);
  return p ? p : json_tokener_parse("{\"standard_offset_minutes\":0,\"dst\":{\"enabled\":false,\"offset_minutes\":60},\"ntp_enabled\":true,\"servers\":[\"pool.ntp.org\"]}");
}

static int validate_rule(struct json_object *rule) {
  if(!rule || !json_object_is_type(rule,json_type_object)) return -EINVAL;
  int m=integer(rule,"month",0), w=integer(rule,"week",0), d=integer(rule,"weekday",-1);
  int h=integer(rule,"hour",-1), min=integer(rule,"minute",0);
  return m>=1&&m<=12&&w>=1&&w<=5&&d>=0&&d<=6&&h>=0&&h<=23&&min>=0&&min<=59?0:-EINVAL;
}
static int validate_policy(struct json_object *p,char *error,size_t n) {
  if(!p || !json_object_is_type(p,json_type_object)){set_error(error,n,"time policy must be an object");return -EINVAL;}
  int off=integer(p,"standard_offset_minutes",9999);
  if(off < -720 || off > 840){set_error(error,n,"UTC offset must be between -720 and 840 minutes");return -EINVAL;}
  struct json_object *servers=member(p,"servers");
  if(!servers || !json_object_is_type(servers,json_type_array) || json_object_array_length(servers)>8){set_error(error,n,"NTP servers must be an array of at most 8 entries");return -EINVAL;}
  for(size_t i=0;i<json_object_array_length(servers);i++) if(!json_object_is_type(json_object_array_get_idx(servers,i),json_type_string)){set_error(error,n,"NTP server entries must be strings");return -EINVAL;}
  struct json_object *dst=member(p,"dst");
  if(dst && boolean(dst,"enabled",false)) {
    int doff=integer(dst,"offset_minutes",9999);
    if(doff < -720 || doff > 840 || validate_rule(member(dst,"start")) || validate_rule(member(dst,"end"))){set_error(error,n,"DST rule is invalid");return -EINVAL;}
  }
  return 0;
}

static int render_chrony(struct json_object *p) {
  FILE *f=fopen("/etc/chrony.conf","w"); if(!f)return -errno;
  struct json_object *servers=member(p,"servers");
  for(size_t i=0;i<json_object_array_length(servers);i++) {
    const char *s=json_object_get_string(json_object_array_get_idx(servers,i));
    if(s&&*s) fprintf(f,"server %s iburst\n",s);
  }
  fputs("driftfile /run/chrony/drift\nmakestep 1.0 3\nrtcsync\n",f);
  return fclose(f)==0?0:-errno;
}
int time_policy_apply(char *error,size_t error_size) {
  struct json_object *p=time_policy_load(); if(!p){set_error(error,error_size,"time policy unavailable");return -ENOENT;}
  int rc=render_chrony(p); json_object_put(p);
  if (rc) set_error(error, error_size, strerror(-rc));
  return rc;
}
int time_policy_save(struct json_object *p,char *error,size_t n) {
  int rc=validate_policy(p,error,n); if(rc)return rc;
  rc=atomic_write(TIME_POLICY_PATH,p); if(rc){set_error(error,n,strerror(-rc));return rc;}
  return time_policy_apply(error,n);
}
struct json_object *time_status_json(void) {
  struct json_object *p=time_policy_load(), *root=json_object_new_object();
  time_t now=time(NULL); bool active=false; int off=active_offset(p,now,&active);
  struct tm utc,local; gmtime_r(&now,&utc); time_t shifted=now+off*60; gmtime_r(&shifted,&local);
  char ubuf[40],lbuf[40]; strftime(ubuf,sizeof(ubuf),"%Y-%m-%dT%H:%M:%SZ",&utc); strftime(lbuf,sizeof(lbuf),"%Y-%m-%dT%H:%M:%S",&local);
  json_object_object_add(root,"utc",json_object_new_string(ubuf));
  json_object_object_add(root,"local",json_object_new_string(lbuf));
  json_object_object_add(root,"offset_minutes",json_object_new_int(off));
  json_object_object_add(root,"dst_active",json_object_new_boolean(active));
  json_object_object_add(root,"policy",p);
  return root;
}
int time_set_epoch(time_t epoch,char *error,size_t n) {
  if(epoch < 946684800){set_error(error,n,"date must be after 2000-01-01");return -EINVAL;}
  struct timespec ts={.tv_sec=epoch,.tv_nsec=0};
  if(clock_settime(CLOCK_REALTIME,&ts)!=0){set_error(error,n,strerror(errno));return -errno;}
  return 0;
}
int time_force_sync(char *error,size_t n) {
  pid_t child=fork(); if(child<0){set_error(error,n,strerror(errno));return -errno;}
  if(child==0){execlp("chronyc","chronyc","makestep",(char*)NULL);_exit(127);}
  int status=0; while(waitpid(child,&status,0)<0)if(errno!=EINTR){set_error(error,n,strerror(errno));return -errno;}
  if(!WIFEXITED(status)||WEXITSTATUS(status)!=0){set_error(error,n,"chrony synchronization request failed");return -EIO;}
  return 0;
}
