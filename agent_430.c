#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <sys/statvfs.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#define TCP_PORT 9410
#define UDP_PORT 9411

#define SID "SID:0340"
#define REG_NO "IT24100430"
#define AUTH_TOKEN "OPS-0430"

#define STORAGE_DIR "agentfiles/IT24100430"

#define BUFFER_SIZE 4096
#define LINE_BUFFER_SIZE 8192
#define MAX_FILE_SIZE (10LL * 1024LL * 1024LL)

#define MONITOR_INTERVAL 2

/* =========================================================
   SEND ALL
   ========================================================= */

int send_all(int socket_fd, const void *data, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(socket_fd,
                            (const char *)data + total_sent,
                            length - total_sent,
                            0);

        if (sent <= 0)
        {
            return -1;
        }

        total_sent += sent;
    }

    return 0;
}

/* =========================================================
   SEND RESPONSE
   ========================================================= */

int send_response(int client_fd, const char *message)
{
    return send_all(client_fd, message, strlen(message));
}

/* =========================================================
   RECEIVE EXACT NUMBER OF BYTES
   ========================================================= */

int recv_all(int socket_fd, void *data, size_t length)
{
    size_t total_received = 0;

    while (total_received < length)
    {
        ssize_t received = recv(socket_fd,
                                (char *)data + total_received,
                                length - total_received,
                                0);

        if (received <= 0)
        {
            return -1;
        }

        total_received += received;
    }

    return 0;
}

/* =========================================================
   SYSINFO VALUES
   ========================================================= */

void get_sysinfo(char *output, size_t output_size)
{
    FILE *fp;
    char line[256];

    double cpu_load = 0.0;
    long total_mem = 0;
    long available_mem = 0;
    double uptime = 0.0;

    /* CPU load */

    fp = fopen("/proc/loadavg", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf", &cpu_load);
        fclose(fp);
    }

    /* Memory */

    fp = fopen("/proc/meminfo", "r");

    if (fp != NULL)
    {
        while (fgets(line, sizeof(line), fp) != NULL)
        {
            if (sscanf(line,
                       "MemTotal: %ld kB",
                       &total_mem) == 1)
            {
                continue;
            }

            if (sscanf(line,
                       "MemAvailable: %ld kB",
                       &available_mem) == 1)
            {
                continue;
            }
        }

        fclose(fp);
    }

    long used_mem = total_mem - available_mem;

    /* Uptime */

    fp = fopen("/proc/uptime", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf", &uptime);
        fclose(fp);
    }

    snprintf(output,
             output_size,
             "SYSINFO CPU_LOAD:%.2f MEM_USED:%ldKB MEM_TOTAL:%ldKB UPTIME:%.0fS %s\n",
             cpu_load,
             used_mem,
             total_mem,
             uptime,
             SID);
}

/* =========================================================
   SYSINFO TCP RESPONSE
   ========================================================= */

void handle_sysinfo(int client_fd)
{
    char response[1024];

    get_sysinfo(response, sizeof(response));

    char final_response[1100];

    snprintf(final_response,
             sizeof(final_response),
             "OK %s",
             response);

    send_response(client_fd, final_response);
}

/* =========================================================
   LIST PROCESSES
   ========================================================= */

void handle_listproc(int client_fd)
{
    FILE *fp;
    char line[256];

    fp = popen("ps -e -o pid,comm --no-headers", "r");

    if (fp == NULL)
    {
        send_response(client_fd,
                      "ERR 003 LISTPROC_FAILED SID:0340\n");
        return;
    }

    send_response(client_fd,
                  "OK PROCS SID:0340\n");

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        send_all(client_fd,
                 line,
                 strlen(line));
    }

    pclose(fp);

    send_response(client_fd,
                  "END PROCS SID:0340\n");
}

/* =========================================================
   EXEC COMMANDS
   ========================================================= */

