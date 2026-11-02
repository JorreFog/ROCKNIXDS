/* The Store runs rocknixds-store in a thread (store/src/job.c) and shows its lines:
   STEP while the job runs, then DONE <version>, OK, OFFLINE, or FAIL <why>.
   A wrong reading installs nothing and tells the person it worked, or starts a
   second job over the one that is still writing the card. */
#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failed;
static char dir[128], script[160], logpath[160], linespath[160], go[160], go2[160];

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "fail: %s\n", msg);
        failed = 1;
    }
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        exit(1);
    }
    fputs(text, f);
    fclose(f);
}

static int count_log(void)
{
    FILE *f = fopen(logpath, "r");
    int n = 0, c;
    if (!f) return 0;
    while ((c = fgetc(f)) != EOF)
        if (c == '\n') n++;
    fclose(f);
    return n;
}

static void gates_close(void)
{
    unlink(go);
    unlink(go2);
}

static void gates_open(void)
{
    write_file(go, "go\n");
    write_file(go2, "go\n");
}

static int spin(int (*ready)(void))
{
    for (int i = 0; i < 500; i++) {
        if (ready()) return 1;
        usleep(10000);
    }
    gates_open();
    job_wait();
    return 0;
}

static int log_at;
static int log_grew(void) { return count_log() > log_at; }

static char seen_step[160];
static int step_is(void)
{
    char step[160];
    int st = job_state(step, sizeof step, 0, 0);
    return st == JOB_RUNNING && strcmp(step, seen_step) == 0;
}

static void prepare(const char *text)
{
    gates_close();
    write_file(linespath, text);
    log_at = count_log();
}

static int started(void)
{
    if (!spin(log_grew)) {
        expect(0, "the job never started");
        return 0;
    }
    return 1;
}

static const char *job_name(int st)
{
    switch (st) {
    case JOB_IDLE: return "idle";
    case JOB_RUNNING: return "running";
    case JOB_DONE: return "done";
    case JOB_FAILED: return "failed";
    default: return "?";
    }
}

static void finish(const char *name, int want, const char *step, const char *what, const char *args)
{
    char got_step[160], got_what[96];
    gates_open();
    job_wait();
    int st = job_state(got_step, sizeof got_step, got_what, sizeof got_what);
    if (st != want) {
        fprintf(stderr, "fail: %s: state %s, want %s (step \"%s\")\n", name, job_name(st), job_name(want), got_step);
        failed = 1;
    }
    if (strcmp(got_step, step) != 0) {
        fprintf(stderr, "fail: %s: step \"%s\", want \"%s\"\n", name, got_step, step);
        failed = 1;
    }
    if (what && strcmp(got_what, what) != 0) {
        fprintf(stderr, "fail: %s: title \"%s\", want \"%s\"\n", name, got_what, what);
        failed = 1;
    }
    if (args && strcmp(job_args(), args) != 0) {
        fprintf(stderr, "fail: %s: args \"%s\", want \"%s\"\n", name, job_args(), args);
        failed = 1;
    }
    if (st != JOB_RUNNING) job_ack();
}

static void test_answers(void)
{
    char step[160], what[96];

    expect(job_state(0, 0, 0, 0) == JOB_IDLE, "idle before any job");
    expect(job_state(step, sizeof step, what, sizeof what) == JOB_IDLE, "null buffers are optional");
    expect(step[0] == 0 && what[0] == 0, "idle step and title are empty");

    prepare("");
    job_start("refresh", "Checking");
    expect(started(), "empty answer starts");
    finish("empty answer", JOB_FAILED, "It didn't run", "Checking", "refresh");

    prepare("hello\n");
    job_start("refresh", "Checking");
    expect(started(), "noise starts");
    finish("noise", JOB_FAILED, "It didn't run", "Checking", "refresh");

    prepare("OK\n");
    job_start("refresh", "Checking");
    expect(started(), "OK starts");
    finish("OK", JOB_DONE, "", "Checking", "refresh");

    prepare("OK\nstill here\n");
    job_start("refresh", "Checking");
    expect(started(), "trailing noise starts");
    finish("trailing noise", JOB_DONE, "", "Checking", "refresh");

    prepare("OKAY\n");
    job_start("refresh", "Checking");
    expect(started(), "OKAY starts");
    finish("OKAY", JOB_FAILED, "It didn't run", "Checking", "refresh");

    prepare("OFFLINE\n");
    job_start("refresh", "Checking");
    expect(started(), "OFFLINE starts");
    finish("OFFLINE", JOB_FAILED, "No network", "Checking", "refresh");

    prepare("OFFLINE \r\n");
    job_start("refresh", "Checking");
    expect(started(), "OFFLINE crlf starts");
    finish("OFFLINE crlf", JOB_FAILED, "No network", "Checking", "refresh");

    prepare("OFFLINE!\n");
    job_start("refresh", "Checking");
    expect(started(), "OFFLINE! starts");
    finish("OFFLINE!", JOB_FAILED, "It didn't run", "Checking", "refresh");

    prepare("FAIL No release yet\n");
    job_start("install ereaderds", "Installing");
    expect(started(), "no release starts");
    finish("no release yet", JOB_FAILED, "No release yet", "Installing", "install ereaderds");

    prepare("FAIL\n");
    job_start("install bank", "Installing");
    expect(started(), "bare FAIL starts");
    finish("bare FAIL", JOB_FAILED, "It failed", "Installing", "install bank");

    prepare("FAIL \n");
    job_start("install bank", "Installing");
    expect(started(), "FAIL space starts");
    finish("FAIL space", JOB_FAILED, "It failed", "Installing", "install bank");

    prepare("DONE\n");
    job_start("remove bank", "Removing");
    expect(started(), "DONE without version starts");
    finish("remove with nothing installed", JOB_DONE, "", "Removing", "remove bank");

    prepare("DONE 1.10.0\r\n");
    job_start("install bank", "Installing");
    expect(started(), "DONE version starts");
    finish("DONE version", JOB_DONE, "1.10.0", "Installing", "install bank");

    prepare("FAIL Download damaged\nDONE 1.10.0\n");
    job_start("install bank", "Installing");
    expect(started(), "the line it ends on starts");
    finish("the line it ends on", JOB_DONE, "1.10.0", "Installing", "install bank");

    char long_fail[240];
    memset(long_fail, 'r', sizeof long_fail - 1);
    long_fail[sizeof long_fail - 1] = 0;
    char long_line[260];
    snprintf(long_line, sizeof long_line, "FAIL %s\n", long_fail);
    prepare(long_line);
    job_start("install bank", "Installing");
    expect(started(), "long FAIL starts");
    gates_open();
    job_wait();
    int st = job_state(step, sizeof step, 0, 0);
    expect(st == JOB_FAILED, "long FAIL fails");
    expect(strlen(step) == sizeof step - 1, "long FAIL is cut to the step buffer");
    expect(strncmp(step, long_fail, strlen(step)) == 0, "long FAIL keeps the start of the reason");
    job_ack();
}

