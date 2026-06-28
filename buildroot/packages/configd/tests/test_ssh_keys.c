#include "ssh_keys.h"
#include "config_file.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool dry_run = false;
const char *config_file = "/tmp/configd-ssh-test.json";

static const char *ED = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4f";
static const char *RSA = "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAAAQAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0A=";
static const char *ECDSA = "ecdsa-sha2-nistp256 AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAABBBAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0A=";
static const char *SK = "sk-ssh-ed25519@openssh.com AAAAGnNrLXNzaC1lZDI1NTE5QG9wZW5zc2guY29tAAAAIAABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fAAAACHNzaDp0ZXN0";

static char *with_comment(const char *key, const char *comment) {
  size_t n = strlen(key) + strlen(comment) + 2;
  char *out = malloc(n); assert(out);
  snprintf(out, n, "%s %s", key, comment); return out;
}

int main(void) {
  char err[256];
  char *ed_laptop = with_comment(ED, "laptop");
  char *rsa_comment = with_comment(RSA, "key@rsa");
  assert(ssh_key_validate(ed_laptop, err, sizeof(err)) == 0);
  assert(ssh_key_validate(rsa_comment, err, sizeof(err)) == 0);
  assert(ssh_key_validate(ECDSA, err, sizeof(err)) == 0);
  assert(ssh_key_validate(SK, err, sizeof(err)) == 0);
  free(ed_laptop); free(rsa_comment);

  assert(ssh_key_validate("", err, sizeof(err)) != 0);
  assert(ssh_key_validate("ssh-ed25519 AAAA\nfoo bar", err, sizeof(err)) != 0);
  assert(ssh_key_validate("command=\"x\" ssh-ed25519 AAAAC3Nz", err, sizeof(err)) != 0);
  assert(ssh_key_validate("ssh-dss AAAAB3Nz", err, sizeof(err)) != 0);
  assert(ssh_key_validate("ssh-ed25519 not*base64!", err, sizeof(err)) != 0);
  assert(ssh_key_validate("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAAQ==", err, sizeof(err)) != 0);
  assert(ssh_key_validate("ssh-rsa AAAAC3NzaC1lZDI1NTE5AAAAIAABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4f", err, sizeof(err)) != 0);
  assert(ssh_key_validate("ssh-ed25519 AAAAB3Nz comment\x01here", err, sizeof(err)) != 0);

  char long_key[17000]; memset(long_key, 'A', sizeof(long_key) - 1); long_key[sizeof(long_key) - 1] = '\0';
  assert(ssh_key_validate(long_key, err, sizeof(err)) != 0);

  char *alice = with_comment(ED, "alice"), *bob = with_comment(ED, "bob");
  char id1[512], id2[512];
  assert(ssh_key_identity(alice, id1, sizeof(id1)) == 0);
  assert(ssh_key_identity(bob, id2, sizeof(id2)) == 0);
  assert(!strcmp(id1, id2) && !strcmp(id1, ED));
  char tiny[10]; assert(ssh_key_identity(alice, tiny, sizeof(tiny)) != 0);
  free(alice); free(bob);

  setenv("CONFIGD_SSH_DIR", "/tmp/configd-ssh-test-dir", 1);
  setenv("CONFIGD_AUTHORIZED_KEYS", "/tmp/configd-ssh-test-dir/authorized_keys", 1);
  system("rm -rf /tmp/configd-ssh-test-dir /tmp/configd-ssh-test.json /tmp/configd-ssh-test.json.bak");
  char *k1 = with_comment(ED, "one"), *k2 = with_comment(RSA, "two"), *failed = with_comment(SK, "failed");
  assert(ssh_keys_add("first", k1, err, sizeof(err)) == 0);
  assert(ssh_keys_add("second", k2, err, sizeof(err)) == 0);

  setenv("CONFIGD_SSH_FAIL_RENDER", "1", 1);
  assert(ssh_keys_add("failed", failed, err, sizeof(err)) != 0);
  unsetenv("CONFIGD_SSH_FAIL_RENDER");
  struct json_object *list = ssh_keys_list(); assert(json_object_array_length(list) == 2); json_object_put(list);

  char *duplicate = with_comment(ED, "other");
  assert(ssh_keys_add("dup", duplicate, err, sizeof(err)) != 0); free(duplicate);
  FILE *file = fopen("/tmp/configd-ssh-test-dir/authorized_keys", "r"); assert(file);
  char body[4096]; size_t got = fread(body, 1, sizeof(body)-1, file); fclose(file); body[got] = '\0';
  assert(strstr(body, k1) && strstr(body, k2) && !strstr(body, failed));
  struct stat st; assert(stat("/tmp/configd-ssh-test-dir/authorized_keys", &st) == 0 && (st.st_mode & 0777) == 0600);
  assert(stat("/tmp/configd-ssh-test-dir", &st) == 0 && (st.st_mode & 0777) == 0700);

  setenv("CONFIGD_SSH_FAIL_RENDER", "1", 1);
  assert(ssh_keys_remove(k1, err, sizeof(err)) != 0);
  unsetenv("CONFIGD_SSH_FAIL_RENDER");
  list = ssh_keys_list(); assert(json_object_array_length(list) == 2); json_object_put(list);
  file = fopen("/tmp/configd-ssh-test-dir/authorized_keys", "r"); assert(file);
  got = fread(body, 1, sizeof(body)-1, file); fclose(file); body[got] = '\0'; assert(strstr(body, k1));

  assert(ssh_keys_remove(k1, err, sizeof(err)) == 0);
  list = ssh_keys_list(); assert(json_object_array_length(list) == 1); json_object_put(list);
  file = fopen("/tmp/configd-ssh-test-dir/authorized_keys", "r"); assert(file);
  got = fread(body, 1, sizeof(body)-1, file); fclose(file); body[got] = '\0'; assert(!strstr(body, k1) && strstr(body, k2));
  free(k1); free(k2); free(failed);
  puts("ssh_keys validation tests passed");
  return 0;
}