void handle_exec(int client_fd, const char *command)
{
    char response[1024];

    /* DATE */

    if (strcmp(command, "EXEC DATE\n") == 0)
    {
        time_t now = time(NULL);

        struct tm *local_time = localtime(&now);

        char date_string[128];

        strftime(date_string,
                 sizeof(date_string),
                 "%Y-%m-%d %H:%M:%S",
                 local_time);

        snprintf(response,
                 sizeof(response),
                 "OK EXEC_RESULT DATE:%s %s\n",
                 date_string,
                 SID);

        send_response(client_fd, response);
    }

    /* UPTIME */

    else if (strcmp(command, "EXEC UPTIME\n") == 0)
    {
        FILE *fp;

        double uptime = 0.0;

        fp = fopen("/proc/uptime", "r");

        if (fp != NULL)
        {
            fscanf(fp, "%lf", &uptime);
            fclose(fp);
        }

        snprintf(response,
                 sizeof(response),
                 "OK EXEC_RESULT UPTIME:%.0fSEC %s\n",
                 uptime,
                 SID);

        send_response(client_fd, response);
    }

    /* DISKFREE */

    else if (strcmp(command, "EXEC DISKFREE\n") == 0)
    {
        struct statvfs fs;

        if (statvfs(".", &fs) == 0)
        {
            unsigned long long free_space =
                (unsigned long long)fs.f_bavail *
                (unsigned long long)fs.f_frsize;

            unsigned long long free_mb =
                free_space / (1024 * 1024);

            snprintf(response,
                     sizeof(response),
                     "OK EXEC_RESULT DISKFREE:%lluMB %s\n",
                     free_mb,
                     SID);

            send_response(client_fd, response);
        }
        else
        {
            send_response(client_fd,
                          "ERR 006 DISKFREE_FAILED SID:0340\n");
        }
    }

    /* HOSTNAME */

    else if (strcmp(command, "EXEC HOSTNAME\n") == 0)
    {
        char hostname[256];

        if (gethostname(hostname, sizeof(hostname)) == 0)
        {
            hostname[sizeof(hostname) - 1] = '\0';

            snprintf(response,
                     sizeof(response),
                     "OK EXEC_RESULT HOSTNAME:%s %s\n",
                     hostname,
                     SID);

            send_response(client_fd, response);
        }
        else
        {
            send_response(client_fd,
                          "ERR 007 HOSTNAME_FAILED SID:0340\n");
        }
    }

    /* WHOAMI */

    else if (strcmp(command, "EXEC WHOAMI\n") == 0)
    {
        struct passwd *pw;

        pw = getpwuid(getuid());

        if (pw != NULL)
        {
            snprintf(response,
                     sizeof(response),
                     "OK EXEC_RESULT WHOAMI:%s %s\n",
                     pw->pw_name,
                     SID);

            send_response(client_fd, response);
        }
        else
        {
            send_response(client_fd,
                          "ERR 008 WHOAMI_FAILED SID:0340\n");
        }
    }

    /* EVERYTHING ELSE IS REJECTED */

    else
    {
        send_response(client_fd,
                      "ERR 002 COMMAND_NOT_ALLOWED SID:0340\n");

        printf("EXEC command rejected.\n");
    }
}

/* =========================================================
   PUT FILE
   ========================================================= */

