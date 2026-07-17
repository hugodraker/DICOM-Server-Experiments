/*
 * telnet.c - Minimal Win32 command-line Telnet client
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, distribute, or
 * sell this software for any purpose, commercial or non-commercial, and
 * by any means.
 *
 * Usage: telnet <address> [port]
 *   address  Hostname or IP address to connect to
 *   port     Port number (default: 23)
 *
 * Press Ctrl+] or Ctrl+C to disconnect.
 *
 * Compile with GCC:  gcc -o telnet.exe telnet.c -lws2_32
 * Compile with TCC:   tcc -o telnet.exe telnet.c -lws2_32
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

/* ---- Telnet protocol constants (RFC 854) ---- */
#define TN_IAC   255
#define TN_DONT  254
#define TN_DO    253
#define TN_WONT  252
#define TN_WILL  251
#define TN_SB    250
#define TN_SE    240

/*
 * Process data received from the server.
 * Strips Telnet IAC negotiation sequences, sends minimal responses,
 * and writes the remaining plain text to stdout.
 */
static void
process_server_data(unsigned char *buf, int len, SOCKET sock)
{
    unsigned char out[8192];
    int outlen = 0;
    int i = 0;

    while (i < len) {
        if (buf[i] == TN_IAC) {
            if (i + 1 >= len)
                break;

            switch (buf[i + 1]) {

            case TN_DO:
                /* We support no options — refuse everything */
                if (i + 2 >= len) { i++; break; }
                {
                    unsigned char r[3] = { TN_IAC, TN_WONT, buf[i + 2] };
                    send(sock, (char *)r, 3, 0);
                }
                i += 3;
                break;

            case TN_DONT:
                if (i + 2 >= len) { i++; break; }
                i += 3;
                break;

            case TN_WILL:
                /* Refuse all server-offered options */
                if (i + 2 >= len) { i++; break; }
                {
                    unsigned char r[3] = { TN_IAC, TN_DONT, buf[i + 2] };
                    send(sock, (char *)r, 3, 0);
                }
                i += 3;
                break;

            case TN_WONT:
                if (i + 2 >= len) { i++; break; }
                i += 3;
                break;

            case TN_SB:
                /* Skip subnegotiation block until IAC SE */
                i += 2;
                while (i < len) {
                    if (buf[i] == TN_IAC && i + 1 < len) {
                        if (buf[i + 1] == TN_SE) {
                            i += 2;
                            break;
                        }
                        i += 2;   /* escaped IAC inside SB */
                    } else {
                        i++;
                    }
                }
                break;

            case TN_IAC:
                /* Escaped 0xFF literal in data stream */
                if (outlen < (int)sizeof(out))
                    out[outlen++] = TN_IAC;
                i += 2;
                break;

            default:
                /* Unknown two-byte command — skip it */
                i += 2;
                break;
            }
        } else {
            if (outlen < (int)sizeof(out))
                out[outlen++] = buf[i];
            i++;
        }
    }

    if (outlen > 0) {
        fwrite(out, 1, outlen, stdout);
        fflush(stdout);
    }
}

int
main(int argc, char *argv[])
{
    WSADATA         wsa;
    SOCKET          sock;
    struct sockaddr_in addr;
    struct hostent *he;
    const char     *hostname;
    int             port;
    HANDLE          hStdin;
    DWORD           old_console_mode;
    fd_set          read_fds;
    struct timeval  tv;
    unsigned char   buf[8192];
    int             n;

    /* ---- Parse command line ---- */
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <address> [port]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "  address  Hostname or IP address\n");
        fprintf(stderr, "  port     Port number (default: 23)\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Press Ctrl+] or Ctrl+C to disconnect.\n");
        return 1;
    }

    hostname = argv[1];
    port = (argc == 3) ? atoi(argv[2]) : 23;

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: invalid port '%s'\n", argv[2]);
        return 1;
    }

    /* ---- Initialize WinSock 2.2 ---- */
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "Error: WSAStartup failed (%d)\n",
                WSAGetLastError());
        return 1;
    }

    /* ---- Create TCP socket ---- */
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Error: socket() failed (%d)\n",
                WSAGetLastError());
        WSACleanup();
        return 1;
    }

    /* ---- Resolve address ---- */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((u_short)port);

    /* Try dotted-decimal first, then fall back to DNS */
    addr.sin_addr.s_addr = inet_addr(hostname);
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        he = gethostbyname(hostname);
        if (he == NULL) {
            fprintf(stderr, "Error: cannot resolve '%s'\n", hostname);
            closesocket(sock);
            WSACleanup();
            return 1;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    /* ---- Connect ---- */
    printf("Connecting to %s:%d ... ", hostname, port);
    fflush(stdout);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr))
        == SOCKET_ERROR)
    {
        fprintf(stderr, "failed (%d)\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("connected.\n");
    printf("Press Ctrl+] or Ctrl+C to disconnect.\n");

    /* ---- Put console into raw mode ----
     * Mode 0 disables echo, line buffering, processed input (Ctrl+C),
     * and window / mouse event generation — we get raw keystrokes.
     */
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &old_console_mode);
    SetConsoleMode(hStdin, 0);

    /* ---- Main I/O loop ----
     *
     * We poll the socket with select() using a 50 ms timeout, then
     * check the console for input with WaitForSingleObject(0).
     * This avoids needing threads while still feeling responsive.
     */
    for (;;) {
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        tv.tv_sec  = 0;
        tv.tv_usec = 50000;   /* 50 ms */

        n = select(0, &read_fds, NULL, NULL, &tv);
        if (n == SOCKET_ERROR)
            break;

        /* --- Data from server? --- */
        if (n > 0 && FD_ISSET(sock, &read_fds)) {
            n = recv(sock, (char *)buf, sizeof(buf), 0);
            if (n <= 0) {
                printf("\r\nConnection closed by remote host.\r\n");
                break;
            }
            process_server_data(buf, n, sock);
        }

        /* --- Keyboard input available? --- */
        if (WaitForSingleObject(hStdin, 0) == WAIT_OBJECT_0) {
            INPUT_RECORD rec[64];
            DWORD        count, k;

            if (ReadConsoleInput(hStdin, rec, 64, &count)) {
                for (k = 0; k < count; k++) {
                    char ch;

                    if (rec[k].EventType != KEY_EVENT)
                        continue;
                    if (!rec[k].Event.KeyEvent.bKeyDown)
                        continue;

                    ch = rec[k].Event.KeyEvent.uChar.AsciiChar;
                    if (ch == 0)
                        continue;

                    /* Ctrl+] (0x1D) or Ctrl+C (0x03) => disconnect */
                    if ((unsigned char)ch == 0x1D ||
                        (unsigned char)ch == 0x03)
                    {
                        printf("\r\n");
                        goto cleanup;
                    }

                    send(sock, &ch, 1, 0);
                }
            }
        }
    }

cleanup:
    /* ---- Restore console and tear down ---- */
    SetConsoleMode(hStdin, old_console_mode);
    closesocket(sock);
    WSACleanup();

    return 0;
}