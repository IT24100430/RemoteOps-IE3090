#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

#define SERVER_IP "127.0.0.1"
#define TCP_PORT 9410
#define UDP_PORT 9411

#define BUFFER_SIZE 4096
#define SID "SID:0340"
#define AUTH_TOKEN "OPS-0430"

int send_all(int sock, const void *data, size_t length)
{
    size_t total = 0;

    while (total < length) {
        ssize_t sent = send(sock,
                            (const char *)data + total,
                            length - total,
                            0);

        if (sent <= 0)
            return -1;

        total += sent;
    }

    return 0;
}

int receive_line(int sock, char *buffer, size_t size)
{
    size_t index = 0;

    while (index < size - 1) {
        char ch;

        ssize_t received = recv(sock, &ch, 1, 0);

        if (received <= 0)
            return -1;

        buffer[index++] = ch;

        if (ch == '\n')
            break;
    }

    buffer[index] = '\0';

    return 0;
}

int receive_all(int sock, void *data, size_t length)
{
    size_t total = 0;

    while (total < length) {
        ssize_t received = recv(sock,
                                (char *)data + total,
                                length - total,
                                0);

        if (received <= 0)
            return -1;

        total += received;
    }

    return 0;
}

void send_command(int sock, const char *command)
{
    if (send_all(sock, command, strlen(command)) < 0) {
        perror("send");
        return;
    }

    printf("Sent: %s", command);
}

/* =========================
   PUT FILE
   ========================= */