void handle_put(int client_fd,
                char *command)
{
    char filename[256];
    long long file_size;

    if (sscanf(command,
               "PUT %255s %lld",
               filename,
               &file_size) != 2)
    {
        send_response(client_fd,
                      "ERR 003 INVALID_PUT SID:0340\n");
        return;
    }

    if (file_size < 0)
    {
        send_response(client_fd,
                      "ERR 003 INVALID_PUT_SIZE SID:0340\n");
        return;
    }

    if (file_size > MAX_FILE_SIZE)
    {
        send_response(client_fd,
                      "ERR 004 FILE_TOO_LARGE SID:0340\n");
        return;
    }

    /* Prevent directory traversal */

    if (strstr(filename, "..") != NULL ||
        strchr(filename, '/') != NULL ||
        strchr(filename, '\\') != NULL)
    {
        send_response(client_fd,
                      "ERR 003 INVALID_FILENAME SID:0340\n");
        return;
    }

    char filepath[512];

    snprintf(filepath,
             sizeof(filepath),
             "%s/%s",
             STORAGE_DIR,
             filename);

    FILE *fp = fopen(filepath, "wb");

    if (fp == NULL)
    {
        send_response(client_fd,
                      "ERR 003 FILE_CREATE_FAILED SID:0340\n");
        return;
    }

    /*
     * PUT protocol:
     *
     * Controller sends:
     * PUT filename size\n
     * immediately followed by raw bytes.
     *
     * No intermediate response is sent.
     */

    char buffer[BUFFER_SIZE];

    long long remaining = file_size;

    while (remaining > 0)
    {
        size_t chunk_size =
            remaining > BUFFER_SIZE
                ? BUFFER_SIZE
                : (size_t)remaining;

        ssize_t received =
            recv(client_fd,
                 buffer,
                 chunk_size,
                 0);

        if (received <= 0)
        {
            fclose(fp);
            remove(filepath);

            printf("PUT failed during transfer.\n");

            return;
        }

        size_t written =
            fwrite(buffer,
                   1,
                   received,
                   fp);

        if (written != (size_t)received)
        {
            fclose(fp);
            remove(filepath);

            send_response(client_fd,
                          "ERR 003 FILE_WRITE_FAILED SID:0340\n");

            return;
        }

        remaining -= received;
    }

    fclose(fp);

    send_response(client_fd,
                  "OK FILE_RECEIVED SID:0340\n");

    printf("PUT completed: %s (%lld bytes)\n",
           filename,
           file_size);
}

/* =========================================================
   GET FILE
   ========================================================= */

void handle_get(int client_fd,
                char *command)
{
    char filename[256];

    if (sscanf(command,
               "GET %255s",
               filename) != 1)
    {
        send_response(client_fd,
                      "ERR 003 INVALID_GET SID:0340\n");
        return;
    }

    if (strstr(filename, "..") != NULL ||
        strchr(filename, '/') != NULL ||
        strchr(filename, '\\') != NULL)
    {
        send_response(client_fd,
                      "ERR 003 INVALID_FILENAME SID:0340\n");
        return;
    }

    char filepath[512];

    snprintf(filepath,
             sizeof(filepath),
             "%s/%s",
             STORAGE_DIR,
             filename);

    FILE *fp = fopen(filepath, "rb");

    if (fp == NULL)
    {
        send_response(client_fd,
                      "ERR 005 FILE_NOT_FOUND SID:0340\n");
        return;
    }

    fseek(fp, 0, SEEK_END);

    long long file_size = ftell(fp);

    fseek(fp, 0, SEEK_SET);

    /*
     * Include file size so Controller knows
     * exactly how many raw bytes to receive.
     */

    char response[512];

    snprintf(response,
             sizeof(response),
             "OK FILE_SEND SIZE:%lld SID:0340\n",
             file_size);

    if (send_response(client_fd, response) < 0)
    {
        fclose(fp);
        return;
    }

    char buffer[BUFFER_SIZE];

    long long remaining = file_size;

    while (remaining > 0)
    {
        size_t chunk_size =
            remaining > BUFFER_SIZE
                ? BUFFER_SIZE
                : (size_t)remaining;

        size_t bytes_read =
            fread(buffer,
                  1,
                  chunk_size,
                  fp);

        if (bytes_read == 0)
        {
            break;
        }

        if (send_all(client_fd,
                     buffer,
                     bytes_read) < 0)
        {
            fclose(fp);
            return;
        }

        remaining -= bytes_read;
    }

    fclose(fp);

    printf("GET completed: %s (%lld bytes)\n",
           filename,
           file_size);
}

/* =========================================================
   UDP MONITOR THREAD
   ========================================================= */

typedef struct
{
    int client_fd;
    struct sockaddr_in controller_addr;
    volatile int *monitoring;

} MonitorData;

void *monitor_thread(void *arg)
{
    MonitorData *data = (MonitorData *)arg;

    int udp_fd;

    udp_fd = socket(AF_INET,
                    SOCK_DGRAM,
                    0);

    if (udp_fd < 0)
    {
        perror("UDP socket");
        free(data);
        return NULL;
    }

    printf("UDP monitoring thread started.\n");

    while (*(data->monitoring))
    {
        char message[1024];

        get_sysinfo(message,
                    sizeof(message));

        sendto(udp_fd,
               message,
               strlen(message),
               0,
               (struct sockaddr *)&data->controller_addr,
               sizeof(data->controller_addr));

        sleep(MONITOR_INTERVAL);
    }

    close(udp_fd);

    printf("UDP monitoring thread stopped.\n");

    free(data);

    return NULL;
}

