#include <nng/nng.h>
#include <nng/protocol/pubsub0/pub.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
static bool read_line(FILE *fp, char *buf, size_t sz) {
    if (!fgets(buf, (int)sz, fp)) return false;
    size_t n = strlen(buf);
    if (n && buf[n-1] == '\n') buf[n-1] = '\0';
    return true;
}

typedef struct {
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
} cpu_times_t;

static bool read_proc_stat(cpu_times_t *t) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return false;
    // first line: cpu  user nice system idle iowait irq softirq steal ...
    char label[8];
    int rc = fscanf(fp, "%7s %llu %llu %llu %llu %llu %llu %llu %llu",
                    label, &t->user, &t->nice, &t->system, &t->idle, &t->iowait,
                    &t->irq, &t->softirq, &t->steal);
    fclose(fp);
    return rc >= 9 && strcmp(label, "cpu") == 0;
}

static double cpu_percent(void) {
    static bool have_prev = false;
    static cpu_times_t prev = {0};
    cpu_times_t cur;
    if (!read_proc_stat(&cur)) return -1.0;

    if (!have_prev)
    {
        have_prev = true;
        prev = cur;
        usleep(200 * 1000);
        if (!read_proc_stat(&cur)) return -1.0;
    }

    unsigned long long idle_cur = cur.idle + cur.iowait;
    unsigned long long idle_prev = prev.idle + prev.iowait;
    unsigned long long non_idle_cur = cur.user + cur.nice + cur.system + cur.irq + cur.softirq + cur.steal;
    unsigned long long non_idle_prev = prev.user + prev.nice + prev.system + prev.irq + prev.softirq + prev.steal;

    unsigned long long total_cur = idle_cur + non_idle_cur;
    unsigned long long total_prev = idle_prev + non_idle_prev;

    unsigned long long totald = total_cur - total_prev;
    unsigned long long idled  = idle_cur  - idle_prev;

    prev = cur;

    if (totald == 0) return 0.0;
    double usage = (double)(totald - idled) * 100.0 / (double)totald;
    if (usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;
    return usage;
}

static double mem_percent(void) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1.0;
    char line[256];
    long long mem_total_kb = -1, mem_avail_kb = -1;

    while (read_line(fp, line, sizeof(line)))
    {
        if (sscanf(line, "MemTotal: %lld kB", &mem_total_kb) == 1) continue;
        if (sscanf(line, "MemAvailable: %lld kB", &mem_avail_kb) == 1) continue;
        if (mem_total_kb > 0 && mem_avail_kb >= 0) break;
    }
    fclose(fp);
    if (mem_total_kb <= 0 || mem_avail_kb < 0) return -1.0;
    long long used = mem_total_kb - mem_avail_kb;
    double pct = (double)used * 100.0 / (double)mem_total_kb;
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    return pct;
}

typedef struct {
    char **items;
    size_t len, cap;
} strvec_t;

static void sv_init(strvec_t *v) { v->items=NULL; v->len=0; v->cap=0; }
static void sv_free(strvec_t *v) { for(size_t i=0;i<v->len;i++) free(v->items[i]); free(v->items); }
static bool sv_contains(const strvec_t *v, const char *s) {
    for (size_t i=0;i<v->len;i++) if (strcmp(v->items[i], s)==0) return true;
    return false;
}
static void sv_push_unique(strvec_t *v, const char *s)
{
    if (s==NULL || *s=='\0') return;
    if (sv_contains(v, s)) return;
    if (v->len == v->cap)
    {
        v->cap = v->cap ? v->cap*2 : 8;
        v->items = (char**)realloc(v->items, v->cap * sizeof(char*));
    }
    v->items[v->len++] = strdup(s);
}

static void collect_users_loginctl(strvec_t *out)
{
    FILE *fp = popen("loginctl list-sessions --no-legend --no-pager 2>/dev/null", "r");
    if (!fp) return;
    char line[512];
    while (read_line(fp, line, sizeof(line))) {
        // expected:  <SESSION> <UID> <USER> <SEAT> ...
        // tokenize by whitespace
        char *saveptr = NULL;
        char *tok = strtok_r(line, " \t", &saveptr); // session
        tok = strtok_r(NULL, " \t", &saveptr);       // uid
        tok = strtok_r(NULL, " \t", &saveptr);       // user
        if (tok && *tok) sv_push_unique(out, tok);
    }
    pclose(fp);
}

static int gather_sessions(strvec_t *out)
{
    sv_init(out);

    collect_users_loginctl(out);

    return (int)out->len;
}

static void json_escape(const char *in, char *out, size_t outsz) {
    // tiny escape for quotes and backslashes
    size_t j = 0;
    for (size_t i=0; in[i] && j+2 < outsz; i++) {
        char c = in[i];
        if (c == '\"' || c == '\\') out[j++]='\\';
        out[j++] = c;
    }
    out[j] = '\0';
}



int main(int argc, char **argv)
{
    if(argc < 3)
    {
        fprintf(stderr, "Usage: %s <broker_address:sub_port> <hostname>\n", argv[0]);
        return 1;
    }

    char dial_addr[128];
    snprintf(dial_addr, sizeof(dial_addr), "tcp://%s", argv[1]);

    const char *hostname = argv[2];
    nng_socket sock;
    int rv;

    rv = nng_pub0_open(&sock);
    if(rv != 0)
    {
        fprintf(stderr, "pub open, %s", nng_strerror(rv));
        return 1;
    }

    rv = nng_listen(sock, "tcp://[::]:6001", NULL, 0);
    if(rv != 0)
    {
        fprintf(stderr, "pub listen, %s", nng_strerror(rv));
        return 1;
    }

    srand(time(NULL));

    while(1)
    {
        float cpu = cpu_percent();
        float mem = mem_percent();;

        strvec_t users;
        int sessions = gather_sessions(&users);

        char json[4096];
        char host_esc[256];
        json_escape(hostname, host_esc, sizeof(host_esc));
        size_t off = 0;
        off += snprintf(json+off, sizeof(json)-off,
                        "{\"server\":\"%s\",\"cpu\":%.1f,\"mem\":%.1f,\"sessions\":%d,\"users\":[",
                        host_esc, cpu < 0 ? 0.0 : cpu, mem < 0 ? 0.0 : mem, sessions);
        for (size_t i=0; i<users.len; i++) {
            char uesc[256]; json_escape(users.items[i], uesc, sizeof(uesc));
            off += snprintf(json+off, sizeof(json)-off, "%s\"%s\"", (i? ",":""), uesc);
        }
        off += snprintf(json+off, sizeof(json)-off, "]}");

        // Send
        rv = nng_send(sock, json, strlen(json)+1, 0);
        if (rv != 0) {
            fprintf(stderr, "nng_send: %s\n", nng_strerror(rv));
        } else {
            printf("[agent:%s] %s\n", hostname, json);
        }
        sv_free(&users);
        sleep(3);
    }

    
}