static void test_one_at_a_time(void)
{
    char step[160], what[96];
    prepare("STEP Downloading\nSTEP Installing\nDONE 1.2.3\n");
    job_start("install bank", "Installing Bank");
    expect(started(), "step job starts");
    int st = job_state(step, sizeof step, what, sizeof what);
    expect(st == JOB_RUNNING, "the job is running before its first line");
    expect(strcmp(step, "Starting") == 0, "a step shows only once the command says it");
    expect(strcmp(what, "Installing Bank") == 0, "the title stays");

    write_file(go, "go\n");
    snprintf(seen_step, sizeof seen_step, "Downloading");
    expect(spin(step_is), "the download step shows while it is still running");
    int running = count_log();
    job_ack();
    expect(job_state(0, 0, 0, 0) == JOB_RUNNING, "seeing the box does not clear a running job");
    job_start("remove bank", "Removing Bank");
    expect(count_log() == running, "a second job waits");
    expect(strcmp(job_args(), "install bank") == 0, "the running arguments stay");
    job_state(0, 0, what, sizeof what);
    expect(strcmp(what, "Installing Bank") == 0, "the running title stays");

    finish("one job at a time", JOB_DONE, "1.2.3", "Installing Bank", "install bank");
    expect(count_log() == running, "the second job never ran");
}

int main(void)
{
    char tmpl[] = "/tmp/rnds job XXXXXX";
    if (!mkdtemp(tmpl)) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(dir, sizeof dir, "%s", tmpl);
    snprintf(script, sizeof script, "%s/linejob.py", dir);
    snprintf(logpath, sizeof logpath, "%s/log", dir);
    snprintf(linespath, sizeof linespath, "%s/lines", dir);
    snprintf(go, sizeof go, "%s/go", dir);
    snprintf(go2, sizeof go2, "%s/go2", dir);

    char src[512];
    snprintf(src, sizeof src, "%s/tests/linejob.py", getenv("ROCKNIXDS_ROOT") ? getenv("ROCKNIXDS_ROOT") : ".");
    FILE *in = fopen(src, "r");
    if (!in) {
        fprintf(stderr, "fail: cannot read %s (run from the repo, or set ROCKNIXDS_ROOT)\n", src);
        return 1;
    }
    FILE *out = fopen(script, "w");
    if (!out) {
        perror(script);
        return 1;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    if (chmod(script, 0755) != 0) {
        perror(script);
        return 1;
    }
    setenv("LINEJOB_LOG", logpath, 1);
    setenv("LINEJOB_LINES", linespath, 1);
    setenv("LINEJOB_GO", go, 1);
    setenv("LINEJOB_GO2", go2, 1);
    setenv("RNDS_STORE_CLI", script, 1);
    write_file(logpath, "");

    test_answers();
    test_one_at_a_time();

    gates_open();
    job_wait();
    unlink(script);
    unlink(logpath);
    unlink(linespath);
    unlink(go);
    unlink(go2);
    rmdir(dir);
    if (failed) fprintf(stderr, "job_lines_test: failed\n");
    else printf("job_lines_test: ok\n");
    return failed ? 1 : 0;
}
