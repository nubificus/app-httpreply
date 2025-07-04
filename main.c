#include <uk/plat/time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <uk/libparam.h>
#include <stdlib.h>

#define LISTEN_PORT 8080
#define BUFLEN 4096

static char recvbuf[BUFLEN];
//static char *boot_wall_time_str= "0";
//UK_LIBPARAM_PARAM(boot_wall_time_str, charp, "Boot wall time in nanoseconds as string");
uint64_t get_boot_wall_time_ns(char *ullstr) {
    return strtoull(ullstr, NULL, 10);
}

static uint64_t boot_time_ns = 0;

__attribute__((constructor(101)))
static void record_boot_time(void) {
    boot_time_ns = ukplat_monotonic_clock();
}

static const char reply[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
    "<head><title>Unikraft Boot Time Demo</title></head>"
    "<body>"
    "<h1>Hello from Unikraft!</h1>"
    "<h2>Boot time: <span style=\"color:green\">%lu ms</span></h2>"
    "<h2>Cold start latency: <span style=\"color:red\">%lu ms</span></h2>"
    "</body></html>";

const char* get_request_start_header(const char *request) {
    static char value[64];
    const char *header = "X-Request-Start: ";
    char buffer[BUFLEN], *line, *saveptr;

    strncpy(buffer, request, BUFLEN - 1);
    buffer[BUFLEN - 1] = '\0';

    line = strtok_r(buffer, "\r\n", &saveptr);
    while (line) {
        if (strncmp(line, header, strlen(header)) == 0) {
            const char *val_start = line + strlen(header);
            size_t length = strcspn(val_start, "\r\n");
            if (length >= sizeof(value)) return NULL;
            strncpy(value, val_start, length);
            value[length] = '\0';
            return value;
        }
        line = strtok_r(NULL, "\r\n", &saveptr);
    }
    return NULL;
}

#include <stdint.h>
#include <stdlib.h>

uint64_t parse_request_start(const char *header_value) {
    if (!header_value) return 0;
    return strtoull(header_value, NULL, 10);
}

#define perror(msg) uk_pr_err("%s: %s\n", msg, strerror(errno))
int main(int argc, char**argv) {
    int srv, client;
    struct sockaddr_in srv_addr;
    char final_reply[2048];
    uint64_t boot_wall_time;
    boot_wall_time = get_boot_wall_time_ns(argv[argc-1]);
#if 0
    printf("argc: %d\n");
    for (int i=0;i<argc;i++) {
	    printf("argv[%d]: %s\n", i, argv[i]);
    }
#endif
    printf("Boot wall time str (ns): %s\n", argv[argc-1]);
    printf("Boot wall time (ns): %lu\n", boot_wall_time);


    srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return 1;
    }

    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons(LISTEN_PORT);

    if (bind(srv, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(srv, 1) < 0) {
        perror("listen");
        return 1;
    }

    printf("Listening on port %d...\n", LISTEN_PORT);

    while (1) {
        client = accept(srv, NULL, 0);
        if (client < 0) {
            perror("accept");
            continue;
        }

        // Read request (not used, just to clear the socket)
        read(client, recvbuf, BUFLEN);

        // Calculate elapsed time since boot
        long unsigned int now_ns = ukplat_monotonic_clock();
        long unsigned int elapsed_ns = (now_ns - boot_time_ns);
	printf("elapsed_ms: %lu\n", elapsed_ns);

	const char *request_start_str = get_request_start_header(recvbuf);
	uint64_t request_start_ns = parse_request_start(request_start_str);
	now_ns = ukplat_monotonic_clock();

	printf("now_ns: %lu\n", now_ns);
	printf("request_start_ns: %lu\n", request_start_ns);
	long unsigned int mytime = request_start_ns - boot_wall_time;
	printf("request_until proper: %lu\n", mytime);
	long unsigned int mytime2 = boot_wall_time - request_start_ns;
	printf("request_until reverse: %lu\n", mytime2);
	//if (request_start_ns > 0 && now_ns > request_start_ns) {
	long unsigned int cold_start_ns;
	cold_start_ns = mytime + elapsed_ns;
	printf("cold_start_ns: %lu\n", cold_start_ns);

        int reply_len = snprintf(final_reply, sizeof(final_reply), reply, elapsed_ns, cold_start_ns);

        if (reply_len > 0) {
            write(client, final_reply, reply_len);
            printf("Reply sent: boot time %lu ms\n", elapsed_ns);
        }

        close(client);
    }

    return 0;
}
