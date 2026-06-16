#include "system_ops.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TERMINAL_OUTPUT_LIMIT 65536
#define TERMINAL_TIMEOUT_MS 15000

static void set_error(char *error, size_t size, const char *message) {
  if (error && size) snprintf(error, size, "%s", message ? message : "error");
}

static long long monotonic_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
}

struct json_object *terminal_execute(const char *command) {
  struct json_object *result = json_object_new_object();
  json_object_object_add(result, "command",
                         json_object_new_string(command ? command : ""));
  if (!command || !*command || strlen(command) > 4096) {
    json_object_object_add(result, "output",
                           json_object_new_string("Invalid command\n"));
    json_object_object_add(result, "exit_code", json_object_new_int(2));
    json_object_object_add(result, "timed_out", json_object_new_boolean(false));
    json_object_object_add(result, "truncated", json_object_new_boolean(false));
    return result;
  }

  int output_pipe[2];
  if (pipe(output_pipe) != 0) {
    json_object_object_add(result, "output",
                           json_object_new_string(strerror(errno)));
    json_object_object_add(result, "exit_code", json_object_new_int(127));
    return result;
  }
  pid_t child = fork();
  if (child == 0) {
    setpgid(0, 0);
    dup2(output_pipe[1], STDOUT_FILENO);
    dup2(output_pipe[1], STDERR_FILENO);
    int nullfd = open("/dev/null", O_RDONLY);
    if (nullfd >= 0) {
      dup2(nullfd, STDIN_FILENO);
      if (nullfd > STDERR_FILENO) close(nullfd);
    }
    close(output_pipe[0]);
    close(output_pipe[1]);
    setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("TERM", "dumb", 1);
    execl("/bin/sh", "sh", "-c", command, (char *)NULL);
    _exit(127);
  }
  close(output_pipe[1]);
  if (child < 0) {
    close(output_pipe[0]);
    json_object_object_add(result, "output",
                           json_object_new_string(strerror(errno)));
    json_object_object_add(result, "exit_code", json_object_new_int(127));
    return result;
  }

  int flags = fcntl(output_pipe[0], F_GETFL, 0);
  fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);
  char *output = calloc(1, TERMINAL_OUTPUT_LIMIT + 1);
  if (!output) {
    kill(-child, SIGKILL);
    kill(child, SIGKILL);
    waitpid(child, NULL, 0);
    close(output_pipe[0]);
    json_object_object_add(result, "output",
                           json_object_new_string("Out of memory"));
    json_object_object_add(result, "exit_code", json_object_new_int(127));
    json_object_object_add(result, "timed_out", json_object_new_boolean(false));
    json_object_object_add(result, "truncated", json_object_new_boolean(false));
    return result;
  }

  size_t used = 0;
  bool truncated = false;
  bool timed_out = false;
  bool child_done = false;
  int status = 0;
  long long deadline = monotonic_ms() + TERMINAL_TIMEOUT_MS;

  while (!child_done || used < TERMINAL_OUTPUT_LIMIT) {
    struct pollfd fd = {output_pipe[0], POLLIN | POLLHUP, 0};
    int remaining = (int)(deadline - monotonic_ms());
    if (remaining <= 0 && !child_done) {
      timed_out = true;
      kill(-child, SIGKILL);
      kill(child, SIGKILL);
    }
    int wait_ms = child_done ? 0 :
        (remaining > 200 ? 200 : (remaining > 0 ? remaining : 0));
    poll(&fd, 1, wait_ms);
    if (fd.revents & (POLLIN | POLLHUP)) {
      char buffer[4096];
      ssize_t got;
      while ((got = read(output_pipe[0], buffer, sizeof(buffer))) > 0) {
        size_t keep = (size_t)got;
        if (used + keep > TERMINAL_OUTPUT_LIMIT) {
          keep = TERMINAL_OUTPUT_LIMIT - used;
          truncated = true;
        }
        if (keep) memcpy(output + used, buffer, keep);
        used += keep;
        if (used == TERMINAL_OUTPUT_LIMIT) truncated = true;
      }
    }
    pid_t waited = waitpid(child, &status, WNOHANG);
    if (waited == child) child_done = true;
    if (child_done && !(fd.revents & POLLIN)) break;
  }
  if (!child_done) waitpid(child, &status, 0);
  close(output_pipe[0]);
  output[used] = '\0';

  int exit_code = 127;
  if (WIFEXITED(status)) exit_code = WEXITSTATUS(status);
  else if (WIFSIGNALED(status)) exit_code = 128 + WTERMSIG(status);
  json_object_object_add(result, "output", json_object_new_string(output));
  json_object_object_add(result, "exit_code", json_object_new_int(exit_code));
  json_object_object_add(result, "timed_out", json_object_new_boolean(timed_out));
  json_object_object_add(result, "truncated", json_object_new_boolean(truncated));
  free(output);
  return result;
}

