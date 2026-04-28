#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

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

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define SIGUSR1 10
#else
#include <signal.h>
#include <sys/wait.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define REPORTS_FILE "reports.dat"
#define CFG_FILE "district.cfg"
#define LOG_FILE "logged_district"

#define DISTRICT_MODE 0750
#define REPORTS_MODE 0664
#define CFG_MODE 0640
#define LOG_MODE 0644

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
} Report;

typedef struct {
    char role[16];
    char user[NAME_LEN];
} Context;

static void trim_newline(char *s) {
    size_t n;
    if (!s) return;
    n = strlen(s);
    if (n > 0 && s[n - 1] == '\n') s[n - 1] = '\0';
}

static void safe_copy(char *dst, const char *src, size_t n) {
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

static void make_path(char *out, size_t outsz, const char *district, const char *name) {
    snprintf(out, outsz, "%s/%s", district, name);
}

static void make_symlink_name(char *out, size_t outsz, const char *district) {
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

static int require_simulated_read(const char *path, const Context *ctx) {
    struct stat st;
    mode_t m;

    if (stat(path, &st) == -1) {
        perror(path);
        return 0;
    }

    m = st.st_mode;

    if (is_manager(ctx)) {
        if (!(m & S_IRUSR)) {
            fprintf(stderr, "access denied: manager lacks owner-read on %s\n", path);
            return 0;
        }
    } else if (is_inspector(ctx)) {
        if (!(m & S_IRGRP)) {
            fprintf(stderr, "access denied: inspector lacks group-read on %s\n", path);
            return 0;
        }
    } else {
        fprintf(stderr, "unknown role: %s\n", ctx->role);
        return 0;
    }

    return 1;
}

static int require_simulated_write(const char *path, const Context *ctx) {
    struct stat st;
    mode_t m;

    if (stat(path, &st) == -1) {
        perror(path);
        return 0;
    }

    m = st.st_mode;

    if (is_manager(ctx)) {
        if (!(m & S_IWUSR)) {
            fprintf(stderr, "access denied: manager lacks owner-write on %s\n", path);
            return 0;
        }
    } else if (is_inspector(ctx)) {
        if (!(m & S_IWGRP)) {
            fprintf(stderr, "access denied: inspector lacks group-write on %s\n", path);
            return 0;
        }
    } else {
        fprintf(stderr, "unknown role: %s\n", ctx->role);
        return 0;
    }

    return 1;
}

static int require_manager_only(const Context *ctx, const char *op_name) {
    if (!is_manager(ctx)) {
        fprintf(stderr, "operation '%s' is manager-only\n", op_name);
        return 0;
    }

    return 1;
}

static void warn_if_dangling_symlink(const char *district) {
#ifdef _WIN32
    (void)district;
#else
    char linkname[PATH_MAX];
    struct stat lst;
    struct stat st;

    make_symlink_name(linkname, sizeof(linkname), district);

    if (lstat(linkname, &lst) == -1) {
        return;
    }

    if (!S_ISLNK(lst.st_mode)) {
        return;
    }

    if (stat(linkname, &st) == -1) {
        fprintf(stderr, "warning: dangling symlink detected: %s\n", linkname);
    }
#endif
}

static int create_or_update_symlink(const char *district) {
#ifdef _WIN32
    char linkname[PATH_MAX];
    char content[PATH_MAX];
    int fd;

    make_symlink_name(linkname, sizeof(linkname), district);
    snprintf(content, sizeof(content), "%s/%s\n", district, REPORTS_FILE);

    fd = open(linkname, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open active_reports file");
        return 0;
    }

    write(fd, content, strlen(content));
    close(fd);
    return 1;
#else
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
#endif
}

static int ensure_district_exists(const Context *ctx, const char *district) {
    struct stat st;
    char path[PATH_MAX];
    int fd;

    if (stat(district, &st) == -1) {
        if (!is_manager(ctx)) {
            fprintf(stderr, "district '%s' does not exist. only manager may initialize it\n", district);
            return 0;
        }

#ifdef _WIN32
        if (mkdir(district) == -1) {
#else
        if (mkdir(district, DISTRICT_MODE) == -1) {
#endif
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
    }
    chmod(path, REPORTS_MODE);

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
    }
    chmod(path, CFG_MODE);

    make_path(path, sizeof(path), district, LOG_FILE);
    if (stat(path, &st) == -1) {
        fd = open(path, O_CREAT | O_RDWR, LOG_MODE);
        if (fd == -1) {
            perror("open logged_district");
            return 0;
        }
        close(fd);
    }
    chmod(path, LOG_MODE);

    if (!create_or_update_symlink(district)) {
        return 0;
    }

    warn_if_dangling_symlink(district);
    return 1;
}

static void log_action(const Context *ctx, const char *district, const char *action) {
    char path[PATH_MAX];
    char line[512];
    time_t now = time(NULL);
    int fd;

    make_path(path, sizeof(path), district, LOG_FILE);

    if (!require_simulated_write(path, ctx)) {
        fprintf(stderr, "warning: action completed, but logging refused\n");
        return;
    }

    fd = open(path, O_WRONLY | O_APPEND);
    if (fd == -1) {
        perror("open log");
        return;
    }

    snprintf(line, sizeof(line), "%ld | role=%s | user=%s | %s\n",
             (long)now, ctx->role, ctx->user, action);

    write(fd, line, strlen(line));
    close(fd);
}

static int notify_monitor(void) {
#ifdef _WIN32
    return 0;
#else
    int fd;
    char buf[64];
    ssize_t n;
    pid_t pid;

    fd = open(".monitor_pid", O_RDONLY);
    if (fd == -1) {
        return 0;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) {
        return 0;
    }

    buf[n] = '\0';
    pid = (pid_t)atoi(buf);

    if (pid <= 0) {
        return 0;
    }

    if (kill(pid, SIGUSR1) == -1) {
        return 0;
    }

    return 1;
#endif
}

static int read_threshold(const Context *ctx, const char *district, int *value) {
    char path[PATH_MAX];
    char buf[128];
    int fd;
    ssize_t n;

    make_path(path, sizeof(path), district, CFG_FILE);

    if (!require_simulated_read(path, ctx)) return 0;

    fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("open district.cfg");
        return 0;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        perror("read district.cfg");
        close(fd);
        return 0;
    }

    buf[n] = '\0';
    close(fd);

    if (sscanf(buf, "threshold=%d", value) != 1) {
        fprintf(stderr, "invalid config file format\n");
        return 0;
    }

    return 1;
}

static int write_threshold(const Context *ctx, const char *district, int value) {
    char path[PATH_MAX];
    char buf[64];
    int fd;
    int len;

    make_path(path, sizeof(path), district, CFG_FILE);

    if (!require_manager_only(ctx, "update_threshold")) return 0;

#ifndef _WIN32
    if (!exact_mode_matches(path, CFG_MODE)) {
        fprintf(stderr, "refusing update: district.cfg permissions are not exactly 640\n");
        return 0;
    }
#else
    (void)exact_mode_matches;
#endif

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

static int next_report_id(int fd) {
    Report r;
    int max_id = 0;

    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
        perror("lseek");
        return 1;
    }

    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        if (r.report_id > max_id) {
            max_id = r.report_id;
        }
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
    fflush(stdout);

    if (!fgets(buf, (int)sz, stdin)) {
        return 0;
    }

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

    if (!read_line_prompt("Category: ", temp, sizeof(temp))) return 0;
    safe_copy(r->category, temp, sizeof(r->category));

    if (!read_line_prompt("Severity (1-3): ", temp, sizeof(temp))) return 0;
    r->severity = atoi(temp);

    if (r->severity < 1 || r->severity > 3) {
        fprintf(stderr, "invalid severity\n");
        return 0;
    }

    if (!read_line_prompt("Description: ", temp, sizeof(temp))) return 0;
    safe_copy(r->description, temp, sizeof(r->description));

    return 1;
}

static int cmd_add(const Context *ctx, const char *district) {
    char path[PATH_MAX];
    int fd;
    int threshold;
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

    if (notify_monitor()) {
        log_action(ctx, district, "add - monitor notified with SIGUSR1");
    } else {
        log_action(ctx, district, "add - monitor could not be informed");
    }

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
    fprintf(stderr, "report %d not found\n", report_id);
    return 0;
}

static int cmd_remove_report(const Context *ctx, const char *district, int report_id) {
    char path[PATH_MAX];
    struct stat st;
    int fd;
    off_t filesize;
    off_t nrecords;
    Report curr;
    Report next;

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
        off_t pos = i * (off_t)sizeof(Report);

        lseek(fd, pos, SEEK_SET);

        if (read(fd, &curr, sizeof(curr)) != (ssize_t)sizeof(curr)) {
            perror("read");
            close(fd);
            return 0;
        }

        if (curr.report_id == report_id) {
            for (off_t j = i + 1; j < nrecords; j++) {
                off_t src = j * (off_t)sizeof(Report);
                off_t dst = (j - 1) * (off_t)sizeof(Report);

                lseek(fd, src, SEEK_SET);
                read(fd, &next, sizeof(next));

                lseek(fd, dst, SEEK_SET);
                write(fd, &next, sizeof(next));
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
    fprintf(stderr, "report %d not found\n", report_id);
    return 0;
}

static int valid_district_name(const char *district) {
    if (!district || strlen(district) == 0) return 0;
    if (strcmp(district, ".") == 0) return 0;
    if (strcmp(district, "..") == 0) return 0;
    if (strchr(district, '/') != NULL) return 0;
    if (strchr(district, '\\') != NULL) return 0;
    return 1;
}

static int cmd_remove_district(const Context *ctx, const char *district) {
    char linkname[PATH_MAX];

    if (!require_manager_only(ctx, "remove_district")) return 0;

    if (!valid_district_name(district)) {
        fprintf(stderr, "invalid district name. refusing to remove\n");
        return 0;
    }

    make_symlink_name(linkname, sizeof(linkname), district);
    unlink(linkname);

#ifdef _WIN32
    {
        char command[PATH_MAX + 32];
        snprintf(command, sizeof(command), "rmdir /s /q \"%s\"", district);

        if (system(command) != 0) {
            fprintf(stderr, "failed to remove district '%s'\n", district);
            return 0;
        }

        printf("District '%s' removed successfully.\n", district);
        return 1;
    }
#else
    {
        pid_t pid;
        int status;

        pid = fork();

        if (pid == -1) {
            perror("fork");
            return 0;
        }

        if (pid == 0) {
            execlp("rm", "rm", "-rf", district, (char *)NULL);
            perror("execlp rm");
            _exit(1);
        }

        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
            return 0;
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("District '%s' removed successfully.\n", district);
            return 1;
        }

        fprintf(stderr, "failed to remove district '%s'\n", district);
        return 0;
    }
#endif
}

int parse_condition(const char *input, char *field, char *op, char *value) {
    const char *first;
    const char *second;
    size_t len_field;
    size_t len_op;
    size_t len_value;

    if (!input || !field || !op || !value) return 0;

    first = strchr(input, ':');
    if (!first) return 0;

    second = strchr(first + 1, ':');
    if (!second) return 0;

    len_field = (size_t)(first - input);
    len_op = (size_t)(second - first - 1);
    len_value = strlen(second + 1);

    if (len_field == 0 || len_op == 0 || len_value == 0) return 0;

    strncpy(field, input, len_field);
    field[len_field] = '\0';

    strncpy(op, first + 1, len_op);
    op[len_op] = '\0';

    strcpy(value, second + 1);

    return 1;
}

static int compare_long(long a, const char *op, long b) {
    if (strcmp(op, "==") == 0) return a == b;
    if (strcmp(op, "!=") == 0) return a != b;
    if (strcmp(op, "<") == 0) return a < b;
    if (strcmp(op, "<=") == 0) return a <= b;
    if (strcmp(op, ">") == 0) return a > b;
    if (strcmp(op, ">=") == 0) return a >= b;
    return 0;
}

static int compare_str(const char *a, const char *op, const char *b) {
    int cmp = strcmp(a, b);

    if (strcmp(op, "==") == 0) return cmp == 0;
    if (strcmp(op, "!=") == 0) return cmp != 0;
    if (strcmp(op, "<") == 0) return cmp < 0;
    if (strcmp(op, "<=") == 0) return cmp <= 0;
    if (strcmp(op, ">") == 0) return cmp > 0;
    if (strcmp(op, ">=") == 0) return cmp >= 0;
    return 0;
}

int match_condition(Report *r, const char *field, const char *op, const char *value) {
    if (!r || !field || !op || !value) return 0;

    if (strcmp(field, "severity") == 0) {
        long v = strtol(value, NULL, 10);
        return compare_long((long)r->severity, op, v);
    }

    if (strcmp(field, "timestamp") == 0) {
        long v = strtol(value, NULL, 10);
        return compare_long((long)r->timestamp, op, v);
    }

    if (strcmp(field, "category") == 0) {
        return compare_str(r->category, op, value);
    }

    if (strcmp(field, "inspector") == 0) {
        return compare_str(r->inspector, op, value);
    }

    return 0;
}

static int cmd_filter(const Context *ctx, const char *district, int nconds, char **conds) {
    char path[PATH_MAX];
    int fd;
    Report r;
    int printed = 0;

    make_path(path, sizeof(path), district, REPORTS_FILE);

    if (!require_simulated_read(path, ctx)) return 0;

    fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("open reports.dat");
        return 0;
    }

    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        int ok = 1;

        for (int i = 0; i < nconds; i++) {
            char field[64];
            char op[16];
            char value[128];

            if (!parse_condition(conds[i], field, op, value)) {
                fprintf(stderr, "invalid condition format: %s\n", conds[i]);
                close(fd);
                return 0;
            }

            if (!match_condition(&r, field, op, value)) {
                ok = 0;
                break;
            }
        }

        if (ok) {
            print_report(&r);
            printed = 1;
        }
    }

    close(fd);

    if (!printed) {
        printf("No reports matched.\n");
    }

    log_action(ctx, district, "filter");
    return 1;
}

static int cmd_update_threshold(const Context *ctx, const char *district, int value) {
    if (!write_threshold(ctx, district, value)) return 0;

    printf("Threshold updated to %d\n", value);
    log_action(ctx, district, "update_threshold");
    return 1;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s --role <manager|inspector> --user <name> --add <district>\n"
            "  %s --role <manager|inspector> --user <name> --list <district>\n"
            "  %s --role <manager|inspector> --user <name> --view <district> <report_id>\n"
            "  %s --role <manager|inspector> --user <name> --filter <district> <cond1> [cond2 ...]\n"
            "  %s --role manager --user <name> --remove_report <district> <report_id>\n"
            "  %s --role manager --user <name> --remove_district <district>\n"
            "  %s --role manager --user <name> --update_threshold <district> <value>\n",
            prog, prog, prog, prog, prog, prog, prog);
}

