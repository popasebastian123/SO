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

/* SYMLINKS */

static void warn_if_dangling_symlink(const char *district) {
  char linkname[PATH_MAX];
  struct stat lst, st;

  make_symlink_name(linkname, sizeof(linkname), district);

  if (lstat(linkname, &lst) == -1) {
    return; /* no link yet */
  }

  if (!S_ISLNK(lst.st_mode)) {
    return;
  }

  if (stat(linkname, &st) == -1) {
    fprintf(stderr, "Warning: dangling symlink detected: %s\n", linkname);
  }
}

int symlink(char * str, char * text);

static int create_or_update_symlink(const char *district) {
  char target[PATH_MAX];
  char linkname[PATH_MAX];

  snprintf(target, sizeof(target), "%s/%s", district, REPORTS_FILE);
  make_symlink_name(linkname, sizeof(linkname), district);

  unlink(linkname);
  if (symlink(target, linkname) == -1) {
    perror("symlink");
    return 0;
  }
  return 1;
}

/* DISTRICT/FILE CREATION */

static int ensure_district_exists(const Context *ctx, const char *district) {
  struct stat st;
  char path[PATH_MAX];
  int fd;

  if (stat(district, &st) == -1) {
    if (!is_manager(ctx)) {
      fprintf(stderr, "District '%s' does not exist. Only manager may initialize it.\n", district);
      return 0;
    }

    if (mkdir(district, DISTRICT_MODE) == -1) {
      perror("mkdir");
      return 0;
    }
    chmod(district, DISTRICT_MODE);
  }

  make_path(path, sizeof(path), district, REPORTS_FILE);
  if (stat(path, &st) == -1) {
    fd = open(path, O_CREAT | O_RDWR, REPORTS_MODE);
    if (fd == -1) {
      perror("open reports.dat");
      return 0;
    }
    close(fd);
    chmod(path, REPORTS_MODE);
  } else {
    chmod(path, REPORTS_MODE);
  }

  make_path(path, sizeof(path), district, CFG_FILE);
  if (stat(path, &st) == -1) {
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, CFG_MODE);
    if (fd == -1) {
      perror("open district.cfg");
      return 0;
    }
    if (write(fd, "threshold=2\n", 12) != 12) {
      perror("write district.cfg");
      close(fd);
      return 0;
    }
    close(fd);
    chmod(path, CFG_MODE);
  } else {
    chmod(path, CFG_MODE);
  }

  make_path(path, sizeof(path), district, LOG_FILE);
  if (stat(path, &st) == -1) {
    fd = open(path, O_CREAT | O_RDWR, LOG_MODE);
    if (fd == -1) {
      perror("open logged_district");
      return 0;
    }
    close(fd);
    chmod(path, LOG_MODE);
  } else {
    chmod(path, LOG_MODE);
  }

  if (!create_or_update_symlink(district)) {
    return 0;
  }

  warn_if_dangling_symlink(district);
  return 1;
}

/* LOGGING */

static void log_action(const Context *ctx, const char *district, const char *action) {
  char path[PATH_MAX];
  char line[512];
  time_t now = time(NULL);
  int fd;

  make_path(path, sizeof(path), district, LOG_FILE);

  if (!require_simulated_write(path, ctx)) {
    fprintf(stderr, "Warning: action completed, but logging refused by simulated permissions for %s\n", path);
    return;
  }

  fd = open(path, O_WRONLY | O_APPEND);
  if (fd == -1) {
    perror("open log");
    return;
  }

  snprintf(line, sizeof(line), "%ld | role=%s | user=%s | %s\n",
           (long)now, ctx->role, ctx->user, action);

  if (write(fd, line, strlen(line)) == -1) {
    perror("write log");
  }

  close(fd);
}

/* THRESHOLD MANAGEMENT */

static int read_threshold(const Context *ctx, const char *district, int *value) {
  char path[PATH_MAX];
  char buf[128];
  int fd, n;

  make_path(path, sizeof(path), district, CFG_FILE);

  if (!require_simulated_read(path, ctx)) return 0;

  fd = open(path, O_RDONLY);
  if (fd == -1) {
    perror("open district.cfg");
    return 0;
  }

  n = (int)read(fd, buf, sizeof(buf) - 1);
  if (n < 0) {
    perror("read district.cfg");
    close(fd);
    return 0;
  }
  buf[n] = '\0';
  close(fd);

  if (sscanf(buf, "threshold=%d", value) != 1) {
    fprintf(stderr, "Invalid config file format in %s\n", path);
    return 0;
  }

  return 1;
}