struct json_object *firmware_status_json(void) {
  const char *path = getenv("FWUPDATE_STATUS_FILE");
  if (!path || !*path) path = "/run/fwupdate/status.json";
  struct json_object *status = json_object_from_file(path);
  if (status && json_object_is_type(status, json_type_object)) return status;
  if (status) json_object_put(status);
  status = json_object_new_object();
  json_object_object_add(status, "state", json_object_new_string("idle"));
  json_object_object_add(status, "stage", json_object_new_string("idle"));
  json_object_object_add(status, "progress", json_object_new_int(0));
  json_object_object_add(status, "message",
                         json_object_new_string("No update is active"));
  return status;
}

static int calculate_sha256(const char *path, char output[65],
                            char *error, size_t error_size) {
  int pipefd[2];
  if (pipe(pipefd) != 0) {
    set_error(error, error_size, strerror(errno));
    return -errno;
  }
  pid_t child = fork();
  if (child < 0) {
    int saved = errno;
    close(pipefd[0]); close(pipefd[1]);
    set_error(error, error_size, strerror(saved));
    return -saved;
  }
  if (child == 0) {
    dup2(pipefd[1], STDOUT_FILENO);
    int nullfd = open("/dev/null", O_WRONLY);
    if (nullfd >= 0) {
      dup2(nullfd, STDERR_FILENO);
      if (nullfd > STDERR_FILENO) close(nullfd);
    }
    close(pipefd[0]); close(pipefd[1]);
    execlp("sha256sum", "sha256sum", path, (char *)NULL);
    _exit(127);
  }
  close(pipefd[1]);
  char buffer[96] = {0};
  ssize_t total = 0;
  while (total < (ssize_t)sizeof(buffer) - 1) {
    ssize_t got = read(pipefd[0], buffer + total,
                       sizeof(buffer) - 1 - (size_t)total);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) break;
    total += got;
  }
  close(pipefd[0]);
  int status = 0;
  waitpid(child, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || total < 64) {
    set_error(error, error_size, "unable to calculate firmware SHA-256");
    return -EIO;
  }
  for (int i = 0; i < 64; i++) {
    char c = buffer[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) {
      set_error(error, error_size, "sha256sum returned invalid output");
      return -EIO;
    }
    output[i] = c;
  }
  output[64] = '\0';
  return 0;
}

int firmware_start_update(const char *path, const char *overlay, bool force,
                          char *error, size_t error_size) {
  struct stat st;
  if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
    set_error(error, error_size, "uploaded firmware is unavailable");
    return -ENOENT;
  }
  const char *program = access("/usr/bin/fw_update", X_OK) == 0
                            ? "/usr/bin/fw_update" : "/bin/fw_update";
  if (access(program, X_OK) != 0) {
    set_error(error, error_size, "fw_update is unavailable");
    return -ENOENT;
  }
  char sha256[65];
  int rc = calculate_sha256(path, sha256, error, error_size);
  if (rc != 0) return rc;

  pid_t child = fork();
  if (child < 0) {
    set_error(error, error_size, strerror(errno));
    return -errno;
  }
  if (child == 0) {
    setsid();
    sleep(1);
    mkdir("/run/fwupdate", 0700);
    int logfd = open("/run/fwupdate/web-update.log",
                     O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (logfd >= 0) {
      dup2(logfd, STDOUT_FILENO);
      dup2(logfd, STDERR_FILENO);
      if (logfd > STDERR_FILENO) close(logfd);
    }
    int nullfd = open("/dev/null", O_RDONLY);
    if (nullfd >= 0) {
      dup2(nullfd, STDIN_FILENO);
      if (nullfd > STDERR_FILENO) close(nullfd);
    }
    if (force) {
      execl(program, "fw_update", "--sha256", sha256, "--overlay", overlay,
            "--source", "web-upload", "--yes", "--force", path,
            (char *)NULL);
    } else {
      execl(program, "fw_update", "--sha256", sha256, "--overlay", overlay,
            "--source", "web-upload", "--yes", path, (char *)NULL);
    }
    _exit(127);
  }
  return 0;
}