static const char *normalize_command(const char *cmd) {
    if (strcmp(cmd, "--add") == 0 || strcmp(cmd, "add") == 0) return "add";
    if (strcmp(cmd, "--list") == 0 || strcmp(cmd, "list") == 0) return "list";
    if (strcmp(cmd, "--view") == 0 || strcmp(cmd, "view") == 0) return "view";
    if (strcmp(cmd, "--filter") == 0 || strcmp(cmd, "filter") == 0) return "filter";
    if (strcmp(cmd, "--remove_report") == 0 || strcmp(cmd, "remove_report") == 0) return "remove_report";
    if (strcmp(cmd, "--remove_district") == 0 || strcmp(cmd, "remove_district") == 0) return "remove_district";
    if (strcmp(cmd, "--update_threshold") == 0 || strcmp(cmd, "update_threshold") == 0) return "update_threshold";

    return NULL;
}

int main(int argc, char *argv[]) {
    Context ctx;
    const char *command = NULL;
    int i;

    memset(&ctx, 0, sizeof(ctx));

    if (argc < 6) {
        print_usage(argv[0]);
        return 1;
    }

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--role") == 0 && i + 1 < argc) {
            safe_copy(ctx.role, argv[++i], sizeof(ctx.role));
        } else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            safe_copy(ctx.user, argv[++i], sizeof(ctx.user));
        } else {
            command = normalize_command(argv[i]);
            if (command) {
                break;
            }
        }
    }

    if (ctx.role[0] == '\0' || ctx.user[0] == '\0' || !command) {
        print_usage(argv[0]);
        return 1;
    }

    if (!is_manager(&ctx) && !is_inspector(&ctx)) {
        fprintf(stderr, "invalid role. must be manager or inspector\n");
        return 1;
    }

    if (strcmp(command, "add") == 0) {
        if (i + 1 >= argc) {
            print_usage(argv[0]);
            return 1;
        }

        return cmd_add(&ctx, argv[i + 1]) ? 0 : 1;
    }

    if (strcmp(command, "list") == 0) {
        if (i + 1 >= argc) {
            print_usage(argv[0]);
            return 1;
        }

        return cmd_list(&ctx, argv[i + 1]) ? 0 : 1;
    }

    if (strcmp(command, "view") == 0) {
        if (i + 2 >= argc) {
            print_usage(argv[0]);
            return 1;
        }

        return cmd_view(&ctx, argv[i + 1], atoi(argv[i + 2])) ? 0 : 1;
    }

    if (strcmp(command, "remove_report") == 0) {
        if (i + 2 >= argc) {
            print_usage(argv[0]);
            return 1;
        }

        return cmd_remove_report(&ctx, argv[i + 1], atoi(argv[i + 2])) ? 0 : 1;
    }

    if (strcmp(command, "remove_district") == 0) {
        if (i + 1 >= argc) {
            print_usage(argv[0]);
            return 1;
        }

        return cmd_remove_district(&ctx, argv[i + 1]) ? 0 : 1;
    }

    if (strcmp(command, "update_threshold") == 0) {
        if (i + 2 >= argc) {
            print_usage(argv[0]);
            return 1;
        }

        return cmd_update_threshold(&ctx, argv[i + 1], atoi(argv[i + 2])) ? 0 : 1;
    }

    if (strcmp(command, "filter") == 0) {
        if (i + 2 >= argc) {
            print_usage(argv[0]);
            return 1;
        }

        return cmd_filter(&ctx, argv[i + 1], argc - (i + 2), &argv[i + 2]) ? 0 : 1;
    }

    print_usage(argv[0]);
    return 1;
}