static int write_threshold(const Context *ctx, const char *district, int value) {
  char path[PATH_MAX];
  char buf[64];
  int fd, len;

  make_path(path, sizeof(path), district, CFG_FILE);

  if (!require_manager_only(ctx, "update_threshold")) return 0;

  if (!exact_mode_matches(path, CFG_MODE)) {
    fprintf(stderr, "Refusing update: district.cfg permissions are not exactly 640\n");
    return 0;
  }

  if (!require_simulated_write(path, ctx)) return 0;

  fd = open(path, O_WRONLY | O_TRUNC);
  if (fd == -1) {
    perror("open district.cfg");
    return 0;
  }

  len = snprintf(buf, sizeof(buf), "threshold=%d\n", value);
  if (write(fd, buf, len) != len) {
    perror("write district.cfg");
    close(fd);
    return 0;
  }

  close(fd);
  return 1;
}

/* REPORTS I/0 */

static int next_report_id(int fd) {
    Report r;
    int max_id = 0;

    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
        perror("lseek");
        return 1;
    }

    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        if (r.report_id > max_id) max_id = r.report_id;
    }

    return max_id + 1;
}

static void print_report(const Report *r) {
    printf("Report ID: %d\n", r->report_id);
    printf("Inspector: %s\n", r->inspector);
    printf("Coordinates: lat=%.6f lon=%.6f\n", r->latitude, r->longitude);
    printf("Category: %s\n", r->category);
    printf("Severity: %d\n", r->severity);
    printf("Timestamp: ");
    print_time_human(r->timestamp);
    printf(" (%ld)\n", (long)r->timestamp);
    printf("Description: %s\n", r->description);
    printf("----------------------------------------\n");
}

static int read_line_prompt(const char *prompt, char *buf, size_t sz) {
    printf("%s", prompt);
    if (!fgets(buf, (int)sz, stdin)) return 0;
    trim_newline(buf);
    return 1;
}

static int prompt_new_report(Report *r, const Context *ctx) {
    char temp[512];

    memset(r, 0, sizeof(*r));
    safe_copy(r->inspector, ctx->user, sizeof(r->inspector));
    r->timestamp = time(NULL);

    if (!read_line_prompt("Latitude: ", temp, sizeof(temp))) return 0;
    r->latitude = atof(temp);

    if (!read_line_prompt("Longitude: ", temp, sizeof(temp))) return 0;
    r->longitude = atof(temp);

    if (!read_line_prompt("Category (road/lighting/flooding/...): ", temp, sizeof(temp))) return 0;
    safe_copy(r->category, temp, sizeof(r->category));

    if (!read_line_prompt("Severity (1-3): ", temp, sizeof(temp))) return 0;
    r->severity = atoi(temp);
    if (r->severity < 1 || r->severity > 3) {
        fprintf(stderr, "Invalid severity. Must be 1, 2 or 3.\n");
        return 0;
    }

    if (!read_line_prompt("Description: ", temp, sizeof(temp))) return 0;
    safe_copy(r->description, temp, sizeof(r->description));

    return 1;
}

static int cmd_add(const Context *ctx, const char *district) {
    char path[PATH_MAX];
    int fd, threshold;
    Report r;

    if (!ensure_district_exists(ctx, district)) return 0;

    make_path(path, sizeof(path), district, REPORTS_FILE);

    if (!require_simulated_write(path, ctx)) return 0;

    fd = open(path, O_RDWR);
    if (fd == -1) {
        perror("open reports.dat");
        return 0;
    }

    if (!prompt_new_report(&r, ctx)) {
        close(fd);
        return 0;
    }

    r.report_id = next_report_id(fd);

    if (lseek(fd, 0, SEEK_END) == (off_t)-1) {
        perror("lseek");
        close(fd);
        return 0;
    }

    if (write(fd, &r, sizeof(r)) != (ssize_t)sizeof(r)) {
        perror("write report");
        close(fd);
        return 0;
    }

    close(fd);
    chmod(path, REPORTS_MODE);

    if (read_threshold(ctx, district, &threshold)) {
        if (r.severity >= threshold) {
            printf("ALERT: report severity %d reached threshold %d\n", r.severity, threshold);
        }
    }

    printf("Report added with ID %d\n", r.report_id);
    log_action(ctx, district, "add");
    return 1;
}

