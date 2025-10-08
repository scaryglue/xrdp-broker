#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <nng/nng.h>
#include <nng/protocol/pubsub0/sub.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/supplemental/util/platform.h>
#include <jansson.h>
#include <unistd.h>


typedef struct {
    char server[64];
    int sessions;
    float cpu;
    float mem;
    char **users;
    int user_count;
    time_t last_seen;
    float performance;
} server_status;

#define MAX_SERVERS 32
#define TIMEOUT 20

static server_status servers[MAX_SERVERS];
static int server_count = 0;
static char **conf_addresses = NULL;
static int conf_server = 0;

typedef struct {
    char user[64];
    time_t redirected_at;
} redirected_user;

#define MAX_REDIRECTED 128
static redirected_user redirected[MAX_REDIRECTED];
static int redirected_count = 0;

int is_redirected(const char *user)
{
    for(int i=0; i < redirected_count; i++)
    {
        if(strcmp(redirected[i].user, user) == 0)
        {
                // Remove this entry
                redirected[i] = redirected[redirected_count - 1];
                redirected_count--;
                return 1;
        }
    }
    return 0;
}

// Setzt einen Redirect-Lock
void set_redirected(const char *user)
{
    if(redirected_count < MAX_REDIRECTED)
    {
        strncpy(redirected[redirected_count].user, user, sizeof(redirected[redirected_count].user));
        redirected[redirected_count].redirected_at = time(NULL);
        redirected_count++;
    }
}

void free_users(server_status *s)
{
    if(s->users)
    {
        for(int i = 0; i < s->user_count; i++)
        {
            free(s->users[i]);
        }
        free(s->users);
        s->users = NULL;
    }
}

int find_server(const char *name)
{
    for(int i=0; i < server_count; i++)
    {
        if(strcmp(servers[i].server, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

int user_on_server(const server_status *s, const char *user)
{
    for(int i = 0; i < s->user_count; i++)
    {
        if(strcmp(s->users[i], user) == 0)
        {
            return 1;
        }
    }
    return 0;
}

void update_server(const char *json_str)
{
    json_error_t err;
    json_t *root = json_loads(json_str, 0, &err);

    if(!root)
    {
        return;
    }
    
    const char *server = json_string_value(json_object_get(root, "server"));
    int sessions = json_integer_value(json_object_get(root, "sessions"));
    double cpu = json_real_value(json_object_get(root, "cpu"));
    double mem = json_real_value(json_object_get(root, "mem"));
    json_t *users_arr = json_object_get(root, "users");

    int temp = find_server(server);
    if(temp == -1 && server_count < MAX_SERVERS)
    {
        temp = server_count++;
    }

    server_status *s = &servers[temp];

    strncpy(s->server, server, sizeof(s->server));
    s->sessions = sessions;
    s->cpu = cpu;
    s->mem = mem;
    s->last_seen = time(NULL);

    free_users(s);
    s->user_count = json_array_size(users_arr);
    s->users = calloc(s->user_count, sizeof(char *));

    for(int i=0; i < s->user_count; i++)
    {
        const char *u = json_string_value(json_array_get(users_arr, i));
        s->users[i] = strdup(u);
    }

    json_decref(root);
}

const char *choose_server(const char *user)
{
    time_t now = time(NULL);


    int best = -1;
    for(int i = 0; i < server_count; i++)
    {
        servers[i]. performance = servers[i].cpu + servers[i].mem + servers[i].sessions;
        printf("server %s: Performance value: %f\n", servers[i].server, servers[i].performance);
        if(difftime(now, servers[i].last_seen) < TIMEOUT)
        {
            if(best == -1 || servers[i].performance < servers[best].performance)
            {
                best = i;
            }
        }
    }
    if(best != -1)
    {
        return servers[best].server;
    }
    
    // existing session
    for(int i=0; i < server_count; i++)
    {
        if(difftime(now, servers[i].last_seen) < TIMEOUT)
        {
            if(user_on_server(&servers[i], user))
            {
                return servers[i].server;
            }
        }
    }

    //take host with fewest sessions, temp
    
    for(int i = 0; i < server_count; i++)
    {
        if(difftime(now, servers[i].last_seen) < TIMEOUT)
        {
            if(best == -1 || servers[i].sessions < servers[best].sessions)
            {
                best = i;
            }
        }
    }

    if(best != -1)
    {
        return servers[best].server;
    }
    return NULL;
}
int read_conf()
{
    FILE *f = fopen("broker.conf", "r");
    if(!f)
    {
        fprintf(stderr, "Could not open broker.conf\n");
        return -1;
    }
    char line[128];
    while(fgets(line, sizeof(line), f))
    {
        //remove newline
        line[strcspn(line, "\r\n")] = 0;
        printf("Read line: %s\n", line);

        char *addr = strdup(line);
        if (!addr) {
            perror("strdup");
            break;
        }

        char **tmp = realloc(conf_addresses, (conf_server + 1) * sizeof(*conf_addresses));
        if (!tmp) {
            perror("realloc");
            free(addr);
            break;
        }
        conf_addresses = tmp;
        conf_addresses[conf_server++] = addr;

    }

    fclose(f);
    return 0;
}

int main()
{
    nng_socket sub, rep;
    int rv;

    //Agents
    rv = nng_sub0_open(&sub);
    if(rv != 0)
    {
        fprintf(stderr, "sub open, %s\n", nng_strerror(rv));
        return 1;
    }
    nng_socket_set(sub, NNG_OPT_SUB_SUBSCRIBE, "", 0);

    read_conf();

    for(int i = 0; i < conf_server; i++)
    {
        rv = nng_dial(sub, conf_addresses[i], NULL, 0);
        if(rv != 0)
        {
            fprintf(stderr, "sub dial, %s\n", nng_strerror(rv));
            return 1;
        }
        else
        {
            printf("successfully connected to %s\n", conf_addresses[i]);
        }
    }

    //req rep for xrdp
    rv = nng_rep0_open(&rep);
    if(rv != 0)
    {
        fprintf(stderr, "rep open, %s\n", nng_strerror(rv));
        return 1;
    }

    rv = nng_listen(rep, "tcp://[::]:6002", NULL, 0);
    if(rv != 0)
    {
        fprintf(stderr, "rep listen, %s\n", nng_strerror(rv));
        return 1;
    }

    printf("broker listening\n");

    while(1)
    {
        char *buf = NULL;
        size_t size;
        //agents on sub
        rv = nng_recv(sub, &buf, &size, NNG_FLAG_ALLOC | NNG_FLAG_NONBLOCK);
        if(rv == 0)
        {
            printf("Got update from agent:\n");
            printf("%s\n", buf);
            update_server(buf);
            nng_free(buf, size);
        }

        //req from xrdp
        char *req = NULL;
        rv = nng_recv(rep ,&req, &size, NNG_FLAG_NONBLOCK | NNG_FLAG_ALLOC);
        if(rv == 0)
        {
            printf("Got request from xrdp: %s\n", req);
            const char *user = req;
            char reply[128];
            if(is_redirected(user))
            {
                snprintf(reply, sizeof(reply), "{\"host\":\"0\"}");
            }
            else
            {
            const char *s = choose_server(user);
            if(s)
            {
                set_redirected(user);
                snprintf(reply, sizeof(reply), "{\"host\":\"%s\"}", s);
            }
            else
            {
                snprintf(reply, sizeof(reply), "{\"error\":\"no host available\"}");
            }
            }
            printf("Replying to xrdp: %s\n", reply);
            nng_send(rep, reply, strlen(reply) + 1, 0);
            nng_free(req, size);
        }

    usleep(100000);
    }
}
