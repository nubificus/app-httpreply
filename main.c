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

#include "images.h"
#define LISTEN_PORT 8080
static const char reply[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
    "<head><meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>Serverless Demo</title>"
    "<style>table {border-collapse: collapse; width: 50%%;}th, td {border: 1px solid #dddddd; text-align: left; padding: 8px;}th {background-color: #f2f2f2;}</style>"
    "</head>"
    "<body>"
    "<h1> Hello <img src=\"data:image/png;base64,%s\" alt=\"CAMAD Logo\"/></h1>"
    "<h2> RuntimeClass</h2>"
    "<img src=\"%s\" alt=\"Runtime Class\" height=\"100px\" />"
    "<h1>Request Headers</h1>"
    "<table><tr><th>Header</th><th>Value</th></tr>%s</table>"
    "<h2> Brought to you by </h2>"
    "<img src=\"%s\" width=200px alt=\"Nubis PC\"/>"
    "</body></html>";


#define BUFLEN 4096
static char recvbuf[BUFLEN];

// URLs for images
const char *imageFirecrackerURL = "https://s3.nbfc.io/hypervisor-logos/firecracker.png";
const char *imageQEMUURL = "https://s3.nbfc.io/hypervisor-logos/qemu.png";
const char *imageCLHURL = "https://s3.nbfc.io/hypervisor-logos/clh.png";
const char *imageRSURL = "https://s3.nbfc.io/hypervisor-logos/dragonball.png";
const char *imageURUNCFCURL = "https://s3.nbfc.io/hypervisor-logos/uruncfc.png";
const char *imageURUNCQEMUURL = "https://s3.nbfc.io/hypervisor-logos/uruncqemu.png";

const char *imageContainerURL = "https://s3.nbfc.io/hypervisor-logos/container.png";


// Alternative to find a substring within a string without using strstr
const char* find_substring(const char* haystack, const char* needle) {
    const char *h, *n;
    size_t needle_len = strlen(needle);

    // Check for NULL input
    if (!haystack || !needle || needle_len == 0) {
        return NULL;
    }

    // Loop through haystack
    for (h = haystack; *h != '\0'; h++) {
        // If first character matches, start comparing the rest
        if (*h == *needle) {
            n = needle;
            while (*n != '\0' && *(h + (n - needle)) == *n) {
                n++;
            }

            // If the whole needle is found
            if (*n == '\0') {
                return h;  // Return the starting position of the match
            }
        }
    }
    return NULL;  // Return NULL if not found
}


// Function to determine the runtime class image URL based on the hostname
const char* determineImageURL(const char *host) {
    if (find_substring(host, "hellofc") != NULL) {
        return imageFirecrackerURL;
    } else if (find_substring(host, "helloqemu") != NULL) {
        return imageQEMUURL;
    } else if (find_substring(host, "helloclh") != NULL) {
        return imageCLHURL;
    } else if (find_substring(host, "hellors") != NULL) {
        return imageRSURL;
    } else if (find_substring(host, "hellouruncfc") != NULL) {
        return imageURUNCFCURL;
    } else if (find_substring(host, "hellouruncqemu") != NULL) {
        return imageURUNCQEMUURL;
    } else {
        return imageContainerURL;  // Default image
    }
}

void hexdump(const void *buffer, size_t size) {
    const unsigned char *buf = (const unsigned char *)buffer;
    size_t i, j;

    for (i = 0; i < size; i += 16) {
        printf("%06lx: ", i);  // Print the offset (address)

        // Print the hex values
        for (j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02x ", buf[i + j]);  // Print hex byte
            } else {
                printf("   ");  // Align properly if we're at the end
            }
        }

        printf("  ");

        // Print the ASCII representation
        for (j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%c", isprint(buf[i + j]) ? buf[i + j] : '.');
            }
        }

        printf("\n");
    }
}


const char* getHostFromHeaders(const char *request) {
    static char host[256];  // Buffer to store the hostname
    const char *hostHeader = "Host: ";
    const char *host_start;
    const char *line_end;
    char *line;
    char *saveptr;
    char buffer[BUFLEN];  // Copy of request buffer to modify

    // Make a modifiable copy of the input
    strncpy(buffer, request, BUFLEN - 1);
    buffer[BUFLEN - 1] = '\0';  // Ensure null-termination

    //hexdump(buffer, BUFLEN);
    line = strtok_r(buffer, "\r\n", &saveptr);  // Split request into lines
    while (line != NULL) {
    	//printf("%s :%d \n", __func__, __LINE__);  // Debugging output
    	//printf("line: %s\n", line);  // Debugging output

        // Find the "Host: " header in the current line
        host_start = find_substring(line, hostHeader);
        if (host_start) {
            // Move the pointer after "Host: "
            host_start += strlen(hostHeader);

            // Find the end of the line (i.e., "\r\n")
            line_end = find_substring(host_start, "\r\n");
            if (!line_end) {
                line_end = host_start + strlen(host_start);  // Assume end of host if no newline
            }

            // Copy the hostname into the buffer and ensure it's null-terminated
            size_t length = line_end - host_start;
            if (length >= sizeof(host)) {
                return NULL;  // Prevent buffer overflow
            }

            strncpy(host, host_start, length);
            host[length] = '\0';  // Null-terminate the hostname
            printf("Host: %s\n", host);  // Debugging output
            return host;  // Return the extracted hostname
        }

        line = strtok_r(NULL, "\r\n", &saveptr);  // Move to the next line
    }

    return NULL;  // Return NULL if no "Host: " header was found
}