/* =========================================================
   CLIENT THREAD
   ========================================================= */

typedef struct
{
    int client_fd;
    struct sockaddr_in client_addr;

} ClientData;

void *handle_client(void *arg)
{
    ClientData *client_data =
        (ClientData *)arg;

    int client_fd =
        client_data->client_fd;

    struct sockaddr_in client_addr =
        client_data->client_addr;

    free(client_data);

    int authenticated = 0;

    volatile int monitoring = 0;

    pthread_t monitor_tid;

    int monitor_running = 0;

    char buffer[LINE_BUFFER_SIZE];

    printf("Controller connected. Thread started.\n");

    while (1)
    {
        memset(buffer,
               0,
               sizeof(buffer));

        ssize_t bytes_received =
            recv(client_fd,
                 buffer,
                 sizeof(buffer) - 1,
                 0);

        if (bytes_received <= 0)
        {
            break;
        }

        buffer[bytes_received] = '\0';

        printf("Received: %s",
               buffer);

        /* AUTH */

        if (strcmp(buffer,
                   "AUTH OPS-0430\n") == 0)
        {
            authenticated = 1;

            send_response(client_fd,
                          "OK AUTHENTICATED SID:0340\n");

            printf("Authentication successful.\n");
        }

        /* SYSINFO */

        else if (strcmp(buffer,
                        "SYSINFO\n") == 0)
        {
            if (!authenticated)
            {
                send_response(client_fd,
                              "ERR 001 AUTH_REQUIRED SID:0340\n");
            }
            else
            {
                handle_sysinfo(client_fd);

                printf("SYSINFO sent.\n");
            }
        }

        /* LISTPROC */

        else if (strcmp(buffer,
                        "LISTPROC\n") == 0)
        {
            if (!authenticated)
            {
                send_response(client_fd,
                              "ERR 001 AUTH_REQUIRED SID:0340\n");
            }
            else
            {
                handle_listproc(client_fd);

                printf("LISTPROC sent.\n");
            }
        }

        /* EXEC */

        else if (strncmp(buffer,
                         "EXEC ",
                         5) == 0)
        {
            if (!authenticated)
            {
                send_response(client_fd,
                              "ERR 001 AUTH_REQUIRED SID:0340\n");
            }
            else
            {
                handle_exec(client_fd,
                            buffer);

                printf("EXEC processed.\n");
            }
        }

        /* PUT */

        else if (strncmp(buffer,
                         "PUT ",
                         4) == 0)
        {
            if (!authenticated)
            {
                send_response(client_fd,
                              "ERR 001 AUTH_REQUIRED SID:0340\n");
            }
            else
            {
                handle_put(client_fd,
                           buffer);
            }
        }

        /* GET */

        else if (strncmp(buffer,
                         "GET ",
                         4) == 0)
        {
            if (!authenticated)
            {
                send_response(client_fd,
                              "ERR 001 AUTH_REQUIRED SID:0340\n");
            }
            else
            {
                handle_get(client_fd,
                           buffer);
            }
        }

        /* MONITOR START */

        else if (strcmp(buffer,
                        "MONITOR START\n") == 0)
        {
            if (!authenticated)
            {
                send_response(client_fd,
                              "ERR 001 AUTH_REQUIRED SID:0340\n");
            }
            else if (monitor_running)
            {
                send_response(client_fd,
                              "OK MONITOR_STARTED SID:0340\n");
            }
            else
            {
                monitoring = 1;

                MonitorData *monitor_data =
                    malloc(sizeof(MonitorData));

                if (monitor_data == NULL)
                {
                    monitoring = 0;

                    send_response(client_fd,
                                  "ERR 003 MONITOR_START_FAILED SID:0340\n");
                }
                else
                {
                    monitor_data->client_fd =
                        client_fd;

                    monitor_data->controller_addr =
                        client_addr;

                    monitor_data->controller_addr.sin_port =
                        htons(UDP_PORT);

                    monitor_data->monitoring =
                        &monitoring;

                    if (pthread_create(&monitor_tid,
                                       NULL,
                                       monitor_thread,
                                       monitor_data) != 0)
                    {
                        monitoring = 0;

                        free(monitor_data);

                        send_response(client_fd,
                                      "ERR 003 MONITOR_START_FAILED SID:0340\n");
                    }
                    else
                    {
                        monitor_running = 1;

                        pthread_detach(monitor_tid);

                        send_response(client_fd,
                                      "OK MONITOR_STARTED SID:0340\n");

                        printf("MONITOR START: UDP port %d\n",
                               UDP_PORT);
                    }
                }
            }
        }

        /* MONITOR STOP */

        else if (strcmp(buffer,
                        "MONITOR STOP\n") == 0)
        {
            if (!authenticated)
            {
                send_response(client_fd,
                              "ERR 001 AUTH_REQUIRED SID:0340\n");
            }
            else
            {
                monitoring = 0;

                monitor_running = 0;

                send_response(client_fd,
                              "OK MONITOR_STOPPED SID:0340\n");

                printf("MONITOR STOP received.\n");
            }
        }

        /* QUIT */

        else if (strcmp(buffer,
                        "QUIT\n") == 0)
        {
            monitoring = 0;

            send_response(client_fd,
                          "OK BYE SID:0340\n");

            printf("QUIT received.\n");

            break;
        }

        /* UNKNOWN */

        else
        {
            if (!authenticated)
            {
                send_response(client_fd,
                              "ERR 001 AUTH_REQUIRED SID:0340\n");
            }
            else
            {
                send_response(client_fd,
                              "ERR 002 COMMAND_NOT_ALLOWED SID:0340\n");

                printf("Command not allowed.\n");
            }
        }
    }

    monitoring = 0;

    close(client_fd);

    printf("Controller disconnected. Thread finished.\n");

    return NULL;
}