int upload_file(int sock, const char *filename)
{
    FILE *fp = fopen(filename, "rb");

    if (fp == NULL) {
        perror("Cannot open upload file");
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    printf("\nUploading file: %s\n", filename);
    printf("File size: %lld bytes\n", file_size);

    char command[512];

    snprintf(command,
             sizeof(command),
             "PUT %s %lld\n",
             filename,
             file_size);

    /*
     * New protocol:
     * PUT header followed immediately by raw bytes.
     */

    if (send_all(sock, command, strlen(command)) < 0) {
        fclose(fp);
        return -1;
    }

    printf("Sent: %s", command);

    char buffer[BUFFER_SIZE];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {

        if (send_all(sock, buffer, bytes_read) < 0) {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    char response[1024];

    if (receive_line(sock, response, sizeof(response)) < 0)
        return -1;

    printf("Agent: %s", response);

    if (strstr(response, "OK FILE_RECEIVED") == NULL) {
        printf("PUT failed.\n");
        return -1;
    }

    printf("PUT completed successfully.\n");

    return 0;
}

/* =========================
   GET FILE
   ========================= */

int download_file(int sock,
                  const char *remote_filename,
                  const char *local_filename)
{
    char command[512];

    snprintf(command,
             sizeof(command),
             "GET %s\n",
             remote_filename);

    send_command(sock, command);

    char response[1024];

    if (receive_line(sock, response, sizeof(response)) < 0)
        return -1;

    printf("Agent: %s", response);

    long long file_size;

    if (sscanf(response,
               "OK FILE_SEND SIZE:%lld",
               &file_size) != 1) {

        printf("GET failed.\n");
        return -1;
    }

    printf("File size: %lld bytes\n", file_size);

    FILE *fp = fopen(local_filename, "wb");

    if (fp == NULL) {
        perror("Cannot create local file");
        return -1;
    }

    char buffer[BUFFER_SIZE];

    long long remaining = file_size;

    while (remaining > 0) {

        size_t chunk_size;

        if (remaining > BUFFER_SIZE)
            chunk_size = BUFFER_SIZE;
        else
            chunk_size = (size_t)remaining;

        if (receive_all(sock, buffer, chunk_size) < 0) {
            fclose(fp);
            return -1;
        }

        fwrite(buffer, 1, chunk_size, fp);

        remaining -= chunk_size;
    }

    fclose(fp);

    printf("File downloaded successfully: %s (%lld bytes)\n",
           local_filename,
           file_size);

    return 0;
}

/* =========================
   MONITOR UDP
   ========================= */

int start_monitor(int tcp_sock, int udp_sock)
{
    char response[1024];

    printf("\n====================================\n");
    printf("         MONITOR START TEST\n");
    printf("====================================\n");

    send_command(tcp_sock, "MONITOR START\n");

    if (receive_line(tcp_sock, response, sizeof(response)) < 0)
        return -1;

    printf("Agent: %s", response);

    if (strstr(response, "OK MONITOR_STARTED") == NULL) {
        printf("Monitor start failed.\n");
        return -1;
    }

    printf("\nReceiving UDP monitoring packets...\n");
    printf("(Waiting for 3 packets)\n\n");

    for (int i = 1; i <= 3; i++) {

        char udp_buffer[BUFFER_SIZE];

        ssize_t received = recvfrom(udp_sock,
                                    udp_buffer,
                                    sizeof(udp_buffer) - 1,
                                    0,
                                    NULL,
                                    NULL);

        if (received < 0) {
            perror("recvfrom");
            return -1;
        }

        udp_buffer[received] = '\0';

        printf("UDP Packet %d:\n", i);
        printf("%s\n", udp_buffer);
    }

    printf("\n====================================\n");
    printf("         MONITOR STOP TEST\n");
    printf("====================================\n");

    send_command(tcp_sock, "MONITOR STOP\n");

    if (receive_line(tcp_sock, response, sizeof(response)) < 0)
        return -1;

    printf("Agent: %s", response);

    if (strstr(response, "OK MONITOR_STOPPED") == NULL) {
        printf("Monitor stop failed.\n");
        return -1;
    }

    printf("Monitor stopped successfully.\n");

    return 0;
}

/* =========================
   MAIN
   ========================= */

int main(void)
{
    int tcp_sock;
    int udp_sock;

    struct sockaddr_in server_addr;
    struct sockaddr_in udp_addr;

    char buffer[BUFFER_SIZE];

    /* =========================
       TCP SOCKET
       ========================= */

    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (tcp_sock < 0) {
        perror("TCP socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);

    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) <= 0) {

        perror("inet_pton");
        close(tcp_sock);
        return 1;
    }

    if (connect(tcp_sock,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {

        perror("TCP connect");
        close(tcp_sock);
        return 1;
    }

    printf("====================================\n");
    printf("       RemoteOps Controller\n");
    printf("====================================\n");
    printf("Connected to Agent.\n");
    printf("Agent IP  : %s\n", SERVER_IP);
    printf("TCP Port  : %d\n", TCP_PORT);
    printf("UDP Port  : %d\n", UDP_PORT);
    printf("SID       : %s\n", SID);
    printf("====================================\n\n");


    /* =========================
       UDP SOCKET
       ========================= */

    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (udp_sock < 0) {
        perror("UDP socket");
        close(tcp_sock);
        return 1;
    }

    memset(&udp_addr, 0, sizeof(udp_addr));

    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    udp_addr.sin_port = htons(UDP_PORT);

    if (bind(udp_sock,
             (struct sockaddr *)&udp_addr,
             sizeof(udp_addr)) < 0) {

        perror("UDP bind");
        close(udp_sock);
        close(tcp_sock);
        return 1;
    }

    printf("UDP socket bound to port %d.\n\n", UDP_PORT);


    /* =========================
       AUTH
       ========================= */

    send_command(tcp_sock, "AUTH " AUTH_TOKEN "\n");

    if (receive_line(tcp_sock,
                     buffer,
                     sizeof(buffer)) < 0)
        goto cleanup;

    printf("Agent: %s", buffer);


    /* =========================
       SYSINFO
       ========================= */

    send_command(tcp_sock, "SYSINFO\n");

    if (receive_line(tcp_sock,
                     buffer,
                     sizeof(buffer)) < 0)
        goto cleanup;

    printf("Agent: %s", buffer);


    /* =========================
       LISTPROC
       ========================= */

    send_command(tcp_sock, "LISTPROC\n");

    printf("\nRunning processes:\n");

    while (1) {

        if (receive_line(tcp_sock,
                         buffer,
                         sizeof(buffer)) < 0)
            goto cleanup;

        printf("%s", buffer);

        if (strstr(buffer, "END PROCS") != NULL)
            break;
    }


    /* =========================
       EXEC TESTS
       ========================= */

    const char *commands[] = {
        "EXEC DATE\n",
        "EXEC UPTIME\n",
        "EXEC DISKFREE\n",
        "EXEC HOSTNAME\n",
        "EXEC WHOAMI\n",
        "EXEC LS\n"
    };

    int command_count =
        sizeof(commands) / sizeof(commands[0]);

    printf("\n====================================\n");
    printf("           EXEC TESTS\n");
    printf("====================================\n");

    for (int i = 0; i < command_count; i++) {

        send_command(tcp_sock, commands[i]);

        if (receive_line(tcp_sock,
                         buffer,
                         sizeof(buffer)) < 0)
            goto cleanup;

        printf("Agent: %s", buffer);
    }


    /* =========================
       PUT TEST
       ========================= */

    printf("\n====================================\n");
    printf("           PUT FILE TEST\n");
    printf("====================================\n");

    if (upload_file(tcp_sock, "testfile.txt") != 0) {
        printf("PUT test failed.\n");
        goto cleanup;
    }


    /* =========================
       GET TEST
       ========================= */

    printf("\n====================================\n");
    printf("           GET FILE TEST\n");
    printf("====================================\n");

    if (download_file(tcp_sock,
                      "testfile.txt",
                      "downloaded_testfile.txt") != 0) {

        printf("GET test failed.\n");
        goto cleanup;
    }


    /* =========================
       MONITOR TEST
       ========================= */

    if (start_monitor(tcp_sock, udp_sock) != 0) {
        printf("MONITOR test failed.\n");
        goto cleanup;
    }


    /* =========================
       QUIT
       ========================= */

    printf("\n====================================\n");
    printf("             QUIT TEST\n");
    printf("====================================\n");

    send_command(tcp_sock, "QUIT\n");

    if (receive_line(tcp_sock,
                     buffer,
                     sizeof(buffer)) < 0)
        goto cleanup;

    printf("Agent: %s", buffer);

cleanup:

    close(udp_sock);
    close(tcp_sock);

    printf("\nDisconnected from Agent.\n");

    return 0;
}