// Helper function to parse headers
void parse_headers(char *recvbuf, char *headers_html, size_t max_size) {
    char *line;
    char *saveptr;
    char key[256];
    char value[256];
    int first_line = 1;

    // Initialize the headers HTML table
    strcpy(headers_html, "");

    // Split the received buffer by lines
    line = strtok_r(recvbuf, "\r\n", &saveptr);
    while (line != NULL) {
        // Skip the first line (request line: GET / HTTP/1.1)
        if (first_line) {
            first_line = 0;
            line = strtok_r(NULL, "\r\n", &saveptr);
            continue;
        }

        // Find the colon that separates the key and value
        char *colon_pos = strchr(line, ':');
        if (colon_pos != NULL) {
            // Extract key
            size_t key_len = colon_pos - line;
            strncpy(key, line, key_len);
            key[key_len] = '\0';

            // Extract value (skip the space after the colon)
            strcpy(value, colon_pos + 2);

            // Append to the headers HTML string
            char row[512];
            snprintf(row, sizeof(row), "<tr><td>%s</td><td>%s</td></tr>", key, value);
            strncat(headers_html, row, max_size - strlen(headers_html) - 1);
        }

        // Get the next line
        line = strtok_r(NULL, "\r\n", &saveptr);
    }
}


// Function to read the full HTTP request headers
ssize_t read_full_request(int client, char *buffer, size_t max_len) {
    size_t total_read = 0;
    ssize_t bytes_read;

    while (total_read < max_len - 1) {  // Leave room for null-termination
        bytes_read = read(client, buffer + total_read, max_len - total_read - 1);
        if (bytes_read < 0) {
            fprintf(stderr, "Failed to read data: %d\n", errno);
            return -1;
        }

        if (bytes_read == 0) {
            // Connection closed by the client
            break;
        }

        total_read += bytes_read;
        buffer[total_read] = '\0';  // Null-terminate the buffer

        // Check if we've received the full headers (end of headers is \r\n\r\n)
        if (find_substring(buffer, "\r\n\r\n") != NULL) {
            break;
        }
    }

    return total_read;
}



int main(int argc __attribute__((unused)),
	 char *argv[] __attribute__((unused)))
{
	int rc = 0;
	int srv, client;
	ssize_t n;
	struct sockaddr_in srv_addr;

	srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0) {
		fprintf(stderr, "Failed to create socket: %d\n", errno);
		goto out;
	}

	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = INADDR_ANY;
	srv_addr.sin_port = htons(LISTEN_PORT);

	rc = bind(srv, (struct sockaddr *) &srv_addr, sizeof(srv_addr));
	if (rc < 0) {
		fprintf(stderr, "Failed to bind socket: %d\n", errno);
		goto out;
	}

	/* Accept one simultaneous connection */
	rc = listen(srv, 1);
	if (rc < 0) {
		fprintf(stderr, "Failed to listen on socket: %d\n", errno);
		goto out;
	}

	printf("Listening on port %d...\n", LISTEN_PORT);
	while (1) {

        client = accept(srv, NULL, 0);  // Simulate accept (replace with real socket code)
        if (client < 0) {
            fprintf(stderr, "Failed to accept incoming connection: %d\n", errno);
            goto out;
        }

        // Read the full HTTP request headers
        ssize_t total_bytes = read_full_request(client, recvbuf, BUFLEN);
        if (total_bytes < 0) {
            close(client);
            continue;
        }

        // Print raw dump of the received buffer
        printf("Raw HTTP headers dump:\n");
        hexdump(recvbuf, total_bytes);

        // Continue with header parsing (e.g., extracting Host header)
        const char *hostname = getHostFromHeaders(recvbuf);
        if (hostname) {
            printf("Extracted Hostname: %s\n", hostname);
        } else {
            printf("Hostname not found in headers\n");
        }

		char final_reply[16384];  // Ensure it's large enough to hold the full reply.
		char headers_html[8192];  // Buffer to store generated HTML table for headers

                // Parse headers into HTML table rows
                parse_headers(recvbuf, headers_html, sizeof(headers_html));

		const char *imageURL = determineImageURL(hostname);
		const char *image2 = "https://s3.nbfc.io/hypervisor-logos/nubis-logo-scaled.png";

		// Format the reply using snprintf
		int reply_len = snprintf(final_reply, sizeof(final_reply), reply, image1, imageURL, headers_html, image2);

		// Check if snprintf was successful
		if (reply_len < 0 || reply_len >= sizeof(final_reply)) {
		    fprintf(stderr, "Failed to format reply: buffer too small or error occurred, reply_len: %d\n", reply_len);
		}

                //printf("Final reply:\n%s\n", final_reply);
		//printf("Image %s: \n", imageURL);
		/* Send reply */
		n = write(client, final_reply, strlen(final_reply));
		if (n < 0)
			fprintf(stderr, "Failed to send a reply\n");
		else
			printf("Sent a reply\n");

		/* Close connection */
		close(client);
	}

out:
	return rc;
}
