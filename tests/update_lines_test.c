/* Döda Kvarter reads update.sh in a thread (dodakvarter/src/update.c). check answers
   UPDATE <version>, UPTODATE <version>, or OFFLINE. install says STEP <what> as it
   goes and ends with DONE <version> or FAIL <why>. The title screen offers whatever
   version came back, and a press installs only from an offer or a failure. A second
   run while one is going would overlap two copies writing the same files. */
#include "update.h"

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

static const char *upd_name(int st)
{
    switch (st) {
    case UPD_UNSUPPORTED: return "unsupported";
    case UPD_IDLE: return "idle";
    case UPD_CHECKING: return "checking";
    case UPD_LATEST: return "latest";
    case UPD_OFFLINE: return "offline";
    case UPD_AVAILABLE: return "available";
    case UPD_INSTALLING: return "installing";
    case UPD_DONE: return "done";
    case UPD_FAILED: return "failed";
    default: return "?";
    }
}

static int log_at;
static int log_grew(void) { return count_log() > log_at; }

static int spin_log(void)
{
    for (int i = 0; i < 500; i++) {
        if (log_grew()) return 1;
        usleep(10000);
    }
    gates_open();
    return 0;
}

static char seen_step[48];
static int step_is(void)
{
    char step[48];
    int st = update_state(0, 0, step, sizeof step);
    return st == UPD_INSTALLING && strcmp(step, seen_step) == 0;
}

static int spin_step(void)
{
    for (int i = 0; i < 500; i++) {
        if (step_is()) return 1;
        usleep(10000);
    }
    gates_open();
    return 0;
}

static void prepare(const char *text)
{
    gates_close();
    write_file(linespath, text);
    log_at = count_log();
}

static void settle(const char *name, int want, const char *ver, const char *step)
{
    char got_ver[64], got_step[64];
    int st = UPD_CHECKING;
    gates_open();
    /* The answer is read only after the gate opens, so the previous result is still
       showing until the command gets past CHECKING or INSTALLING. */
    for (int i = 0; i < 500; i++) {
        st = update_state(got_ver, sizeof got_ver, got_step, sizeof got_step);
        if (st != UPD_CHECKING && st != UPD_INSTALLING) break;
        usleep(10000);
        st = -1;
    }
    if (st == -1) {
        fprintf(stderr, "fail: %s: still running\n", name);
        failed = 1;
        return;
    }
    if (st != want) {
        fprintf(stderr, "fail: %s: state %s, want %s (version \"%s\", step \"%s\")\n",
                name, upd_name(st), upd_name(want), got_ver, got_step);
        failed = 1;
    }
    if (ver && strcmp(got_ver, ver) != 0) {
        fprintf(stderr, "fail: %s: version \"%s\", want \"%s\"\n", name, got_ver, ver);
        failed = 1;
    }
    if (step && strcmp(got_step, step) != 0) {
        fprintf(stderr, "fail: %s: step \"%s\", want \"%s\"\n", name, got_step, step);
        failed = 1;
    }
}

static void check(const char *name, const char *text, int want, const char *ver)
{
    prepare(text);
    update_check();
    expect(spin_log(), name);
    settle(name, want, ver, 0);
}

