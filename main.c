/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Authors: Simon Kuenzer <simon.kuenzer@neclab.eu>
 *
 * Copyright (c) 2019, NEC Laboratories Europe GmbH, NEC Corporation.
 *                     All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#define LISTEN_PORT 8080
#define BUFLEN 4096

static char recvbuf[BUFLEN];

// Image URLs
const char *imageFirecrackerURL = "https://s3.nbfc.io/hypervisor-logos/firecracker.png";
const char *imageQEMUURL        = "https://s3.nbfc.io/hypervisor-logos/qemu.png";
const char *imageCLHURL         = "https://s3.nbfc.io/hypervisor-logos/clh.png";
const char *imageRSURL          = "https://s3.nbfc.io/hypervisor-logos/dragonball.png";
const char *imageURUNCFCURL     = "https://s3.nbfc.io/hypervisor-logos/uruncfc.png";
const char *imageURUNCQEMUURL   = "https://s3.nbfc.io/hypervisor-logos/uruncqemu.png";
const char *imageContainerURL   = "https://s3.nbfc.io/hypervisor-logos/container.png";
const char *imageAthensURL      = "https://s3.nbfc.io/hypervisor-logos/athk8s.png";
const char *imageNubisURL       = "https://s3.nbfc.io/hypervisor-logos/nubis-logo-scaled.png";

// Response template (HTML)
static const char reply[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
    "<head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>Serverless Demo</title>"
    "<style>"
    "body { font-family: sans-serif; margin: 20px; }"
    "h1, h2 { margin-bottom: 10px; }"
    ".logo-row { display: flex; flex-wrap: wrap; gap: 20px; align-items: center; }"
    ".logo-row img { max-width: 100%%; height: auto; max-height: 120px; }"
    ".header-list { list-style: none; padding: 0; }"
    ".header-list li { margin: 5px 0; padding: 8px; background: #f2f2f2; border-radius: 5px; }"
    "</style>"
    "</head>"
    "<body>"
    "<h1>Hello from urunc!</h1>"
    "<div class=\"logo-row\">"
    "<img src=\"%s\" alt=\"Athens K8s Logo\" />"
    "<img src=\"%s\" alt=\"RuntimeClass\" />"
    "</div>"
    "<h2>Request Headers</h2>"
    "<ul class=\"header-list\">%s</ul>"
    "<h2>Brought to you by</h2>"
    "<img src=\"%s\" alt=\"Nubis Logo\" style=\"max-width: 200px;\">"
    "</body></html>";

#define perror(msg) uk_pr_err("%s: %s\n", msg, strerror(errno))

// Substring search alternative to `strstr`
const char* find_substring(const char* haystack, const char* needle) {
    size_t needle_len = strlen(needle);
    if (!haystack || !needle || needle_len == 0) return NULL;
    for (const char *h = haystack; *h; ++h) {
        if (strncmp(h, needle, needle_len) == 0) return h;
    }
    return NULL;
}

// Determine image based on Host
const char* determineImageURL(const char *host) {
    if (find_substring(host, "hellofc")) return imageFirecrackerURL;
    if (find_substring(host, "helloqemu")) return imageQEMUURL;
    if (find_substring(host, "helloclh")) return imageCLHURL;
    if (find_substring(host, "hellors")) return imageRSURL;
    if (find_substring(host, "hellouruncfc")) return imageURUNCFCURL;
    if (find_substring(host, "hellouruncqemu")) return imageURUNCQEMUURL;
    return imageContainerURL;
}

// Parse headers into <li> elements
void parse_headers(char *recvbuf, char *headers_html, size_t max_size) {
    char *line, *saveptr;
    char key[256], value[256];
    int first_line = 1;

    headers_html[0] = '\0';  // Clear output

    line = strtok_r(recvbuf, "\r\n", &saveptr);
    while (line) {
        if (first_line) {
            first_line = 0;
            line = strtok_r(NULL, "\r\n", &saveptr);
            continue;
        }

        char *colon_pos = strchr(line, ':');
        if (colon_pos) {
            size_t key_len = colon_pos - line;
            strncpy(key, line, key_len);
            key[key_len] = '\0';
            strcpy(value, colon_pos + 2);

            char row[512];
            snprintf(row, sizeof(row), "<li><strong>%s:</strong> %s</li>", key, value);
            strncat(headers_html, row, max_size - strlen(headers_html) - 1);
        }

        line = strtok_r(NULL, "\r\n", &saveptr);
    }
}

// Extract Host header
const char* getHostFromHeaders(const char *request) {
    static char host[256];
    const char *hostHeader = "Host: ";
    char buffer[BUFLEN], *line, *saveptr;

    strncpy(buffer, request, BUFLEN - 1);
    buffer[BUFLEN - 1] = '\0';

    line = strtok_r(buffer, "\r\n", &saveptr);
    while (line) {
        const char *host_start = find_substring(line, hostHeader);
        if (host_start) {
            host_start += strlen(hostHeader);
            size_t length = strcspn(host_start, "\r\n");
            if (length >= sizeof(host)) return NULL;
            strncpy(host, host_start, length);
            host[length] = '\0';
            return host;
        }
        line = strtok_r(NULL, "\r\n", &saveptr);
    }
    return NULL;
}

// Read the full HTTP request
ssize_t read_full_request(int client, char *buffer, size_t max_len) {
    size_t total_read = 0;
    ssize_t bytes_read;

    while (total_read < max_len - 1) {
        bytes_read = read(client, buffer + total_read, max_len - total_read - 1);
        if (bytes_read <= 0) break;
        total_read += bytes_read;
        buffer[total_read] = '\0';
        if (find_substring(buffer, "\r\n\r\n")) break;
    }

    return total_read;
}

// Main server logic
int main(void) {
    int srv, client;
    struct sockaddr_in srv_addr;
    char headers_html[8192], final_reply[16384];

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

        ssize_t total_bytes = read_full_request(client, recvbuf, BUFLEN);
        if (total_bytes < 0) {
            close(client);
            continue;
        }

        const char *hostname = getHostFromHeaders(recvbuf);
        const char *imageURL = determineImageURL(hostname ? hostname : "");
        parse_headers(recvbuf, headers_html, sizeof(headers_html));

        int reply_len = snprintf(
            final_reply, sizeof(final_reply),
            reply, imageAthensURL, imageURL, headers_html, imageNubisURL
        );

        if (reply_len > 0) {
            write(client, final_reply, reply_len);
            printf("Reply sent.\n");
        }

        close(client);
    }

    return 0;
}