static int cmd_list(const Context *ctx, const char *district) {
    char path[PATH_MAX];
    struct stat st;
    int fd;
    Report r;
    char perm[10];
    char timebuf[64];
    struct tm *tm_info;

    warn_if_dangling_symlink(district);

    make_path(path, sizeof(path), district, REPORTS_FILE);

    if (!require_simulated_read(path, ctx)) return 0;

    if (stat(path, &st) == -1) {
        perror("stat reports.dat");
        return 0;
    }

    mode_to_string(st.st_mode, perm);
    tm_info = localtime(&st.st_mtime);
    if (tm_info) {
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        safe_copy(timebuf, "invalid-time", sizeof(timebuf));
    }

    printf("reports.dat info:\n");
    printf("  Permissions: %s\n", perm);
    printf("  Size: %lld bytes\n", (long long)st.st_size);
    printf("  Last modified: %s\n", timebuf);
    printf("========================================\n");

    fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("open reports.dat");
        return 0;
    }

    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        print_report(&r);
    }

    close(fd);
    log_action(ctx, district, "list");
    return 1;
}

static int cmd_view(const Context *ctx, const char *district, int report_id) {
    char path[PATH_MAX];
    int fd;
    Report r;

    make_path(path, sizeof(path), district, REPORTS_FILE);

    if (!require_simulated_read(path, ctx)) return 0;

    fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("open reports.dat");
        return 0;
    }

    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        if (r.report_id == report_id) {
            print_report(&r);
            close(fd);
            log_action(ctx, district, "view");
            return 1;
        }
    }

    close(fd);
    fprintf(stderr, "Report %d not found in district %s\n", report_id, district);
    return 0;
}

static int cmd_remove_report(const Context *ctx, const char *district, int report_id) {
    char path[PATH_MAX];
    struct stat st;
    int fd;
    off_t filesize, nrecords;
    Report curr, next;
    int found = 0;
    off_t pos;

    if (!require_manager_only(ctx, "remove_report")) return 0;

    make_path(path, sizeof(path), district, REPORTS_FILE);

    if (!require_simulated_write(path, ctx)) return 0;

    fd = open(path, O_RDWR);
    if (fd == -1) {
        perror("open reports.dat");
        return 0;
    }

    if (stat(path, &st) == -1) {
        perror("stat reports.dat");
        close(fd);
        return 0;
    }

    filesize = st.st_size;
    nrecords = filesize / (off_t)sizeof(Report);

    for (off_t i = 0; i < nrecords; i++) {
        pos = i * (off_t)sizeof(Report);

        if (lseek(fd, pos, SEEK_SET) == (off_t)-1) {
            perror("lseek");
            close(fd);
            return 0;
        }

        if (read(fd, &curr, sizeof(curr)) != (ssize_t)sizeof(curr)) {
            perror("read");
            close(fd);
            return 0;
        }

        if (curr.report_id == report_id) {
            found = 1;

            for (off_t j = i + 1; j < nrecords; j++) {
                off_t src = j * (off_t)sizeof(Report);
                off_t dst = (j - 1) * (off_t)sizeof(Report);

                if (lseek(fd, src, SEEK_SET) == (off_t)-1) {
                    perror("lseek src");
                    close(fd);
                    return 0;
                }

                if (read(fd, &next, sizeof(next)) != (ssize_t)sizeof(next)) {
                    perror("read next");
                    close(fd);
                    return 0;
                }

                if (lseek(fd, dst, SEEK_SET) == (off_t)-1) {
                    perror("lseek dst");
                    close(fd);
                    return 0;
                }

                if (write(fd, &next, sizeof(next)) != (ssize_t)sizeof(next)) {
                    perror("write shifted");
                    close(fd);
                    return 0;
                }
            }

            if (ftruncate(fd, filesize - (off_t)sizeof(Report)) == -1) {
                perror("ftruncate");
                close(fd);
                return 0;
            }

            close(fd);
            printf("Removed report %d\n", report_id);
            log_action(ctx, district, "remove_report");
            return 1;
        }
    }

    close(fd);
    fprintf(stderr, "Report %d not found\n", report_id);
    return 0;
}