int main(void)
{
    char tmpl[] = "/tmp/rnds upd XXXXXX";
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
    write_file(logpath, "");
    unsetenv("DK_UPDATER");

    char ver[24] = "x", step[48] = "y";
    expect(update_state(0, 0, 0, 0) == UPD_UNSUPPORTED, "no updater is unsupported, and null buffers are safe");
    expect(update_state(ver, sizeof ver, step, sizeof step) == UPD_UNSUPPORTED, "no updater");
    expect(ver[0] == 0 && step[0] == 0, "no updater clears the text");
    int before = count_log();
    update_check();
    update_install();
    expect(count_log() == before, "without an updater nothing is run");

    setenv("DK_UPDATER", script, 1);
    expect(update_state(0, 0, 0, 0) == UPD_IDLE, "an updater that has not been asked is idle");
    update_install();
    expect(count_log() == before, "install does nothing until a version is offered or a try failed");

    check("garbage", "hello\n", UPD_OFFLINE, 0);
    check("up to date, no version", "UPTODATE\n", UPD_LATEST, "");
    check("up to date", "UPTODATE 0.3.0\n", UPD_LATEST, "0.3.0");
    before = count_log();
    update_install();
    expect(count_log() == before, "up to date does not install");
    check("UPDATE without a version is not an offer", "UPDATE\n", UPD_OFFLINE, "0.3.0");

    prepare("UPDATE 1.10.0\r\n");
    update_check();
    expect(spin_log(), "the offer starts");
    before = count_log();
    expect(update_state(0, 0, 0, 0) == UPD_CHECKING, "still checking while the updater has not answered");
    update_check();
    update_install();
    expect(count_log() == before, "a second check, or an install, waits for this one");
    settle("an offer", UPD_AVAILABLE, "1.10.0", 0);

    char big[80], lines[160];
    memset(big, 's', 70);
    big[70] = 0;
    snprintf(lines, sizeof lines, "STEP %s\nFAIL Download damaged\n", big);
    prepare(lines);
    update_install();
    expect(spin_log(), "the failed install starts");
    expect(update_state(0, 0, step, sizeof step) == UPD_INSTALLING, "installing before the first step");
    expect(strcmp(step, "Starting") == 0, "the row says Starting until a step arrives");
    write_file(go, "go\n");
    memcpy(seen_step, big, 47);
    seen_step[47] = 0;
    expect(spin_step(), "a long step is shown, cut to the row, while it is still running");
    before = count_log();
    update_check();
    update_install();
    expect(count_log() == before, "nothing else starts over an install");
    settle("download damaged", UPD_FAILED, "1.10.0", "Download damaged");

    check("look again after a failure", "UPTODATE 1.10.0\n", UPD_LATEST, "1.10.0");

    char offer[64];
    memset(offer, 'b', 40);
    offer[40] = 0;
    snprintf(lines, sizeof lines, "UPDATE %s\n", offer);
    prepare(lines);
    update_check();
    expect(spin_log(), "a long version starts");
    settle("a long version", UPD_AVAILABLE, 0, 0);
    update_state(ver, sizeof ver, 0, 0);
    expect(strlen(ver) == 23, "a long version is cut to the buffer");
    expect(strncmp(ver, offer, 23) == 0, "a long version keeps its start");

    prepare("");
    update_install();
    expect(spin_log(), "a silent install starts");
    settle("silent install", UPD_FAILED, 0, "the update didn't run");

    prepare("UPDATE 0.4.0\n");
    update_check();
    expect(spin_log(), "0.4.0 starts");
    settle("offered 0.4.0", UPD_AVAILABLE, "0.4.0", 0);

    prepare("STEP Downloading\nSTEP Installing\nDONE 0.4.0\n");
    update_install();
    expect(spin_log(), "the install starts");
    write_file(go, "go\n");
    snprintf(seen_step, sizeof seen_step, "Downloading");
    expect(spin_step(), "Downloading shows before the updater finishes");
    before = count_log();
    update_check();
    update_install();
    expect(count_log() == before, "done is one install");
    settle("installed", UPD_DONE, "0.4.0", 0);

    before = count_log();
    update_check();
    update_install();
    expect(update_state(0, 0, 0, 0) == UPD_DONE, "after it is in, the row stays on restarting");
    expect(count_log() == before, "after it is in, nothing checks or installs again");

    gates_open();
    unlink(script);
    unlink(logpath);
    unlink(linespath);
    unlink(go);
    unlink(go2);
    rmdir(dir);
    if (failed) fprintf(stderr, "update_lines_test: failed\n");
    else printf("update_lines_test: ok\n");
    return failed ? 1 : 0;
}
