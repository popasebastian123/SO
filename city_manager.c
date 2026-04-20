#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#define REPORTS_FILE "reports.dat"
#define CFG_FILE "district.cfg"
#define LOG_FILE "logged_district"

#define DISTRICT_MODE 0750
#define REPORTS_MODE  0664
#define CFG_MODE      0640
#define LOG_MODE      0644

#define NAME_LEN 32
#define CATEGORY_LEN 32
#define DESC_LEN 256

typedef struct {
  int report_id;
  char inspector[NAME_LEN];
  double latitude;
  double longitude;
  char category[CATEGORY_LEN];
  int severity;
  time_t timestamp;
  char description[DESC_LEN];
}Report;

typedef struct {
  char role[16];
  char user[NAME_LEN];
}Context;

/* HELPERS */


static void trim_newline(char *s) {
  if (!s) return;
  ssize_t n = strlen(s);
  if (n > 0 && s[n - 1] == '\n') s[n - 1] = '\0';
}

static void safe_copy(char *dst, const char *src, ssize_t n) {
  if (n == 0) return;
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

static int is_manager(const Context *ctx) {
  return strcmp(ctx->role, "manager") == 0;
}

static int is_inspector(const Context *ctx) {
  return strcmp(ctx->role, "inspector") == 0;
}

static void mode_to_string(mode_t mode, char out[10]) {
  out[0] = (mode & S_IRUSR) ? 'r' : '-';
  out[1] = (mode & S_IWUSR) ? 'w' : '-';
  out[2] = (mode & S_IXUSR) ? 'x' : '-';
  out[3] = (mode & S_IRGRP) ? 'r' : '-';
  out[4] = (mode & S_IWGRP) ? 'w' : '-';
  out[5] = (mode & S_IXGRP) ? 'x' : '-';
  out[6] = (mode & S_IROTH) ? 'r' : '-';
  out[7] = (mode & S_IWOTH) ? 'w' : '-';
  out[8] = (mode & S_IXOTH) ? 'x' : '-';
  out[9] = '\0';
}

static void make_path(char *out, ssize_t outsz, const char *district, const char *name) {
  snprintf(out, outsz, "%s/%s", district, name);
}

static void make_symlink_name(char *out, ssize_t outsz, const char *district) {
  snprintf(out, outsz, "active_reports-%s", district);
}

static void print_time_human(time_t t) {
  char buf[64];
  struct tm *tm_info = localtime(&t);
  if (!tm_info) {
    printf("(invalid time)");
    return;
  }
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
  printf("%s", buf);
}

static int exact_mode_matches(const char *path, mode_t expected) {
  struct stat st;
  if (stat(path, &st) == -1) {
    perror("stat");
    return 0;
  }
  return ((st.st_mode & 0777) == expected);
}

/* PERMISSION CHECKS */

static int require_simulated_read(const char *path, const Context *ctx) {
  struct stat st;
  if (stat(path, &st) == -1) {
    perror(path);
    return 0;
  }

  mode_t m = st.st_mode;
  if (is_manager(ctx)) {
    if (!(m & S_IRUSR)) {
      fprintf(stderr, "Access denied: manager role lacks owner-read permission on %s\n", path);
      return 0;
    }
  } else if (is_inspector(ctx)) {
    if (!(m & S_IRGRP)) {
      fprintf(stderr, "Access denied: inspector role lacks group-read permission on %s\n", path);
      return 0;
    }
  } else {
    fprintf(stderr, "Unknown role: %s\n", ctx->role);
    return 0;
  }

  return 1;
}

static int require_simulated_write(const char *path, const Context *ctx) {
  struct stat st;
  if (stat(path, &st) == -1) {
    perror(path);
    return 0;
  }

  mode_t m = st.st_mode;
  if (is_manager(ctx)) {
    if (!(m & S_IWUSR)) {
      fprintf(stderr, "Access denied: manager role lacks owner-write permission on %s\n", path);
      return 0;
    }
  } else if (is_inspector(ctx)) {
    if (!(m & S_IWGRP)) {
      fprintf(stderr, "Access denied: inspector role lacks group-write permission on %s\n", path);
      return 0;
    }
  } else {
    fprintf(stderr, "Unknown role: %s\n", ctx->role);
    return 0;
  }

  return 1;
}

static int require_manager_only(const Context *ctx, const char *op_name) {
  if (!is_manager(ctx)) {
    fprintf(stderr, "Operation '%s' is manager-only.\n", op_name);
    return 0;
  }
  return 1;
}