/* =========================================================
   MAIN
   ========================================================= */

int main(void)
{
    int server_fd;

    struct sockaddr_in server_addr;

    server_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int opt = 1;

    setsockopt(server_fd,
               SOL_SOCKET,
               SO_REUSEADDR,
               &opt,
               sizeof(opt));

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(TCP_PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");

        close(server_fd);

        return 1;
    }

    if (listen(server_fd, 5) < 0)
    {
        perror("listen");

        close(server_fd);

        return 1;
    }

    /* Create personalized storage directory */

    mkdir("agentfiles", 0755);

    mkdir(STORAGE_DIR, 0755);

    printf("====================================\n");
    printf("       RemoteOps Agent\n");
    printf("====================================\n");

    printf("Registration : %s\n",
           REG_NO);

    printf("TCP Port     : %d\n",
           TCP_PORT);

    printf("UDP Port     : %d\n",
           UDP_PORT);

    printf("SID          : %s\n",
           SID);

    printf("Storage      : %s\n",
           STORAGE_DIR);

    printf("Concurrency  : pthread\n");

    printf("Monitor      : Every %d seconds\n",
           MONITOR_INTERVAL);

    printf("Status       : Listening...\n");

    printf("====================================\n");

    while (1)
    {
        struct sockaddr_in client_addr;

        socklen_t client_len =
            sizeof(client_addr);

        int client_fd =
            accept(server_fd,
                   (struct sockaddr *)&client_addr,
                   &client_len);

        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        ClientData *client_data =
            malloc(sizeof(ClientData));

        if (client_data == NULL)
        {
            perror("malloc");

            close(client_fd);

            continue;
        }

        client_data->client_fd =
            client_fd;

        client_data->client_addr =
            client_addr;

        pthread_t thread_id;

        if (pthread_create(&thread_id,
                           NULL,
                           handle_client,
                           client_data) != 0)
        {
            perror("pthread_create");

            close(client_fd);

            free(client_data);

            continue;
        }

        pthread_detach(thread_id);

        printf("New client thread created.\n");
    }

    close(server_fd);

    return 0;
}
