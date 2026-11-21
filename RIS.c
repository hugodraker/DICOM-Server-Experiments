/* ============================================================================
 * RIS Web Server + Embedded DICOM Worklist & PACS Server (SQLite + Win32 Tray)
 *
 * Compile:
 *   gcc -Os -s -DSQLITE_ENABLE_FTS5 -Wno-return-local-addr -o ris.exe ris.c sqlite3.c -lws2_32 -luser32 -lshell32 -lkernel32 -lgdi32 -lcomdlg32
 * ============================================================================ */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sqlite3.h"
#include "ris_server.h"
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")
#define MAX_CLIENTS          128
#define RECV_SIZE            32768
#define CLIENT_BUF_SIZE      65536
#define DEFAULT_TIMEOUT_MS   10000
#define FIONBIO_VAL          0x8004667E
/* UI/Tray Constants */
#define WM_TRAYICON         (WM_USER + 1)
#define ID_TRAY_TOGGLE      1000
#define ID_TRAY_SHOW        1001
#define ID_TRAY_EXIT        1002
#define ID_TRAY_SETTINGS    1003
#define ID_BTN_STARTSTOP    1010
#define ID_EDT_AET          1011
#define ID_EDT_TELNET       1012
#define ID_EDT_HTTP         1013
#define ID_EDT_DICOM        1014
#define ID_EDT_TIMEOUT      1015
#define ID_CHK_DEBUG        1016
#define ID_CHK_RELAY        1020
#define ID_EDT_RELAY_IP     1021
#define ID_EDT_RELAY_PORT   1022
#define ID_EDT_RELAY_AET    1023
#define ID_CHK_RELAY_DEL    1024
#define ID_EDT_RELAY_CMD    1025
#define ID_BTN_RELAY_BROWSE 1026
#define ID_OK               IDOK
#define ID_BTN_DEL_PATIENTS 1027
#define ID_BTN_DEL_ALL      1028
#define ID_LBL_SETTINGS_MSG 1029
#define ID_BTN_RESET_ADMIN  1030
/* Global Networking & Application State (DICOM/Relay globals come from ris_server.h) */
sqlite3 *g_db = NULL;
SOCKET g_listenSocketHttp = INVALID_SOCKET;
int g_bHttpRunning = 0, g_bAppExit = 0;
int g_HttpPort = 8080;
int g_TelnetPort = 23, g_TelnetTimeout = 10;

/* C-STORE -> SQLite sync trackers */
static DWORD g_lastCStoreCheckTick = 0;
static long  g_lastCStoreFileSize = -1;

/* Client Buffers */
SOCKET g_clients[MAX_CLIENTS];
int    g_clientBufLen[MAX_CLIENTS];
int    g_clientBufCap[MAX_CLIENTS];
char  *g_clientBuffers[MAX_CLIENTS];
DWORD  g_clientLastTick[MAX_CLIENTS];
char   g_recvBuf[RECV_SIZE];

/* Win32 GUI */
HINSTANCE g_hInstance = NULL;
HWND g_hMainWnd = NULL;
HWND g_hSettingsDlg = NULL;
HMENU g_hMenu = NULL;
NOTIFYICONDATA nid;
const char *szWndClass = "RISServerTrayClass";
const char *szTrayTip = "RIS HTTP + DICOM Server";

static DWORD g_lastOutboundDispatchTick = 0;
static unsigned int g_unlinkedUploadSeq = 0;

typedef struct { uint16_t g, e; char vr[3]; char val[256]; int found; } DcmTagRule;

/* --- String & Utility Helpers --- */
static char* StrIStr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    int len = (int)strlen(needle);
    while (*haystack) {
        if (StrNIEquals(haystack, needle, len)) return (char*)haystack;
        haystack++;
    }
    return NULL;
}

static void ParseCsvField(const char **p, char *out, size_t outSz) {
    size_t i = 0;
    const char *s = *p;
    if (*s == '"') {
        s++;
        while (*s) {
            if (*s == '"') {
                if (*(s + 1) == '"') {
                    if (i + 1 < outSz) out[i++] = '"';
                    s += 2;
                    continue;
                }
                s++;
                break;
            }
            if (i + 1 < outSz) out[i++] = *s;
            s++;
        }
        if (*s == ',') s++;
    } else {
        while (*s && *s != ',' && *s != '\r' && *s != '\n') {
            if (i + 1 < outSz) out[i++] = *s;
            s++;
        }
        if (*s == ',') s++;
    }
    out[i] = '\0';
    *p = s;
}

static void StrReplaceAll(char *dst, size_t dstSize, const char *src, const char *find, const char *repl) {
    char tmp[4096];
    const char *p = src;
    size_t outLen = 0, findLen = strlen(find), replLen = strlen(repl);
    while (*p && outLen < sizeof(tmp) - 1) {
        const char *hit = strstr(p, find);
        if (!hit) {
            size_t rem = strlen(p);
            if (outLen + rem >= sizeof(tmp)) rem = sizeof(tmp) - 1 - outLen;
            memcpy(tmp + outLen, p, rem);
            outLen += rem;
            break;
        }
        size_t chunk = (size_t)(hit - p);
        if (outLen + chunk >= sizeof(tmp)) chunk = sizeof(tmp) - 1 - outLen;
        memcpy(tmp + outLen, p, chunk);
        outLen += chunk;
        if (outLen + replLen < sizeof(tmp) - 1) {
            memcpy(tmp + outLen, repl, replLen);
            outLen += replLen;
        }
        p = hit + findLen;
    }
    tmp[outLen] = '\0';
    strncpy(dst, tmp, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static void SanitizeArg(char *dst, size_t dstSize, const char *src) {
    size_t j = 0;
    if (!src) { dst[0] = '\0'; return; }
    for (size_t i = 0; src[i] && j < dstSize - 1; i++) {
        char c = src[i];
        if (c == '"' || c == '\r' || c == '\n' || c == '|' || c == '&' || c == ';') c = ' ';
        dst[j++] = c;
    }
    dst[j] = '\0';
}

static int MemContains(const char *hay, int hayLen, const char *needle) {
    int nLen = (int)strlen(needle);
    if (nLen <= 0 || hayLen < nLen) return 0;
    for (int i = 0; i <= hayLen - nLen; i++) {
        if (memcmp(hay + i, needle, (size_t)nLen) == 0) return 1;
    }
    return 0;
}

/* --- HTTP Response Helpers --- */
void SendBufferCompletely(SOCKET sock, const char* buf, int len) {
    int total = 0;
    while (total < len) {
        int r = send(sock, buf + total, len - total, 0);
        if (r < 0) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                Sleep(1);
                continue;
            }
            break;
        }
        if (r == 0) break;
        total += r;
    }
}

void ServeFile(SOCKET sock, const char* filepath, const char* contentType) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        char err[] = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n404 File Not Found";
        SendBufferCompletely(sock, err, (int)strlen(err));
        return;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char header[512];
    int hdrLen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Connection: close\r\n\r\n", contentType, fsize);
    SendBufferCompletely(sock, header, hdrLen);

    char chunk[8192];
    size_t bytes;
    while ((bytes = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        SendBufferCompletely(sock, chunk, (int)bytes);
    }
    fclose(f);
}

void SendHttpResponse(SOCKET sock, const char* contentType, const char* body, int bodyLen) {
    char header[512];
    int hdrLen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n",
        contentType, bodyLen);
    SendBufferCompletely(sock, header, hdrLen);
    SendBufferCompletely(sock, body, bodyLen);
}

static void SendJsonWithCookie(SOCKET sock, const char *json, const char *setCookieHeader) {
    char header[768];
    int bodyLen = (int)strlen(json);
    int hdrLen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "%s"
        "Connection: close\r\n\r\n",
        bodyLen, setCookieHeader ? setCookieHeader : "");
    SendBufferCompletely(sock, header, hdrLen);
    SendBufferCompletely(sock, json, bodyLen);
}

/* --- URL Query Parameter Helper --- */
static int GetQueryParam(const char *req, const char *key, char *out, size_t outSize) {
    out[0] = '\0';
    const char *q = strchr(req, '?');
    const char *lineEnd = strstr(req, " HTTP/");
    if (!q || !lineEnd || q >= lineEnd) return 0;
    q++;
    size_t klen = strlen(key);
    while (q < lineEnd) {
        if (strncmp(q, key, klen) == 0 && q[klen] == '=') {
            const char *v = q + klen + 1;
            size_t j = 0;
            while (v < lineEnd && *v != '&' && *v != ' ' && j < outSize - 1) {
                if (*v == '%' && v + 2 < lineEnd) {
                    char hex[3] = { v[1], v[2], 0 };
                    out[j++] = (char)strtol(hex, NULL, 16);
                    v += 3;
                } else if (*v == '+') {
                    out[j++] = ' ';
                    v++;
                } else {
                    out[j++] = *v++;
                }
            }
            out[j] = '\0';
            return 1;
        }
        const char *amp = strchr(q, '&');
        if (!amp || amp >= lineEnd) break;
        q = amp + 1;
    }
    return 0;
}

static int GetFormParam(const char *body, const char *key, char *out, size_t outSz) {
    out[0] = '\0';
    if (!body || !key) return 0;
    size_t klen = strlen(key);
    const char *p = body;
    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            size_t j = 0;
            while (*v && *v != '&' && j < outSz - 1) {
                if (*v == '%' && v[1] && v[2]) {
                    char hx[3] = { v[1], v[2], 0 };
                    out[j++] = (char)strtol(hx, NULL, 16);
                    v += 3;
                } else if (*v == '+') {
                    out[j++] = ' '; v++;
                } else {
                    out[j++] = *v++;
                }
            }
            out[j] = '\0';
            return 1;
        }
        const char *amp = strchr(p, '&');
        if (!amp) break;
        p = amp + 1;
    }
    return 0;
}

static int GetCookieParam(const char *req, const char *name, char *out, size_t outSz) {
    out[0] = '\0';
    const char *hdrEnd = strstr(req, "\r\n\r\n");
    const char *cHdr = StrIStr(req, "\r\nCookie:");
    if (!cHdr || (hdrEnd && cHdr > hdrEnd)) return 0;
    cHdr += 9;
    const char *lineEnd = strstr(cHdr, "\r\n");
    if (!lineEnd) lineEnd = cHdr + strlen(cHdr);
    size_t nlen = strlen(name);
    const char *p = cHdr;
    while (p < lineEnd) {
        while (p < lineEnd && (*p == ' ' || *p == ';')) p++;
        if (p + nlen < lineEnd && strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            const char *v = p + nlen + 1;
            size_t j = 0;
            while (v < lineEnd && *v != ';' && *v != '\r' && *v != '\n' && j < outSz - 1) out[j++] = *v++;
            out[j] = '\0';
            return 1;
        }
        const char *semi = strchr(p, ';');
        if (!semi || semi >= lineEnd) break;
        p = semi + 1;
    }
    return 0;
}

static void Sha256Hex(const unsigned char *data, size_t len, char outHex[65]) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };
    size_t padLen = ((len + 9 + 63) / 64) * 64;
    unsigned char *msg = (unsigned char*)calloc(1, padLen);
    if (!msg) { outHex[0] = '\0'; return; }
    memcpy(msg, data, len);
    msg[len] = 0x80;
    uint64_t bitLen = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) msg[padLen - 1 - i] = (unsigned char)((bitLen >> (i * 8)) & 0xFF);

    for (size_t off = 0; off < padLen; off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)msg[off + i*4] << 24) | ((uint32_t)msg[off + i*4 + 1] << 16) |
                   ((uint32_t)msg[off + i*4 + 2] << 8) | (uint32_t)msg[off + i*4 + 3];
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ((w[i-15] >> 7) | (w[i-15] << 25)) ^ ((w[i-15] >> 18) | (w[i-15] << 14)) ^ (w[i-15] >> 3);
            uint32_t s1 = ((w[i-2] >> 17) | (w[i-2] << 15)) ^ ((w[i-2] >> 19) | (w[i-2] << 13)) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ((e >> 6) | (e << 26)) ^ ((e >> 11) | (e << 21)) ^ ((e >> 25) | (e << 7));
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = ((a >> 2) | (a << 30)) ^ ((a >> 13) | (a << 19)) ^ ((a >> 22) | (a << 10));
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    free(msg);
    for (int i = 0; i < 8; i++) snprintf(outHex + i * 8, 9, "%08x", h[i]);
    outHex[64] = '\0';
}

static void GenerateRandomTokenHex(char *out, size_t hexChars) {
    static unsigned long seq = 0;
    LARGE_INTEGER pc; QueryPerformanceCounter(&pc);
    char seed[128];
    snprintf(seed, sizeof(seed), "%lu:%lld:%lu:%u:%p",
             (unsigned long)GetTickCount(), (long long)pc.QuadPart,
             (unsigned long)GetCurrentProcessId(), ++seq, (void*)out);
    char digest[65];
    Sha256Hex((const unsigned char*)seed, strlen(seed), digest);
    if (hexChars > 64) hexChars = 64;
    memcpy(out, digest, hexChars);
    out[hexChars] = '\0';
}

static void HashPasswordSalted(const char *salt, const char *password, char outHex[65]) {
    char combo[512];
    snprintf(combo, sizeof(combo), "%s:%s", salt ? salt : "", password ? password : "");
    Sha256Hex((const unsigned char*)combo, strlen(combo), outHex);
}

static void EscapeTypstInline(char *dst, size_t dstSize, const char *src) {
    size_t j = 0;
    if (!src) { dst[0] = '\0'; return; }
    for (size_t i = 0; src[i] && j + 4 < dstSize; i++) {
        char c = src[i];
        if (c == '\\' || c == '"') { dst[j++] = '\\'; dst[j++] = c; }
        else if (c == '\r' || c == '\n') { dst[j++] = ' '; }
        else { dst[j++] = c; }
    }
    dst[j] = '\0';
}

static void ReplaceTokenLarge(char *buf, size_t bufSz, const char *find, const char *repl) {
    char *tmp = (char*)malloc(bufSz);
    if (!tmp) return;
    const char *p = buf;
    size_t outLen = 0, findLen = strlen(find), replLen = strlen(repl);
    while (*p && outLen < bufSz - 1) {
        const char *hit = strstr(p, find);
        if (!hit) {
            size_t rem = strlen(p);
            if (outLen + rem >= bufSz) rem = bufSz - 1 - outLen;
            memcpy(tmp + outLen, p, rem);
            outLen += rem;
            break;
        }
        size_t chunk = (size_t)(hit - p);
        if (outLen + chunk >= bufSz) chunk = bufSz - 1 - outLen;
        memcpy(tmp + outLen, p, chunk);
        outLen += chunk;
        if (outLen + replLen < bufSz - 1) {
            memcpy(tmp + outLen, repl, replLen);
            outLen += replLen;
        }
        p = hit + findLen;
    }
    tmp[outLen] = '\0';
    memcpy(buf, tmp, outLen + 1);
    free(tmp);
}

static void SanitizeTypstBody(char *dst, size_t dstSize, const char *src) {
    size_t j = 0;
    if (!src) { dst[0] = '\0'; return; }
    for (size_t i = 0; src[i] && j + 2 < dstSize; i++) {
        if (src[i] == '\r') continue;
        dst[j++] = src[i];
    }
    dst[j] = '\0';
}

static void EscapeTypstStr(char *dst, size_t dstSize, const char *src) {
    size_t j = 0;
    if (!src) { dst[0] = '\0'; return; }
    for (size_t i = 0; src[i] && j + 4 < dstSize; i++) {
        char c = src[i];
        if (c == '\\' || c == '"') { dst[j++] = '\\'; dst[j++] = c; }
        else if (c == '\r') { /* skip CR */ }
        else if (c == '\n') { dst[j++] = '\\'; dst[j++] = 'n'; }
        else { dst[j++] = c; }
    }
    dst[j] = '\0';
}

static int BuildAndStoreReportTypst(sqlite3 *db, int reportId, char *outTyp, size_t outTypSz) {
    if (outTyp && outTypSz > 0) outTyp[0] = '\0';
    if (!db || reportId <= 0) return 0;

    const char *sql =
        "SELECT o.order_id, o.accession_number, ec.exam_name, ec.modality_type, "
        "       p.last_name || ', ' || p.first_name, p.mrn, p.dob, p.biological_sex, "
        "       COALESCE(ref.first_name || ' ' || ref.last_name, 'N/A'), "
        "       COALESCE(rad.first_name || ' ' || rad.last_name || COALESCE(', ' || rad.credentials, ''), 'N/A'), "
        "       COALESCE(rad.npi_or_license, ''), "
        "       COALESCE(f.facility_name, 'Diagnostic Imaging'), COALESCE(f.address, ''), COALESCE(f.phone, ''), "
        "       r.report_status, r.version_number, COALESCE(r.final_signed_at, r.dictated_at), "
        "       COALESCE(r.clinical_history_section, ''), COALESCE(r.comparison_studies_section, 'None'), "
        "       r.technique_section, r.findings_section, r.impression_section, "
        "       CASE WHEN r.standardized_category_system != 'NONE' THEN '*Standardized Assessment:* ' || r.standardized_category_system || ' ' || COALESCE(r.standardized_category_score,'') || char(10) ELSE '' END "
        "FROM radiology_reports r "
        "JOIN imaging_orders o ON r.order_id = o.order_id "
        "JOIN patients p ON o.patient_id = p.patient_id "
        "JOIN exam_catalog ec ON o.exam_code = ec.exam_code "
        "LEFT JOIN facilities f ON o.facility_id = f.facility_id "
        "LEFT JOIN staff_users ref ON o.referring_md_id = ref.user_id "
        "LEFT JOIN staff_users rad ON r.signing_radiologist_id = rad.user_id "
        "WHERE r.report_id = ?";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, 0) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, reportId);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); return 0; }

    int orderId = sqlite3_column_int(st, 0);
    int ver     = sqlite3_column_int(st, 15);
    char verStr[16]; snprintf(verStr, sizeof(verStr), "%d", ver);

    char acc[64], exam[128], mod[32], pat[128], mrn[64], dob[32], sex[8];
    char refMd[128], radMd[128], radLic[64], facName[128], facAddr[256], facPhone[64];
    char repStat[32], signedAt[64], catScore[128];
    char *hist = (char*)malloc(2048), *comp = (char*)malloc(1024), *tech = (char*)malloc(2048);
    char *find = (char*)malloc(8192), *impr = (char*)malloc(4096);
    char *typBuf = (char*)malloc(32768);
    if (!hist || !comp || !tech || !find || !impr || !typBuf) {
        free(hist); free(comp); free(tech); free(find); free(impr); free(typBuf);
        sqlite3_finalize(st);
        return 0;
    }

    EscapeTypstInline(acc,      sizeof(acc),      (const char*)sqlite3_column_text(st, 1));
    EscapeTypstInline(exam,     sizeof(exam),     (const char*)sqlite3_column_text(st, 2));
    EscapeTypstInline(mod,      sizeof(mod),      (const char*)sqlite3_column_text(st, 3));
    EscapeTypstInline(pat,      sizeof(pat),      (const char*)sqlite3_column_text(st, 4));
    EscapeTypstInline(mrn,      sizeof(mrn),      (const char*)sqlite3_column_text(st, 5));
    EscapeTypstInline(dob,      sizeof(dob),      (const char*)sqlite3_column_text(st, 6));
    EscapeTypstInline(sex,      sizeof(sex),      (const char*)sqlite3_column_text(st, 7));
    EscapeTypstInline(refMd,    sizeof(refMd),    (const char*)sqlite3_column_text(st, 8));
    EscapeTypstInline(radMd,    sizeof(radMd),    (const char*)sqlite3_column_text(st, 9));
    EscapeTypstInline(radLic,   sizeof(radLic),   (const char*)sqlite3_column_text(st, 10));
    EscapeTypstInline(facName,  sizeof(facName),  (const char*)sqlite3_column_text(st, 11));
    EscapeTypstInline(facAddr,  sizeof(facAddr),  (const char*)sqlite3_column_text(st, 12));
    EscapeTypstInline(facPhone, sizeof(facPhone), (const char*)sqlite3_column_text(st, 13));
    EscapeTypstInline(repStat,  sizeof(repStat),  (const char*)sqlite3_column_text(st, 14));
    EscapeTypstInline(signedAt, sizeof(signedAt), (const char*)sqlite3_column_text(st, 16));
    SanitizeTypstBody(hist,     2048,             (const char*)sqlite3_column_text(st, 17));
    SanitizeTypstBody(comp,     1024,             (const char*)sqlite3_column_text(st, 18));
    SanitizeTypstBody(tech,     2048,             (const char*)sqlite3_column_text(st, 19));
    SanitizeTypstBody(find,     8192,             (const char*)sqlite3_column_text(st, 20));
    SanitizeTypstBody(impr,     4096,             (const char*)sqlite3_column_text(st, 21));
    SanitizeTypstBody(catScore, sizeof(catScore), (const char*)sqlite3_column_text(st, 22));
    sqlite3_finalize(st);

    /* Load Typst template from SQL (modality-specific match first, then default) */
    char tplCode[64] = "STANDARD_RADIOLOGY_REPORT";
    typBuf[0] = '\0';
    const char *tplSql =
        "SELECT template_code, typst_markup FROM typst_report_templates "
        "WHERE modality_type = ? OR modality_type = 'ALL' OR is_default = 1 "
        "ORDER BY CASE WHEN modality_type = ? THEN 1 WHEN is_default = 1 THEN 2 ELSE 3 END "
        "LIMIT 1";
    if (sqlite3_prepare_v2(db, tplSql, -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, mod, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, mod, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char *c = (const char*)sqlite3_column_text(st, 0);
            const char *m = (const char*)sqlite3_column_text(st, 1);
            if (c) { strncpy(tplCode, c, sizeof(tplCode) - 1); tplCode[sizeof(tplCode) - 1] = '\0'; }
            if (m) { strncpy(typBuf, m, 32767); typBuf[32767] = '\0'; }
        }
        sqlite3_finalize(st);
    }

    if (!typBuf[0]) {
        free(hist); free(comp); free(tech); free(find); free(impr); free(typBuf);
        return 0;
    }

    ReplaceTokenLarge(typBuf, 32768, "{facility_name}",          facName);
    ReplaceTokenLarge(typBuf, 32768, "{facility_address}",       facAddr);
    ReplaceTokenLarge(typBuf, 32768, "{facility_phone}",         facPhone);
    ReplaceTokenLarge(typBuf, 32768, "{patient_name}",           pat);
    ReplaceTokenLarge(typBuf, 32768, "{accession}",              acc);
    ReplaceTokenLarge(typBuf, 32768, "{mrn}",                    mrn);
    ReplaceTokenLarge(typBuf, 32768, "{dob}",                    dob);
    ReplaceTokenLarge(typBuf, 32768, "{sex}",                    sex);
    ReplaceTokenLarge(typBuf, 32768, "{exam_name}",              exam);
    ReplaceTokenLarge(typBuf, 32768, "{modality}",               mod);
    ReplaceTokenLarge(typBuf, 32768, "{referring_md}",           refMd);
    ReplaceTokenLarge(typBuf, 32768, "{radiologist}",            radMd);
    ReplaceTokenLarge(typBuf, 32768, "{radiologist_license}",    radLic);
    ReplaceTokenLarge(typBuf, 32768, "{report_status}",          repStat);
    ReplaceTokenLarge(typBuf, 32768, "{version}",                verStr);
    ReplaceTokenLarge(typBuf, 32768, "{signed_at}",              signedAt);
    ReplaceTokenLarge(typBuf, 32768, "{clinical_history}",       hist[0] ? hist : "None provided.");
    ReplaceTokenLarge(typBuf, 32768, "{comparison}",             comp);
    ReplaceTokenLarge(typBuf, 32768, "{technique}",              tech);
    ReplaceTokenLarge(typBuf, 32768, "{findings}",               find);
    ReplaceTokenLarge(typBuf, 32768, "{impression}",             impr);
    ReplaceTokenLarge(typBuf, 32768, "{standardized_assessment}",catScore);

    /* Persist rendered Typst source into report_typst_documents */
    const char *saveSql =
        "INSERT INTO report_typst_documents (report_id, order_id, typst_source, template_name, updated_at) "
        "VALUES (?, ?, ?, ?, datetime('now')) "
        "ON CONFLICT(report_id) DO UPDATE SET "
        "order_id=excluded.order_id, typst_source=excluded.typst_source, template_name=excluded.template_name, updated_at=datetime('now');";
    if (sqlite3_prepare_v2(db, saveSql, -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st,  1, reportId);
        sqlite3_bind_int(st,  2, orderId);
        sqlite3_bind_text(st, 3, typBuf,  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, tplCode, -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    if (outTyp && outTypSz > 0) {
        strncpy(outTyp, typBuf, outTypSz - 1);
        outTyp[outTypSz - 1] = '\0';
    }

    free(hist); free(comp); free(tech); free(find); free(impr); free(typBuf);
    return 1;
}
/* Always ensures Typst is saved in DB; only invokes typst.exe -> PDF when Use JS Typst is disabled */
static int GenerateReportTypstPdf(sqlite3 *db, int reportId, char *outPdfPath, size_t pdfPathSz) {
    outPdfPath[0] = '\0';
    if (!db || reportId <= 0) return 0;

    char *typBuf = (char*)malloc(32768);
    if (!typBuf) return 0;
    if (!BuildAndStoreReportTypst(db, reportId, typBuf, 32768)) {
        free(typBuf);
        return 0;
    }

    char typstExe[512] = "";
    int useJsTypst = 1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT setting_key, setting_value FROM app_settings WHERE setting_key IN ('rep_typst_exe','rep_use_js_typst')", -1, &st, 0) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *k = (const char*)sqlite3_column_text(st, 0);
            const char *v = (const char*)sqlite3_column_text(st, 1);
            if (k && v) {
                if (strcmp(k, "rep_typst_exe") == 0) strncpy(typstExe, v, sizeof(typstExe) - 1);
                else if (strcmp(k, "rep_use_js_typst") == 0) useJsTypst = (strcmp(v, "0") != 0);
            }
        }
        sqlite3_finalize(st);
    }

    if (useJsTypst || typstExe[0] == '\0') {
        free(typBuf);
        return 0;
    }

    CreateDirectoryA("outbound_dispatches", NULL);
    char typPath[MAX_PATH], pdfPath[MAX_PATH];
    snprintf(typPath, sizeof(typPath), "outbound_dispatches\\report_%d.typ", reportId);
    snprintf(pdfPath, sizeof(pdfPath), "outbound_dispatches\\report_%d.pdf", reportId);

    FILE *tf = fopen(typPath, "wb");
    if (tf) {
        fwrite(typBuf, 1, strlen(typBuf), tf);
        fclose(tf);
    }
    free(typBuf);

    char cmdLine[1024];
    snprintf(cmdLine, sizeof(cmdLine), "\"%s\" \"%s\" \"%s\"", typstExe, typPath, pdfPath);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    DWORD exitCode = 1;
    if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 15000);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    if (exitCode == 0) {
        strncpy(outPdfPath, pdfPath, pdfPathSz - 1);
        outPdfPath[pdfPathSz - 1] = '\0';
        return 1;
    }
    return 0;
}
static int DispatchSingleQueueRow(sqlite3 *db, int dispatchId, char *outStatus, size_t statusSz, DWORD *outExitCode) {
    char channel[32] = "", dest[256] = "", msg[2048] = "", patName[128] = "", mrn[64] = "", acc[64] = "";
    char examName[128] = "", roomName[128] = "", schedAt[64] = "", refMd[128] = "", faxNum[64] = "", repStatus[32] = "";
    int reportId = 0;

    const char *sql =
        "SELECT q.channel, q.destination_endpoint, q.payload_content, "
        "       COALESCE(p.first_name || ' ' || p.last_name, ''), COALESCE(p.mrn, ''), "
        "       COALESCE(o.accession_number, ''), COALESCE(e.exam_name, ''), "
        "       COALESCE(m.room_name, ''), COALESCE(o.scheduled_at, ''), "
        "       COALESCE(u.first_name || ' ' || u.last_name, ''), COALESCE(u.fax_number, ''), "
        "       COALESCE(r.report_status, ''), COALESCE(q.report_id, 0) "
        "FROM correspondence_dispatch_queue q "
        "LEFT JOIN patients p ON q.patient_id = p.patient_id "
        "LEFT JOIN imaging_orders o ON q.order_id = o.order_id "
        "LEFT JOIN exam_catalog e ON o.exam_code = e.exam_code "
        "LEFT JOIN modalities_and_rooms m ON o.modality_id = m.modality_id "
        "LEFT JOIN staff_users u ON q.recipient_user_id = u.user_id "
        "LEFT JOIN radiology_reports r ON q.report_id = r.report_id "
        "WHERE q.dispatch_id = ?";

    sqlite3_stmt *stmt = NULL;
    int found = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, dispatchId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            found = 1;
            SanitizeArg(channel,   sizeof(channel),   (const char*)sqlite3_column_text(stmt, 0));
            SanitizeArg(dest,      sizeof(dest),      (const char*)sqlite3_column_text(stmt, 1));
            SanitizeArg(msg,       sizeof(msg),       (const char*)sqlite3_column_text(stmt, 2));
            SanitizeArg(patName,   sizeof(patName),   (const char*)sqlite3_column_text(stmt, 3));
            SanitizeArg(mrn,       sizeof(mrn),       (const char*)sqlite3_column_text(stmt, 4));
            SanitizeArg(acc,       sizeof(acc),       (const char*)sqlite3_column_text(stmt, 5));
            SanitizeArg(examName,  sizeof(examName),  (const char*)sqlite3_column_text(stmt, 6));
            SanitizeArg(roomName,  sizeof(roomName),  (const char*)sqlite3_column_text(stmt, 7));
            SanitizeArg(schedAt,   sizeof(schedAt),   (const char*)sqlite3_column_text(stmt, 8));
            SanitizeArg(refMd,     sizeof(refMd),     (const char*)sqlite3_column_text(stmt, 9));
            SanitizeArg(faxNum,    sizeof(faxNum),    (const char*)sqlite3_column_text(stmt, 10));
            SanitizeArg(repStatus, sizeof(repStatus), (const char*)sqlite3_column_text(stmt, 11));
            reportId = sqlite3_column_int(stmt, 12);
        }
        sqlite3_finalize(stmt);
    }
    if (!found) {
        snprintf(outStatus, statusSz, "NOT_FOUND");
        return 0;
    }

    if (dest[0] == '\0' || strcmp(dest, "UNKNOWN") == 0 || strcmp(dest, "N/A") == 0) {
        char holdSql[256];
        snprintf(holdSql, sizeof(holdSql),
            "UPDATE correspondence_dispatch_queue SET delivery_status='FAILED', "
            "error_message='Missing or invalid channel/destination endpoint (fix in Comms Queue)' "
            "WHERE dispatch_id=%d", dispatchId);
        sqlite3_exec(db, holdSql, 0, 0, 0);
        snprintf(outStatus, statusSz, "INVALID_CHANNEL");
        return 0;
    }

    const char *exeKey = NULL, *argsKey = NULL;
    if      (strcmp(channel, "EMAIL") == 0)          { exeKey = "rem_email_exe";  argsKey = "rem_email_args"; }
    else if (strcmp(channel, "SMS") == 0)            { exeKey = "rem_sms_exe";    argsKey = "rem_sms_args"; }
    else if (strcmp(channel, "PHONE_TTS") == 0)      { exeKey = "rem_tts_exe";    argsKey = "rem_tts_args"; }
    else if (strcmp(channel, "HL7_ORU_R01") == 0 ||
             strcmp(channel, "HL7_ORM_O01") == 0)    { exeKey = "rep_hl7_exe";    argsKey = "rep_hl7_args"; }
    else if (strcmp(channel, "HL7_FHIR_PUSH") == 0)  { exeKey = "rep_fhir_exe";   argsKey = "rep_fhir_args"; }
    else if (strcmp(channel, "FAX") == 0)            { exeKey = "rep_fax_exe";    argsKey = "rep_fax_args"; }
    else if (strcmp(channel, "PATIENT_PORTAL") == 0) { exeKey = "rep_portal_exe"; argsKey = "rep_portal_args"; }
    else {
        snprintf(outStatus, statusSz, "MANUAL_CHANNEL");
        return 0;
    }

    char exePath[512] = "", argsTpl[2048] = "";
    const char *cfgSql = "SELECT setting_key, setting_value FROM app_settings WHERE setting_key IN (?, ?)";
    if (sqlite3_prepare_v2(db, cfgSql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, exeKey, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, argsKey, -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *k = (const char*)sqlite3_column_text(stmt, 0);
            const char *v = (const char*)sqlite3_column_text(stmt, 1);
            if (k && v) {
                if (strcmp(k, exeKey) == 0) strncpy(exePath, v, sizeof(exePath) - 1);
                else if (strcmp(k, argsKey) == 0) strncpy(argsTpl, v, sizeof(argsTpl) - 1);
            }
        }
        sqlite3_finalize(stmt);
    }

    char upd[512];
    if (exePath[0] == '\0') {
        snprintf(upd, sizeof(upd),
            "UPDATE correspondence_dispatch_queue SET delivery_status='FAILED', retry_count=COALESCE(retry_count,0)+1, error_message='No executable configured for %s (%s)' WHERE dispatch_id=%d",
            channel, exeKey, dispatchId);
        sqlite3_exec(db, upd, 0, 0, 0);
        snprintf(outStatus, statusSz, "NO_EXE");
        return 0;
    }

    CreateDirectoryA("outbound_dispatches", NULL);
    char payloadFile[MAX_PATH];
    snprintf(payloadFile, sizeof(payloadFile), "outbound_dispatches\\dispatch_%d.txt", dispatchId);
    FILE *pf = fopen(payloadFile, "wb");
    if (pf) {
        if (strncmp(channel, "HL7_", 4) == 0) {
            fprintf(pf,
                "MSH|^~\\&|RIS|METRO_IMAGING|EHR|REFERRING|20261120120000||ORU^R01|MSG-%d|P|2.5.1\r\n"
                "PID|1||%s^^^RIS^MR||%s\r\n"
                "OBR|1|%s|%s|%s^%s|||20261120120000||||||||||||||%s\r\n"
                "OBX|1|TX|IMP^IMPRESSION||%s||||||%s\r\n",
                dispatchId, mrn, patName, acc, acc, examName, examName,
                repStatus[0] ? repStatus : "F", msg, repStatus[0] ? repStatus : "F");
        } else {
            fprintf(pf, "%s\r\n", msg);
        }
        fclose(pf);
    }

    char pdfPath[MAX_PATH] = "";
    if (reportId > 0) {
        GenerateReportTypstPdf(db, reportId, pdfPath, sizeof(pdfPath));
    }

    snprintf(upd, sizeof(upd), "UPDATE correspondence_dispatch_queue SET delivery_status='TRANSMITTING' WHERE dispatch_id=%d", dispatchId);
    sqlite3_exec(db, upd, 0, 0, 0);

    char cmdLine[4096];
    snprintf(cmdLine, sizeof(cmdLine), "\"%s\" %s", exePath, argsTpl);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{to}", dest);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{email}", dest);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{phone}", dest);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{fax}", faxNum[0] ? faxNum : dest);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{message}", msg);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{payload_file}", payloadFile);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{pdf_file}", pdfPath[0] ? pdfPath : payloadFile);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{patient}", patName);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{mrn}", mrn);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{accession}", acc);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{exam}", examName);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{room}", roomName);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{datetime}", schedAt);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{doctor}", refMd);
    StrReplaceAll(cmdLine, sizeof(cmdLine), cmdLine, "{status}", repStatus);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    DWORD exitCode = 1;
    BOOL ok = CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (ok) {
        WaitForSingleObject(pi.hProcess, 15000);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    if (outExitCode) *outExitCode = exitCode;
    if (ok && exitCode == 0) {
        snprintf(outStatus, statusSz, "SENT");
        snprintf(upd, sizeof(upd),
            "UPDATE correspondence_dispatch_queue SET delivery_status='SENT', retry_count=COALESCE(retry_count,0)+1, error_message=NULL, sent_at=CURRENT_TIMESTAMP WHERE dispatch_id=%d",
            dispatchId);
        sqlite3_exec(db, upd, 0, 0, 0);
        return 1;
    } else {
        snprintf(outStatus, statusSz, "FAILED");
        snprintf(upd, sizeof(upd),
            "UPDATE correspondence_dispatch_queue SET delivery_status=CASE WHEN COALESCE(retry_count,0)+1 >= 3 THEN 'PERMANENT_FAILURE' ELSE 'FAILED_RETRYING' END, "
            "retry_count=COALESCE(retry_count,0)+1, error_message='Process failed (started=%d, exit_code=%lu)', sent_at=CURRENT_TIMESTAMP WHERE dispatch_id=%d",
            ok ? 1 : 0, (unsigned long)exitCode, dispatchId);
        sqlite3_exec(db, upd, 0, 0, 0);
        return 0;
    }
}
static void ProcessOutboundDispatchQueue(void) {
    if (!g_db) return;
    const char *sql =
        "SELECT q.dispatch_id FROM correspondence_dispatch_queue q "
        "WHERE q.delivery_status IN ('QUEUED', 'FAILED_RETRYING') "
        "  AND COALESCE(q.retry_count, 0) < 3 "
        "  AND q.channel IN ('EMAIL','SMS','PHONE_TTS','HL7_ORU_R01','HL7_ORM_O01','HL7_FHIR_PUSH','FAX','PATIENT_PORTAL') "
        "  AND EXISTS ("
        "    SELECT 1 FROM app_settings s WHERE TRIM(COALESCE(s.setting_value,'')) != '' AND s.setting_key = CASE q.channel "
        "      WHEN 'EMAIL' THEN 'rem_email_exe' "
        "      WHEN 'SMS' THEN 'rem_sms_exe' "
        "      WHEN 'PHONE_TTS' THEN 'rem_tts_exe' "
        "      WHEN 'HL7_ORU_R01' THEN 'rep_hl7_exe' "
        "      WHEN 'HL7_ORM_O01' THEN 'rep_hl7_exe' "
        "      WHEN 'HL7_FHIR_PUSH' THEN 'rep_fhir_exe' "
        "      WHEN 'FAX' THEN 'rep_fax_exe' "
        "      WHEN 'PATIENT_PORTAL' THEN 'rep_portal_exe' ELSE '' END"
        "  ) "
        "ORDER BY q.queued_at ASC LIMIT 5";

    int ids[5] = {0}, cnt = 0;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, 0) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW && cnt < 5) {
            ids[cnt++] = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    for (int i = 0; i < cnt; i++) {
        char st[32];
        DispatchSingleQueueRow(g_db, ids[i], st, sizeof(st), NULL);
    }
}
/* --- Minimal Explicit VR Little Endian DICOM Tag Writers --- */
static void DcmWriteTagUS(unsigned char *buf, int *off, uint16_t g, uint16_t e, uint16_t val) {
    buf[(*off)++] = (unsigned char)(g & 0xFF); buf[(*off)++] = (unsigned char)(g >> 8);
    buf[(*off)++] = (unsigned char)(e & 0xFF); buf[(*off)++] = (unsigned char)(e >> 8);
    buf[(*off)++] = 'U'; buf[(*off)++] = 'S';
    buf[(*off)++] = 2;   buf[(*off)++] = 0;
    buf[(*off)++] = (unsigned char)(val & 0xFF); buf[(*off)++] = (unsigned char)(val >> 8);
}

static void DcmWriteTagStr(unsigned char *buf, int *off, uint16_t g, uint16_t e, const char *vr, const char *val) {
    int len = (int)strlen(val);
    int pad = (len & 1) ? 1 : 0;
    int tlen = len + pad;
    buf[(*off)++] = (unsigned char)(g & 0xFF); buf[(*off)++] = (unsigned char)(g >> 8);
    buf[(*off)++] = (unsigned char)(e & 0xFF); buf[(*off)++] = (unsigned char)(e >> 8);
    buf[(*off)++] = (unsigned char)vr[0];      buf[(*off)++] = (unsigned char)vr[1];
    buf[(*off)++] = (unsigned char)(tlen & 0xFF); buf[(*off)++] = (unsigned char)(tlen >> 8);
    memcpy(buf + *off, val, len); *off += len;
    if (pad) buf[(*off)++] = ' ';
}

/* --- SQLite <-> ris_server.h Worklist & C-STORE Bridge --- */
static void SanitizeMwlField(char *dst, size_t dstSize, const char *src) {
    size_t j = 0;
    if (!src) { dst[0] = '\0'; return; }
    for (size_t i = 0; src[i] && j < dstSize - 1; i++) {
        char c = src[i];
        if (c == ',' || c == '"' || c == '\r' || c == '\n' || c == '~') c = ' ';
        dst[j++] = c;
    }
    dst[j] = '\0';
    TrimInPlace(dst);
}

static void HandleApiAdminSetPassword(SOCKET sock, const char *body) {
    char uidStr[32] = "", pass[128] = "";
    GetFormParam(body, "user_id", uidStr, sizeof(uidStr));
    GetFormParam(body, "password", pass, sizeof(pass));
    int userId = atoi(uidStr);
    if (userId <= 0 || strlen(pass) < 6) {
        const char *e = "{\"error\":\"Valid user_id and password (min 6 chars) required\"}";
        SendHttpResponse(sock, "application/json", e, (int)strlen(e));
        return;
    }

    char salt[33], hash[65];
    GenerateRandomTokenHex(salt, 32);
    HashPasswordSalted(salt, pass, hash);

    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO user_auth_credentials (user_id, password_salt, password_hash, must_change_password, updated_at) "
        "VALUES (?, ?, ?, 0, datetime('now')) "
        "ON CONFLICT(user_id) DO UPDATE SET "
        "password_salt=excluded.password_salt, password_hash=excluded.password_hash, updated_at=datetime('now')";
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st,  1, userId);
        sqlite3_bind_text(st, 2, salt, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, hash, -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    if (sqlite3_prepare_v2(g_db, "UPDATE staff_users SET password_hash=? WHERE user_id=?", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, hash, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st,  2, userId);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    const char *ok = "{\"status\":\"PASSWORD_UPDATED\"}";
    SendHttpResponse(sock, "application/json", ok, (int)strlen(ok));
}

void HandleApiMe(SOCKET sock, const char *req) {
    char tok[128] = "";
    GetCookieParam(req, "ris_session", tok, sizeof(tok));

    sqlite3_stmt *stmt = NULL;
    char json[512] = "";

    /* 1. Check active session cookie */
    if (tok[0]) {
        const char *sSql =
            "SELECT u.user_id, u.username, u.first_name || ' ' || u.last_name, r.role_code, COALESCE(u.email,'') "
            "FROM user_sessions s "
            "JOIN staff_users u ON s.user_id = u.user_id "
            "JOIN roles r ON u.role_id = r.role_id "
            "WHERE s.session_token = ? AND s.expires_at > datetime('now') AND u.is_active = 1 LIMIT 1";
        if (sqlite3_prepare_v2(g_db, sSql, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, tok, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                int uid = sqlite3_column_int(stmt, 0);
                const char *uname = (const char*)sqlite3_column_text(stmt, 1);
                const char *fname = (const char*)sqlite3_column_text(stmt, 2);
                const char *role  = (const char*)sqlite3_column_text(stmt, 3);
                const char *email = (const char*)sqlite3_column_text(stmt, 4);
                snprintf(json, sizeof(json),
                    "{\"authenticated\":true,\"user\":\"%s\",\"user_id\":%d,\"name\":\"%s\",\"role\":\"%s\",\"email\":\"%s\",\"edit_all\":%s,\"visible_people\":\"\"}",
                    uname ? uname : "", uid, fname ? fname : "", role ? role : "", email ? email : "",
                    (role && strcmp(role, "SYSTEM_ADMIN") == 0) ? "true" : "false");
            }
            sqlite3_finalize(stmt);
        }
        if (json[0]) {
            SendHttpResponse(sock, "application/json", json, (int)strlen(json));
            return;
        }
    }

    /* 2. If any user has credentials configured, require login; otherwise fall back to bootstrap admin */
    int hasCreds = 0;
    if (sqlite3_prepare_v2(g_db, "SELECT COUNT(*) FROM user_auth_credentials", -1, &stmt, 0) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) hasCreds = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    if (hasCreds > 0) {
        const char *unauth = "{\"authenticated\":false}";
        SendHttpResponse(sock, "application/json", unauth, (int)strlen(unauth));
        return;
    }

    const char *fbSql =
        "SELECT u.user_id, u.username, u.first_name || ' ' || u.last_name, r.role_code, COALESCE(u.email,'') "
        "FROM staff_users u JOIN roles r ON u.role_id = r.role_id "
        "WHERE u.is_active = 1 ORDER BY CASE r.role_code WHEN 'SYSTEM_ADMIN' THEN 1 WHEN 'RADIOLOGIST' THEN 2 ELSE 3 END, u.user_id LIMIT 1";
    strcpy(json, "{\"authenticated\":true,\"user\":\"admin\",\"user_id\":1,\"name\":\"Admin\",\"role\":\"SYSTEM_ADMIN\",\"email\":\"\",\"edit_all\":true,\"visible_people\":\"\"}");
    if (sqlite3_prepare_v2(g_db, fbSql, -1, &stmt, 0) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int uid = sqlite3_column_int(stmt, 0);
            const char *uname = (const char*)sqlite3_column_text(stmt, 1);
            const char *fname = (const char*)sqlite3_column_text(stmt, 2);
            const char *role  = (const char*)sqlite3_column_text(stmt, 3);
            const char *email = (const char*)sqlite3_column_text(stmt, 4);
            snprintf(json, sizeof(json),
                "{\"authenticated\":true,\"user\":\"%s\",\"user_id\":%d,\"name\":\"%s\",\"role\":\"%s\",\"email\":\"%s\",\"edit_all\":true,\"visible_people\":\"\"}",
                uname ? uname : "admin", uid, fname ? fname : "Admin", role ? role : "SYSTEM_ADMIN", email ? email : "");
        }
        sqlite3_finalize(stmt);
    }
    SendHttpResponse(sock, "application/json", json, (int)strlen(json));
}

static void HandleApiResetPassword(SOCKET sock, const char *body) {
    char tok[128] = "", newPass[128] = "";
    GetFormParam(body, "token", tok, sizeof(tok));
    GetFormParam(body, "new_password", newPass, sizeof(newPass));
    TrimInPlace(tok);

    if (!tok[0] || strlen(newPass) < 6) {
        const char *e = "{\"error\":\"Reset token and new password (min 6 chars) are required\"}";
        SendHttpResponse(sock, "application/json", e, (int)strlen(e));
        return;
    }

    int tokenId = 0, userId = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(g_db,
            "SELECT token_id, user_id FROM password_reset_tokens "
            "WHERE reset_token = ? AND used_at IS NULL AND expires_at > datetime('now') LIMIT 1",
            -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, tok, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            tokenId = sqlite3_column_int(st, 0);
            userId  = sqlite3_column_int(st, 1);
        }
        sqlite3_finalize(st);
    }
    if (tokenId <= 0 || userId <= 0) {
        const char *e = "{\"error\":\"Invalid or expired reset token\"}";
        SendHttpResponse(sock, "application/json", e, (int)strlen(e));
        return;
    }

    char salt[33], hash[65];
    GenerateRandomTokenHex(salt, 32);
    HashPasswordSalted(salt, newPass, hash);

    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO user_auth_credentials (user_id, password_salt, password_hash, must_change_password, updated_at) "
            "VALUES (?, ?, ?, 0, datetime('now')) "
            "ON CONFLICT(user_id) DO UPDATE SET password_salt=excluded.password_salt, password_hash=excluded.password_hash, updated_at=datetime('now')",
            -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st,  1, userId);
        sqlite3_bind_text(st, 2, salt, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, hash, -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    char upd[256];
    snprintf(upd, sizeof(upd), "UPDATE password_reset_tokens SET used_at=datetime('now') WHERE token_id=%d;", tokenId);
    sqlite3_exec(g_db, upd, 0, 0, 0);
    snprintf(upd, sizeof(upd), "DELETE FROM user_sessions WHERE user_id=%d;", userId);
    sqlite3_exec(g_db, upd, 0, 0, 0);

    const char *ok = "{\"status\":\"PASSWORD_RESET_OK\"}";
    SendHttpResponse(sock, "application/json", ok, (int)strlen(ok));
}
static void HandleApiRequestPasswordReset(SOCKET sock, const char *body) {
    char ident[128] = "";
    GetFormParam(body, "identifier", ident, sizeof(ident));
    TrimInPlace(ident);
    if (!ident[0]) {
        const char *e = "{\"error\":\"Enter a username or email address\"}";
        SendHttpResponse(sock, "application/json", e, (int)strlen(e));
        return;
    }

    int userId = 0;
    char email[128] = "", uname[64] = "", fullName[128] = "";
    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT user_id, username, first_name || ' ' || last_name, COALESCE(email,'') "
        "FROM staff_users WHERE is_active = 1 AND (LOWER(username)=LOWER(?) OR LOWER(COALESCE(email,''))=LOWER(?)) LIMIT 1";
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, ident, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, ident, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            userId = sqlite3_column_int(st, 0);
            const char *s;
            if ((s = (const char*)sqlite3_column_text(st, 1))) strncpy(uname, s, sizeof(uname) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 2))) strncpy(fullName, s, sizeof(fullName) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 3))) strncpy(email, s, sizeof(email) - 1);
        }
        sqlite3_finalize(st);
    }

    if (userId <= 0 || !email[0]) {
        const char *e = "{\"error\":\"No active account with a valid email address found\"}";
        SendHttpResponse(sock, "application/json", e, (int)strlen(e));
        return;
    }

    char resetTok[33];
    GenerateRandomTokenHex(resetTok, 32);

    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO password_reset_tokens (user_id, reset_token, expires_at) VALUES (?, ?, datetime('now', '+30 minutes'))",
            -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st,  1, userId);
        sqlite3_bind_text(st, 2, resetTok, -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    /* Queue reset email through existing correspondence_dispatch_queue (rem_email_exe) */
    char msg[512];
    snprintf(msg, sizeof(msg),
        "RIS Password Reset for %s (%s). Reset Token (valid 30 mins): %s",
        fullName, uname, resetTok);

    int dispatchId = 0;
    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO correspondence_dispatch_queue "
            "(recipient_user_id, channel, destination_endpoint, payload_content, delivery_status) "
            "VALUES (?, 'EMAIL', ?, ?, 'QUEUED')", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st,  1, userId);
        sqlite3_bind_text(st, 2, email, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, msg,   -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_DONE) dispatchId = (int)sqlite3_last_insert_rowid(g_db);
        sqlite3_finalize(st);
    }

    char dispStatus[32] = "QUEUED";
    if (dispatchId > 0) {
        DispatchSingleQueueRow(g_db, dispatchId, dispStatus, sizeof(dispStatus), NULL);
    }

    char resp[256];
    snprintf(resp, sizeof(resp), "{\"status\":\"RESET_QUEUED\",\"email\":\"%s\",\"dispatch_status\":\"%s\"}", email, dispStatus);
    SendHttpResponse(sock, "application/json", resp, (int)strlen(resp));
}

static void HandleApiLogout(SOCKET sock, const char *req) {
    char tok[128] = "";
    if (GetCookieParam(req, "ris_session", tok, sizeof(tok)) && tok[0]) {
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(g_db, "DELETE FROM user_sessions WHERE session_token=?", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, tok, -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    SendJsonWithCookie(sock, "{\"status\":\"LOGGED_OUT\"}",
                       "Set-Cookie: ris_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax\r\n");
}

static void HandleApiLogin(SOCKET sock, const char *body) {
    char ident[128] = "", pass[128] = "";
    GetFormParam(body, "username", ident, sizeof(ident));
    GetFormParam(body, "password", pass, sizeof(pass));
    TrimInPlace(ident);
    if (!ident[0] || !pass[0]) {
        const char *e = "{\"error\":\"Username/email and password are required\"}";
        SendHttpResponse(sock, "application/json", e, (int)strlen(e));
        return;
    }

    int userId = 0;
    char salt[64] = "", storedHash[80] = "", uname[64] = "", fullName[128] = "", role[64] = "";
    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT u.user_id, u.username, u.first_name || ' ' || u.last_name, r.role_code, c.password_salt, c.password_hash "
        "FROM staff_users u "
        "JOIN roles r ON u.role_id = r.role_id "
        "JOIN user_auth_credentials c ON u.user_id = c.user_id "
        "WHERE u.is_active = 1 AND (LOWER(u.username) = LOWER(?) OR LOWER(COALESCE(u.email,'')) = LOWER(?)) LIMIT 1";
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, ident, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, ident, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            userId = sqlite3_column_int(st, 0);
            const char *s;
            if ((s = (const char*)sqlite3_column_text(st, 1))) strncpy(uname, s, sizeof(uname) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 2))) strncpy(fullName, s, sizeof(fullName) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 3))) strncpy(role, s, sizeof(role) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 4))) strncpy(salt, s, sizeof(salt) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 5))) strncpy(storedHash, s, sizeof(storedHash) - 1);
        }
        sqlite3_finalize(st);
    }

    char calcHash[65];
    HashPasswordSalted(salt, pass, calcHash);
    if (userId <= 0 || strcmp(calcHash, storedHash) != 0) {
        const char *e = "{\"error\":\"Invalid username/email or password\"}";
        SendHttpResponse(sock, "application/json", e, (int)strlen(e));
        return;
    }

    char sessTok[49];
    GenerateRandomTokenHex(sessTok, 48);
    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO user_sessions (session_token, user_id, expires_at) VALUES (?, ?, datetime('now', '+12 hours'))",
            -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, sessTok, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st,  2, userId);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    if (sqlite3_prepare_v2(g_db, "UPDATE user_auth_credentials SET last_login_at=datetime('now') WHERE user_id=?", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, userId);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    char cookieHdr[192], resp[384];
    snprintf(cookieHdr, sizeof(cookieHdr), "Set-Cookie: ris_session=%s; Path=/; HttpOnly; SameSite=Lax\r\n", sessTok);
    snprintf(resp, sizeof(resp), "{\"authenticated\":true,\"user_id\":%d,\"user\":\"%s\",\"name\":\"%s\",\"role\":\"%s\"}",
             userId, uname, fullName, role);
    SendJsonWithCookie(sock, resp, cookieHdr);
}

static void ResetCStoreTrackingFile(void) {
    EnterCriticalSection(&g_cstoreLock);
    FILE *fp = fopen("c-store.csv", "w");
    if (fp) {
        fprintf(fp, "\"Accession\",\"PatientName\",\"PatientID\",\"StudyUID\",\"SOPInstanceUID\",\"Folder\",\"Filename\",\"Modality\",\"FileSize\",\"Timestamp\"\n");
        fclose(fp);
    }
    g_lastCStoreFileSize = -1;
    LeaveCriticalSection(&g_cstoreLock);
}

static const char* InferDicomVR(uint16_t g, uint16_t e) {
    if (g == 0x0008) {
        if (e == 0x0016 || e == 0x0018) return "UI";
        if (e == 0x0020 || e == 0x0021 || e == 0x0022 || e == 0x0023) return "DA";
        if (e == 0x0030 || e == 0x0031 || e == 0x0032 || e == 0x0033) return "TM";
        if (e == 0x0050) return "SH";
        if (e == 0x0060 || e == 0x0008) return "CS";
        if (e == 0x0090 || e == 0x1050 || e == 0x1070) return "PN";
        return "LO";
    }
    if (g == 0x0010) {
        if (e == 0x0010) return "PN";
        if (e == 0x0030) return "DA";
        if (e == 0x0040) return "CS";
        if (e == 0x1010) return "AS";
        if (e == 0x1020 || e == 0x1030) return "DS";
        return "LO";
    }
    if (g == 0x0018) {
        if (e == 0x0015 || e == 0x5100) return "CS";
        if (e == 0x0050 || e == 0x0060 || e == 0x1152) return "DS";
        return "LO";
    }
    if (g == 0x0020) {
        if (e == 0x000D || e == 0x000E) return "UI";
        if (e == 0x0010) return "SH";
        if (e == 0x0011 || e == 0x0012 || e == 0x0013) return "IS";
        if (e == 0x0060 || e == 0x0062) return "CS";
        return "LO";
    }
    return "LO";
}

static void HandleUploadUnlinked(SOCKET sock, const char *pBuf, int totalLen) {
    const char *hdrEnd = strstr(pBuf, "\r\n\r\n");
    if (!hdrEnd) {
        char err[] = "{\"error\":\"Invalid upload request\"}";
        SendHttpResponse(sock, "application/json", err, (int)strlen(err));
        return;
    }

    const char *body = hdrEnd + 4;
    int fileLen = totalLen - (int)(body - pBuf);
    char *clPtr = StrIStr(pBuf, "Content-Length:");
    if (clPtr) {
        int cl = atoi(clPtr + 15);
        if (cl > 0 && cl <= fileLen) fileLen = cl;
    }

    if (fileLen <= 0) {
        char err[] = "{\"error\":\"Empty image payload\"}";
        SendHttpResponse(sock, "application/json", err, (int)strlen(err));
        return;
    }

    char folder[32], filename[128], path[MAX_PATH];
    AllocateImageFile(folder, filename, path);

    FILE *f = fopen(path, "wb");
    if (!f) {
        char err[] = "{\"error\":\"Failed to save uploaded image\"}";
        SendHttpResponse(sock, "application/json", err, (int)strlen(err));
        return;
    }
    fwrite(body, 1, (size_t)fileLen, f);
    fclose(f);

    char acc[64] = "", pid[64] = "", pname[128] = "", mod[32] = "", studyUid[128] = "", sopUid[128] = "";
    GetQueryParam(pBuf, "acc",   acc,      sizeof(acc));
    GetQueryParam(pBuf, "pid",   pid,      sizeof(pid));
    GetQueryParam(pBuf, "pname", pname,    sizeof(pname));
    GetQueryParam(pBuf, "mod",   mod,      sizeof(mod));
    GetQueryParam(pBuf, "suid",  studyUid, sizeof(studyUid));
    GetQueryParam(pBuf, "sop",   sopUid,   sizeof(sopUid));

    TrimInPlace(acc); TrimInPlace(pid); TrimInPlace(pname); TrimInPlace(mod);
    if (!mod[0]) strcpy(mod, "OT");

    /* Always generate a guaranteed unique SOP UID if missing or duplicate */
    g_unlinkedUploadSeq++;
    if (!sopUid[0]) {
        snprintf(sopUid, sizeof(sopUid), "1.2.826.0.1.3680043.8.498.U.%lu.%u",
                 (unsigned long)GetTickCount(), g_unlinkedUploadSeq);
    }

    sqlite3_stmt *st = NULL;
    const char *qsql =
        "INSERT OR IGNORE INTO dicom_unlinked_queue "
        "(sop_instance_uid, study_instance_uid, raw_accession, raw_patient_id, raw_patient_name, modality, file_path) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(g_db, qsql, -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, sopUid,   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, studyUid, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, acc,      -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, pid,      -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, pname,    -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 6, mod,      -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 7, path,     -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    char resp[256];
    int rlen = snprintf(resp, sizeof(resp), "{\"status\":\"QUEUED\",\"sop\":\"%s\"}", sopUid);
    SendHttpResponse(sock, "application/json", resp, rlen);
}
static int InsertMissingDicomTagsFile(const char *path, const char *rulesStr) {
    if (!path || !path[0] || !rulesStr || !rulesStr[0]) return 0;

    DcmTagRule rules[64];
    int ruleCnt = 0;
    char copy[4096];
    strncpy(copy, rulesStr, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *tok = strtok(copy, "`");
    while (tok && ruleCnt < 64) {
        char *eq = strchr(tok, '=');
        if (eq) {
            *eq = '\0';
            unsigned int g = 0, e = 0;
            char vrOpt[8] = "";
            int parsed = sscanf(tok, "%x,%x,%2s", &g, &e, vrOpt);
            if (parsed < 2) parsed = sscanf(tok, "0x%x,0x%x,%2s", &g, &e, vrOpt);
            if (parsed >= 2 && g >= 0x0008 && g < 0x7FE0) {
                rules[ruleCnt].g = (uint16_t)g;
                rules[ruleCnt].e = (uint16_t)e;
                const char *vr = (parsed >= 3 && vrOpt[0] && vrOpt[1]) ? vrOpt : InferDicomVR((uint16_t)g, (uint16_t)e);
                rules[ruleCnt].vr[0] = vr[0]; rules[ruleCnt].vr[1] = vr[1]; rules[ruleCnt].vr[2] = '\0';
                strncpy(rules[ruleCnt].val, eq + 1, sizeof(rules[ruleCnt].val) - 1);
                rules[ruleCnt].val[sizeof(rules[ruleCnt].val) - 1] = '\0';
                rules[ruleCnt].found = 0;
                ruleCnt++;
            }
        }
        tok = strtok(NULL, "`");
    }
    if (ruleCnt == 0) return 0;

    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 136 || sz > 64 * 1024 * 1024) { fclose(fp); return 0; }

    unsigned char *buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(fp); return 0; }
    fread(buf, 1, (size_t)sz, fp);
    fclose(fp);

    /* Detect Explicit vs Implicit VR and scan existing tags up to PixelData (7FE0,0010) */
    long off = (memcmp(buf + 128, "DICM", 4) == 0) ? 132 : 0;
    long insertPos = -1;
    int isExplicit = 1;

    while (off + 8 <= sz) {
        uint16_t g = (uint16_t)(buf[off] | (buf[off + 1] << 8));
        uint16_t e = (uint16_t)(buf[off + 2] | (buf[off + 3] << 8));
        if (g == 0x7FE0 && e == 0x0010) { insertPos = off; break; }

        for (int i = 0; i < ruleCnt; i++) {
            if (rules[i].g == g && rules[i].e == e) rules[i].found = 1;
        }

        unsigned char c1 = buf[off + 4], c2 = buf[off + 5];
        int looksVR = (c1 >= 'A' && c1 <= 'Z' && c2 >= 'A' && c2 <= 'Z');
        if (g >= 0x0008 && !looksVR) isExplicit = 0;

        uint32_t vlen = 0;
        long hdrSz = 8;
        if (g == 0x0002 || (looksVR && isExplicit)) {
            if ((c1=='O'&&c2=='B')||(c1=='O'&&c2=='W')||(c1=='O'&&c2=='F')||(c1=='S'&&c2=='Q')||
                (c1=='U'&&c2=='T')||(c1=='U'&&c2=='N')||(c1=='U'&&c2=='C')||(c1=='U'&&c2=='R')) {
                if (off + 12 > sz) break;
                memcpy(&vlen, buf + off + 8, 4);
                hdrSz = 12;
            } else {
                vlen = (uint32_t)(buf[off + 6] | (buf[off + 7] << 8));
                hdrSz = 8;
            }
        } else {
            memcpy(&vlen, buf + off + 4, 4);
            hdrSz = 8;
        }

        if (vlen == 0xFFFFFFFF) {
            /* Skip undefined-length sequence until Sequence Delimitation Item (FFFE,E0DD) */
            long p = off + hdrSz;
            while (p + 8 <= sz) {
                uint16_t sg = (uint16_t)(buf[p] | (buf[p + 1] << 8));
                uint16_t se = (uint16_t)(buf[p + 2] | (buf[p + 3] << 8));
                if (sg == 0xFFFE && se == 0xE0DD) { p += 8; break; }
                p += 2;
            }
            off = p;
            continue;
        }
        if (off + hdrSz + (long)vlen > sz) break;
        off += hdrSz + (long)vlen;
    }

    /* Count missing tags */
    int missing = 0;
    for (int i = 0; i < ruleCnt; i++) if (!rules[i].found) missing++;
    if (missing == 0) { free(buf); return 0; }
    if (insertPos < 0) insertPos = off;

    /* Sort missing rules by (group, element) */
    for (int i = 0; i < ruleCnt - 1; i++) {
        for (int j = i + 1; j < ruleCnt; j++) {
            uint32_t k1 = ((uint32_t)rules[i].g << 16) | rules[i].e;
            uint32_t k2 = ((uint32_t)rules[j].g << 16) | rules[j].e;
            if (k2 < k1) { DcmTagRule tmp = rules[i]; rules[i] = rules[j]; rules[j] = tmp; }
        }
    }

    /* Build serialized missing tags buffer */
    unsigned char addBuf[16384];
    int addLen = 0;
    for (int i = 0; i < ruleCnt; i++) {
        if (rules[i].found) continue;
        int len = (int)strlen(rules[i].val);
        int pad = (len & 1) ? 1 : 0;
        uint32_t tlen = (uint32_t)(len + pad);
        if (addLen + (int)tlen + 16 >= (int)sizeof(addBuf)) break;

        if (isExplicit) {
            DcmWriteTagStr(addBuf, &addLen, rules[i].g, rules[i].e, rules[i].vr, rules[i].val);
        } else {
            addBuf[addLen++] = (unsigned char)(rules[i].g & 0xFF); addBuf[addLen++] = (unsigned char)(rules[i].g >> 8);
            addBuf[addLen++] = (unsigned char)(rules[i].e & 0xFF); addBuf[addLen++] = (unsigned char)(rules[i].e >> 8);
            memcpy(addBuf + addLen, &tlen, 4); addLen += 4;
            memcpy(addBuf + addLen, rules[i].val, (size_t)len); addLen += len;
            if (pad) addBuf[addLen++] = ' ';
        }
    }

    char tmpPath[MAX_PATH];
    snprintf(tmpPath, sizeof(tmpPath), "%s.ins", path);
    FILE *out = fopen(tmpPath, "wb");
    if (!out) { free(buf); return 0; }
    fwrite(buf, 1, (size_t)insertPos, out);
    fwrite(addBuf, 1, (size_t)addLen, out);
    fwrite(buf + insertPos, 1, (size_t)(sz - insertPos), out);
    fclose(out);
    free(buf);

    remove(path);
    rename(tmpPath, path);
    return missing;
}
static void SyncWorklistFromDB(void) {
    if (!g_db) return;

    EnsureCsvInitialized();
    EnterCriticalSection(&g_csvLock);
    strncpy(g_csvData[0], szCSVHeader, LINE_SIZE - 1);
    g_csvData[0][LINE_SIZE - 1] = '\0';
    g_csvRows = 1;
    LeaveCriticalSection(&g_csvLock);

    const char *sql =
        "SELECT "
        "  UPPER(p.last_name || '^' || p.first_name || COALESCE('^' || p.middle_name, '')), "
        "  p.mrn, "
        "  REPLACE(substr(p.dob, 1, 10), '-', ''), "
        "  p.biological_sex, "
        "  COALESCE(ref.last_name || '^' || ref.first_name, ''), "
        "  COALESCE(f.facility_name, o.patient_class), "
        "  ec.exam_name, "
        "  o.clinical_indication, "
        "  o.accession_number, "
        "  o.exam_code, "
        "  CASE o.priority WHEN 'CODE_STROKE_TRAUMA' THEN 'STAT' WHEN 'STAT' THEN 'STAT' WHEN 'URGENT' THEN 'HIGH' ELSE 'ROUTINE' END, "
        "  COALESCE(m.ae_title, 'ANY_SCP'), "
        "  REPLACE(substr(COALESCE(o.scheduled_at, o.ordered_at), 1, 10), '-', ''), "
        "  REPLACE(substr(COALESCE(o.scheduled_at, o.ordered_at), 12, 8), ':', ''), "
        "  COALESCE(rad.last_name || '^' || rad.first_name, tech.last_name || '^' || tech.first_name, ''), "
        "  COALESCE(sp.protocol_name, ec.exam_name), "
        "  COALESCE(o.sps_id, 'SPS-' || o.order_id), "
        "  COALESCE(m.room_name, ''), "
        "  COALESCE(f.facility_code, ''), "
        "  CASE o.workflow_status "
        "    WHEN 'SCHEDULED' THEN '2' "
        "    WHEN 'ARRIVED_WAITING' THEN '3' "
        "    WHEN 'IN_PREP_SCREENING' THEN '3' "
        "    WHEN 'READY_FOR_SCAN' THEN '3' "
        "    WHEN 'IN_PROGRESS' THEN '4' "
        "    ELSE '5' END, "
        "  ec.modality_type, "
        "  COALESCE(ref.last_name || '^' || ref.first_name, ''), "
        "  ec.exam_name, "
        "  COALESCE(sp.protocol_code, o.exam_code), "
        "  'RIS', "
        "  COALESCE(sp.protocol_name, ec.exam_name), "
        "  COALESCE(o.preassigned_study_uid, '1.2.826.0.1.3680043.8.498.' || o.order_id) "
        "FROM imaging_orders o "
        "JOIN patients p ON o.patient_id = p.patient_id "
        "JOIN exam_catalog ec ON o.exam_code = ec.exam_code "
        "LEFT JOIN staff_users ref  ON o.referring_md_id = ref.user_id "
        "LEFT JOIN staff_users rad  ON o.assigned_radiologist_id = rad.user_id "
        "LEFT JOIN staff_users tech ON o.performing_tech_id = tech.user_id "
        "LEFT JOIN modalities_and_rooms m ON o.modality_id = m.modality_id "
        "LEFT JOIN facilities f ON o.facility_id = f.facility_id "
        "LEFT JOIN scanner_protocols sp ON o.assigned_protocol_id = sp.protocol_id "
        "WHERE o.workflow_status IN ('SCHEDULED', 'ARRIVED_WAITING', 'IN_PREP_SCREENING', 'READY_FOR_SCAN', 'IN_PROGRESS') "
        "ORDER BY COALESCE(o.scheduled_at, o.ordered_at) ASC";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, 0) != SQLITE_OK) return;

    char line[LINE_SIZE];
    char fld[512];
    int count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        line[0] = '\0';
        size_t pos = 0;
        for (int c = 0; c < 27; c++) {
            const char *txt = (const char*)sqlite3_column_text(stmt, c);
            SanitizeMwlField(fld, sizeof(fld), txt ? txt : "");
            if (c > 0 && pos < sizeof(line) - 2) line[pos++] = ',';
            size_t flen = strlen(fld);
            if (pos + flen < sizeof(line) - 2) {
                memcpy(line + pos, fld, flen);
                pos += flen;
            }
        }
        line[pos] = '\0';
        ris_dicom_upsert_worklist_csv(line, NULL);
        count++;
    }
    sqlite3_finalize(stmt);

    char logBuf[128];
    snprintf(logBuf, sizeof(logBuf), "MWL Sync: Loaded %d active worklist entries from ris.db", count);
    write_log(logBuf);
}

static void HandleRelinkDicom(SOCKET sock, const char *req, int totalLen) {
    char qidsStr[2048] = "", oidStr[32] = "", refMdStr[32] = "";
    char destCh[64] = "", destEp[256] = "";
    GetQueryParam(req, "queue_ids", qidsStr, sizeof(qidsStr));
    if (!qidsStr[0]) GetQueryParam(req, "queue_id", qidsStr, sizeof(qidsStr));
    GetQueryParam(req, "order_id", oidStr, sizeof(oidStr));
    GetQueryParam(req, "ref_md_id", refMdStr, sizeof(refMdStr));
    GetQueryParam(req, "dest_channel", destCh, sizeof(destCh));
    GetQueryParam(req, "dest_endpoint", destEp, sizeof(destEp));

    int orderId = atoi(oidStr);
    int refMdId = atoi(refMdStr);

    if (!qidsStr[0] || orderId <= 0) {
        char err[] = "{\"error\":\"Missing queue_ids or order_id\"}";
        SendHttpResponse(sock, "application/json", err, (int)strlen(err));
        return;
    }

    /* Extract optional custom tag overrides from POST body (backtick-delimited 0xGGGG,0xEEEE=VAL) */
    char extraTags[2048] = "";
    const char *hdrEnd = strstr(req, "\r\n\r\n");
    if (hdrEnd) {
        const char *body = hdrEnd + 4;
        int bodyLen = totalLen - (int)(body - req);
        char *clPtr = StrIStr(req, "Content-Length:");
        if (clPtr) {
            int cl = atoi(clPtr + 15);
            if (cl >= 0 && cl < bodyLen) bodyLen = cl;
        }
        if (bodyLen > 0) {
            if (bodyLen >= (int)sizeof(extraTags)) bodyLen = (int)sizeof(extraTags) - 1;
            memcpy(extraTags, body, (size_t)bodyLen);
            extraTags[bodyLen] = '\0';
            TrimInPlace(extraTags);
        }
    }

    /* Apply optional Referring MD & Destination overrides to DB before reading order tags */
    sqlite3_stmt *st = NULL;
    if (refMdId > 0) {
        if (sqlite3_prepare_v2(g_db, "UPDATE imaging_orders SET referring_md_id=? WHERE order_id=?", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, refMdId);
            sqlite3_bind_int(st, 2, orderId);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    } else {
        if (sqlite3_prepare_v2(g_db, "SELECT referring_md_id FROM imaging_orders WHERE order_id=?", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, orderId);
            if (sqlite3_step(st) == SQLITE_ROW) refMdId = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
    }

    TrimInPlace(destCh); TrimInPlace(destEp);
    if (refMdId > 0 && destCh[0]) {
        if (sqlite3_prepare_v2(g_db, "UPDATE staff_users SET preferred_comm_channel=? WHERE user_id=?", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, destCh, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 2, refMdId);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    if (refMdId > 0 && destEp[0]) {
        const char *colSql = (strcmp(destCh, "EMAIL") == 0)
            ? "UPDATE staff_users SET email=? WHERE user_id=?"
            : "UPDATE staff_users SET fax_number=? WHERE user_id=?";
        if (sqlite3_prepare_v2(g_db, colSql, -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, destEp, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 2, refMdId);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    if (destCh[0] || destEp[0]) {
        if (sqlite3_prepare_v2(g_db,
                "UPDATE correspondence_dispatch_queue SET "
                "channel=CASE WHEN ?!='' THEN ? ELSE channel END, "
                "destination_endpoint=CASE WHEN ?!='' THEN ? ELSE destination_endpoint END "
                "WHERE order_id=? AND delivery_status IN ('QUEUED','FAILED','FAILED_RETRYING')", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, destCh, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, destCh, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, destEp, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, destEp, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st,  5, orderId);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }

    char acc[64] = "", mrn[64] = "", patName[128] = "", dob[32] = "", sex[8] = "";
    char refp[128] = "", desc[128] = "", mod[32] = "OT", suid[128] = "";
    if (sqlite3_prepare_v2(g_db,
            "SELECT o.accession_number, p.mrn, UPPER(p.last_name || '^' || p.first_name), REPLACE(substr(p.dob,1,10),'-',''), "
            "       p.biological_sex, COALESCE(ref.last_name || '^' || ref.first_name, ''), ec.exam_name, ec.modality_type, "
            "       COALESCE(o.preassigned_study_uid, '1.2.826.0.1.3680043.8.498.' || o.order_id) "
            "FROM imaging_orders o "
            "JOIN patients p ON o.patient_id = p.patient_id "
            "JOIN exam_catalog ec ON o.exam_code = ec.exam_code "
            "LEFT JOIN staff_users ref ON o.referring_md_id = ref.user_id "
            "WHERE o.order_id=?",
            -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, orderId);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char *s;
            if ((s = (const char*)sqlite3_column_text(st, 0))) strncpy(acc,     s, sizeof(acc) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 1))) strncpy(mrn,     s, sizeof(mrn) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 2))) strncpy(patName, s, sizeof(patName) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 3))) strncpy(dob,     s, sizeof(dob) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 4))) strncpy(sex,     s, sizeof(sex) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 5))) strncpy(refp,    s, sizeof(refp) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 6))) strncpy(desc,    s, sizeof(desc) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 7))) strncpy(mod,     s, sizeof(mod) - 1);
            if ((s = (const char*)sqlite3_column_text(st, 8))) strncpy(suid,    s, sizeof(suid) - 1);
        }
        sqlite3_finalize(st);
    }
    if (!acc[0]) {
        char err[] = "{\"error\":\"Target order not found\"}";
        SendHttpResponse(sock, "application/json", err, (int)strlen(err));
        return;
    }

    char sql[1024];
    snprintf(sql, sizeof(sql),
        "INSERT OR IGNORE INTO dicom_studies "
        "(order_id, study_instance_uid, pacs_wado_url, pacs_zero_footprint_viewer_url, total_series_count, total_instance_count) "
        "VALUES (%d, '%s', '/api/images?acc=%s', 'embedded:%s', 1, 0);",
        orderId, suid, acc, acc);
    sqlite3_exec(g_db, sql, 0, 0, 0);

    int studyId = 0, seriesId = 0;
    if (sqlite3_prepare_v2(g_db, "SELECT study_id FROM dicom_studies WHERE order_id=? LIMIT 1", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, orderId);
        if (sqlite3_step(st) == SQLITE_ROW) studyId = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    if (studyId > 0) {
        if (sqlite3_prepare_v2(g_db,
                "INSERT OR IGNORE INTO dicom_series (study_id, series_instance_uid, series_number, modality, series_description, instance_count) "
                "VALUES (?, ?, 1, ?, ?, 0);", -1, &st, 0) == SQLITE_OK) {
            char serUid[160]; snprintf(serUid, sizeof(serUid), "%s.1", suid);
            sqlite3_bind_int(st,  1, studyId);
            sqlite3_bind_text(st, 2, serUid, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, mod,    -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, desc,   -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        if (sqlite3_prepare_v2(g_db, "SELECT series_id FROM dicom_series WHERE study_id=? ORDER BY series_number ASC LIMIT 1", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, studyId);
            if (sqlite3_step(st) == SQLITE_ROW) seriesId = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
    }

    /* Combine standard RIS tags + user custom tag overrides */
    char rules[3584];
    snprintf(rules, sizeof(rules),
        "0x0008,0x0050=%s`0x0010,0x0010=%s`0x0010,0x0020=%s`0x0010,0x0030=%s`0x0010,0x0040=%s`0x0008,0x0060=%s`0x0008,0x0090=%s`0x0008,0x1030=%s`0x0020,0x000D=%s",
        acc, patName, mrn, dob, sex, mod, refp, desc, suid);
    if (extraTags[0]) {
        strncat(rules, "`", sizeof(rules) - strlen(rules) - 1);
        strncat(rules, extraTags, sizeof(rules) - strlen(rules) - 1);
    }

    int linkedCount = 0;
    char *tok = strtok(qidsStr, ",");
    while (tok) {
        int queueId = atoi(tok);
        tok = strtok(NULL, ",");
        if (queueId <= 0) continue;

        char sopUid[128] = "", filePath[MAX_PATH] = "";
        if (sqlite3_prepare_v2(g_db,
                "SELECT sop_instance_uid, file_path FROM dicom_unlinked_queue WHERE queue_id=? AND status='UNLINKED'",
                -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, queueId);
            if (sqlite3_step(st) == SQLITE_ROW) {
                const char *s0 = (const char*)sqlite3_column_text(st, 0);
                const char *s1 = (const char*)sqlite3_column_text(st, 1);
                if (s0) strncpy(sopUid,   s0, sizeof(sopUid) - 1);
                if (s1) strncpy(filePath, s1, sizeof(filePath) - 1);
            }
            sqlite3_finalize(st);
        }
        if (!sopUid[0] || !filePath[0]) continue;

        /* 1. Edit existing tags; 2. Insert any requested tags not present in original image */
        char tmpPath[MAX_PATH];
        snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", filePath);
        if (apply_dicom_edits_file(filePath, tmpPath, rules, "`")) {
            remove(filePath);
            rename(tmpPath, filePath);
        } else {
            remove(tmpPath);
        }
        InsertMissingDicomTagsFile(filePath, rules);

        long fsize = 0;
        FILE *fpSz = fopen(filePath, "rb");
        if (fpSz) { fseek(fpSz, 0, SEEK_END); fsize = ftell(fpSz); fclose(fpSz); }

        if (studyId > 0 && seriesId > 0) {
            int nextInst = 1;
            if (sqlite3_prepare_v2(g_db, "SELECT COALESCE(MAX(instance_number),0)+1 FROM dicom_instances WHERE study_id=?", -1, &st, 0) == SQLITE_OK) {
                sqlite3_bind_int(st, 1, studyId);
                if (sqlite3_step(st) == SQLITE_ROW) nextInst = sqlite3_column_int(st, 0);
                sqlite3_finalize(st);
            }
            if (sqlite3_prepare_v2(g_db,
                    "INSERT OR IGNORE INTO dicom_instances (series_id, study_id, sop_instance_uid, instance_number, modality, file_path, file_size_bytes) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?);", -1, &st, 0) == SQLITE_OK) {
                sqlite3_bind_int(st,  1, seriesId);
                sqlite3_bind_int(st,  2, studyId);
                sqlite3_bind_text(st, 3, sopUid,   -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(st,  4, nextInst);
                sqlite3_bind_text(st, 5, mod,      -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 6, filePath, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(st,  7, (int)fsize);
                sqlite3_step(st);
                sqlite3_finalize(st);
            }
        }

        snprintf(sql, sizeof(sql), "UPDATE dicom_unlinked_queue SET status='LINKED' WHERE queue_id=%d;", queueId);
        sqlite3_exec(g_db, sql, 0, 0, 0);
        linkedCount++;
    }

    if (studyId > 0 && seriesId > 0) {
        snprintf(sql, sizeof(sql),
            "UPDATE dicom_series SET instance_count=(SELECT COUNT(*) FROM dicom_instances WHERE series_id=%d) WHERE series_id=%d;",
            seriesId, seriesId);
        sqlite3_exec(g_db, sql, 0, 0, 0);

        snprintf(sql, sizeof(sql),
            "UPDATE dicom_studies SET "
            "total_series_count=(SELECT COUNT(*) FROM dicom_series WHERE study_id=%d), "
            "total_instance_count=(SELECT COALESCE(SUM(instance_count),0) FROM dicom_series WHERE study_id=%d) "
            "WHERE study_id=%d;", studyId, studyId, studyId);
        sqlite3_exec(g_db, sql, 0, 0, 0);
    }

    if (linkedCount > 0) {
        snprintf(sql, sizeof(sql),
            "INSERT OR IGNORE INTO exam_safety_screenings "
            "(order_id, screened_by_id, verified_two_patient_identifiers, verified_correct_exam_and_site, screening_clearance_status, radiologist_override_notes) "
            "SELECT %d, COALESCE((SELECT user_id FROM staff_users WHERE is_active=1 LIMIT 1), 1), 1, 1, 'CLEARED', 'Auto-cleared on DICOM reconciliation' "
            "WHERE NOT EXISTS (SELECT 1 FROM exam_safety_screenings WHERE order_id=%d);",
            orderId, orderId);
        sqlite3_exec(g_db, sql, 0, 0, 0);

        snprintf(sql, sizeof(sql),
            "UPDATE imaging_orders SET workflow_status='READY_FOR_READ', mpps_status='COMPLETED', "
            "arrived_at=COALESCE(arrived_at, datetime('now')), prep_started_at=COALESCE(prep_started_at, datetime('now')), "
            "scan_started_at=COALESCE(scan_started_at, datetime('now')), scan_completed_at=COALESCE(scan_completed_at, datetime('now')), "
            "qa_completed_at=COALESCE(qa_completed_at, datetime('now')) "
            "WHERE order_id=%d AND workflow_status IN ('ORDERED','SCHEDULED','IN_TRANSIT','ARRIVED_WAITING','IN_PREP_SCREENING','READY_FOR_SCAN','IN_PROGRESS','IMAGES_CAPTURED_QA');",
            orderId);
        sqlite3_exec(g_db, sql, 0, 0, 0);

        snprintf(sql, sizeof(sql),
            "UPDATE appointment_slots SET slot_status='COMPLETED' WHERE order_id=%d AND slot_status IN ('BOOKED','RESERVED_STAT');",
            orderId);
        sqlite3_exec(g_db, sql, 0, 0, 0);

        SyncWorklistFromDB();
    }

    char okResp[128];
    int rlen = snprintf(okResp, sizeof(okResp), "{\"status\":\"LINKED\",\"count\":%d}", linkedCount);
    SendHttpResponse(sock, "application/json", okResp, rlen);
}
static int ExecDeleteList(const char *const *stmts, char *errOut, size_t errSize) {
    if (!g_db) { snprintf(errOut, errSize, "Database not open."); return 0; }

    int skipped = 0;
    sqlite3_exec(g_db, "PRAGMA foreign_keys = OFF;", 0, 0, 0);

    for (int i = 0; stmts[i] != NULL; i++) {
        char *errMsg = NULL;
        if (sqlite3_exec(g_db, stmts[i], 0, 0, &errMsg) != SQLITE_OK) {
            skipped++;
            if (errMsg) {
                char logBuf[512];
                snprintf(logBuf, sizeof(logBuf), "DELETE skipped (%s): %s", errMsg, stmts[i]);
                write_log(logBuf);
                sqlite3_free(errMsg);
            }
        }
    }

    sqlite3_exec(g_db, "PRAGMA foreign_keys = ON;", 0, 0, 0);
    ResetCStoreTrackingFile();
    SyncWorklistFromDB();

    if (skipped > 0) {
        snprintf(errOut, errSize, "Done (%d statement(s) skipped; see log).", skipped);
    } else {
        errOut[0] = '\0';
    }
    return 1;
}

static int DeletePatientDataFromDB(char *errOut, size_t errSize) {
    static const char *const stmts[] = {
        "DELETE FROM report_discrete_findings;",
        "DELETE FROM clinical_followup_recalls;",
        "DELETE FROM critical_findings_escalations;",
        "DELETE FROM correspondence_dispatch_queue;",
        "DELETE FROM billing_remittance_transactions;",
        "DELETE FROM front_desk_pos_collections;",
        "DELETE FROM billing_claim_lines;",
        "DELETE FROM billing_claim_headers;",
        "DELETE FROM order_billing_accident_grant_info;",
        "DELETE FROM order_diagnoses;",
        "DELETE FROM order_dicom_study_map;",
        "DELETE FROM dicom_instances;",
        "DELETE FROM dicom_series;",
        "DELETE FROM dicom_studies;",
        "DELETE FROM contrast_and_medication_logs;",
        "DELETE FROM exam_safety_screenings;",
        "DELETE FROM patient_consents;",
        "DELETE FROM procedure_vital_signs;",
        "DELETE FROM probe_disinfection_logs;",
        "DELETE FROM radiopharmaceutical_hotlab_logs;",
        "DELETE FROM rejected_image_logs;",
        "DELETE FROM pathology_specimen_chain;",
        "DELETE FROM tumor_board_and_teaching_cases;",
        "DELETE FROM workflow_state_history;",
        "DELETE FROM worklist_locks;",
        "DELETE FROM radiology_reports;",
        "INSERT INTO fts_radiology_reports(fts_radiology_reports) VALUES('delete-all');",
        "DELETE FROM appointment_slots WHERE order_id IS NOT NULL;",
        "DELETE FROM imaging_orders;",
        "DELETE FROM patient_allergies;",
        "DELETE FROM patient_insurances;",
        "DELETE FROM patient_merge_history;",
        "DELETE FROM hipaa_phi_access_logs;",
        "DELETE FROM hl7_inbound_staging;",
        "DELETE FROM patients;",
        "DELETE FROM sqlite_sequence WHERE name IN ("
        "'patients','imaging_orders','radiology_reports','dicom_studies','dicom_series','dicom_instances',"
        "'billing_claim_headers','billing_claim_lines','billing_remittance_transactions','front_desk_pos_collections',"
        "'correspondence_dispatch_queue','critical_findings_escalations','clinical_followup_recalls',"
        "'contrast_and_medication_logs','exam_safety_screenings','patient_allergies','patient_consents',"
        "'patient_insurances','patient_merge_history','procedure_vital_signs','probe_disinfection_logs',"
        "'radiopharmaceutical_hotlab_logs','rejected_image_logs','pathology_specimen_chain',"
        "'tumor_board_and_teaching_cases','workflow_state_history','hipaa_phi_access_logs','hl7_inbound_staging');",
        NULL
    };

    int ok = ExecDeleteList(stmts, errOut, errSize);
    if (ok) write_log("ADMIN: Deleted all patient data and linked clinical/DICOM records.");
    return ok;
}

static int DeleteAllDataFromDB(char *errOut, size_t errSize) {
    static const char *const stmts[] = {
        "DELETE FROM report_discrete_findings;",
        "DELETE FROM clinical_followup_recalls;",
        "DELETE FROM critical_findings_escalations;",
        "DELETE FROM correspondence_dispatch_queue;",
        "DELETE FROM billing_remittance_transactions;",
        "DELETE FROM front_desk_pos_collections;",
        "DELETE FROM billing_claim_lines;",
        "DELETE FROM billing_claim_headers;",
        "DELETE FROM order_billing_accident_grant_info;",
        "DELETE FROM order_diagnoses;",
        "DELETE FROM order_dicom_study_map;",
        "DELETE FROM dicom_instances;",
        "DELETE FROM dicom_series;",
        "DELETE FROM dicom_studies;",
        "DELETE FROM contrast_and_medication_logs;",
        "DELETE FROM exam_safety_screenings;",
        "DELETE FROM patient_consents;",
        "DELETE FROM procedure_vital_signs;",
        "DELETE FROM probe_disinfection_logs;",
        "DELETE FROM radiopharmaceutical_hotlab_logs;",
        "DELETE FROM rejected_image_logs;",
        "DELETE FROM pathology_specimen_chain;",
        "DELETE FROM tumor_board_and_teaching_cases;",
        "DELETE FROM workflow_state_history;",
        "DELETE FROM worklist_locks;",
        "DELETE FROM radiology_reports;",
        "INSERT INTO fts_radiology_reports(fts_radiology_reports) VALUES('delete-all');",
        "DELETE FROM appointment_slots;",
        "DELETE FROM imaging_orders;",
        "DELETE FROM patient_allergies;",
        "DELETE FROM patient_insurances;",
        "DELETE FROM patient_merge_history;",
        "DELETE FROM hipaa_phi_access_logs;",
        "DELETE FROM hl7_inbound_staging;",
        "DELETE FROM patients;",
        "DELETE FROM equipment_qa_logs;",
        "DELETE FROM staff_shift_schedules;",
        "DELETE FROM staff_schedule_profiles;",
        "DELETE FROM hanging_protocols;",
        "DELETE FROM radiologist_voice_macros;",
        "DELETE FROM radreport_cde_definitions;",
        "DELETE FROM report_templates;",
        "DELETE FROM rsna_reporting_templates;",
        "DELETE FROM scanner_protocols;",
        "DELETE FROM billing_bundling_rules;",
        "DELETE FROM fee_schedule_rates;",
        "DELETE FROM billing_payers;",
        "DELETE FROM exam_catalog;",
        "DELETE FROM icd10_diagnoses;",
        "DELETE FROM modalities_and_rooms;",
        "DELETE FROM staff_users;",
        "DELETE FROM roles;",
        "DELETE FROM facilities;",
        "DELETE FROM app_settings;",
        "DELETE FROM sqlite_sequence;",
        NULL
    };

    int ok = ExecDeleteList(stmts, errOut, errSize);
    if (ok) write_log("ADMIN: Deleted ALL data from ris.db.");
    return ok;
}
static void ParseDicomNameParts(const char *dicomName, char *outFirst, size_t firstSz, char *outLast, size_t lastSz) {
    char tmp[128] = {0};
    strncpy(tmp, dicomName && dicomName[0] ? dicomName : "UNKNOWN^PATIENT", sizeof(tmp) - 1);
    TrimInPlace(tmp);
    char *caret = strchr(tmp, '^');
    if (caret) {
        *caret = '\0';
        char *rest = caret + 1;
        char *c2 = strchr(rest, '^');
        if (c2) *c2 = '\0';
        strncpy(outLast, tmp[0] ? tmp : "Unknown", lastSz - 1);
        strncpy(outFirst, rest[0] ? rest : "Unknown", firstSz - 1);
    } else {
        char *sp = strrchr(tmp, ' ');
        if (sp) {
            *sp = '\0';
            strncpy(outFirst, tmp[0] ? tmp : "Unknown", firstSz - 1);
            strncpy(outLast, (sp + 1)[0] ? (sp + 1) : "Unknown", lastSz - 1);
        } else {
            strncpy(outLast, tmp, lastSz - 1);
            strncpy(outFirst, "Unknown", firstSz - 1);
        }
    }
    outFirst[firstSz - 1] = '\0';
    outLast[lastSz - 1] = '\0';
}

static void SyncCStoreToDB(void) {
    if (!g_db) return;

    EnterCriticalSection(&g_cstoreLock);
    FILE *fp = fopen("c-store.csv", "rb");
    if (!fp) {
        LeaveCriticalSection(&g_cstoreLock);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long curSize = ftell(fp);
    if (curSize == g_lastCStoreFileSize) {
        fclose(fp);
        LeaveCriticalSection(&g_cstoreLock);
        return;
    }
    g_lastCStoreFileSize = curSize;
    fseek(fp, 0, SEEK_SET);

    char line[2048];
    int lineNum = 0;
    int anyOrderUpdated = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineNum++;
        if (lineNum == 1 && StrIStr(line, "Accession") && StrIStr(line, "SOPInstanceUID")) continue;

        char acc[64] = "", pName[128] = "", pId[64] = "", studyUid[128] = "", sopUid[128] = "";
        char folder[256] = "", fname[256] = "", mod[32] = "", fsize[32] = "", ts[64] = "";

        const char *p = line;
        ParseCsvField(&p, acc,      sizeof(acc));
        ParseCsvField(&p, pName,    sizeof(pName));
        ParseCsvField(&p, pId,      sizeof(pId));
        ParseCsvField(&p, studyUid, sizeof(studyUid));
        ParseCsvField(&p, sopUid,   sizeof(sopUid));
        ParseCsvField(&p, folder,   sizeof(folder));
        ParseCsvField(&p, fname,    sizeof(fname));
        ParseCsvField(&p, mod,      sizeof(mod));
        ParseCsvField(&p, fsize,    sizeof(fsize));
        ParseCsvField(&p, ts,       sizeof(ts));

        TrimInPlace(acc); TrimInPlace(pName); TrimInPlace(pId);
        TrimInPlace(studyUid); TrimInPlace(sopUid);
        TrimInPlace(folder); TrimInPlace(fname); TrimInPlace(mod);

        if (!sopUid[0] && fname[0]) {
            strncpy(sopUid, fname, sizeof(sopUid) - 1);
            sopUid[sizeof(sopUid) - 1] = '\0';
        }
        if (!sopUid[0]) continue;

        int exists = 0;
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(g_db, "SELECT 1 FROM dicom_instances WHERE sop_instance_uid=? LIMIT 1", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, sopUid, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) exists = 1;
            sqlite3_finalize(st);
        }
        if (exists) continue;

        char fullPath[MAX_PATH];
        if (folder[0]) snprintf(fullPath, sizeof(fullPath), "%s\\%s", folder, fname);
        else { strncpy(fullPath, fname, sizeof(fullPath) - 1); fullPath[sizeof(fullPath) - 1] = '\0'; }

        int orderId = 0;
        if (acc[0] && sqlite3_prepare_v2(g_db, "SELECT order_id FROM imaging_orders WHERE TRIM(accession_number)=? LIMIT 1", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, acc, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) orderId = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }

        /* Unmatched image -> park in Unlinked DICOM Reconciliation Queue */
        if (orderId <= 0) {
            const char *qsql =
                "INSERT OR IGNORE INTO dicom_unlinked_queue "
                "(sop_instance_uid, study_instance_uid, raw_accession, raw_patient_id, raw_patient_name, modality, file_path) "
                "VALUES (?, ?, ?, ?, ?, ?, ?);";
            if (sqlite3_prepare_v2(g_db, qsql, -1, &st, 0) == SQLITE_OK) {
                sqlite3_bind_text(st, 1, sopUid,   -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 2, studyUid, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 3, acc,      -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 4, pId,      -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 5, pName,    -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 6, mod[0] ? mod : "OT", -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 7, fullPath, -1, SQLITE_TRANSIENT);
                if (sqlite3_step(st) != SQLITE_DONE) {
                    char lb[256];
                    snprintf(lb, sizeof(lb), "C-STORE unlinked insert error: %s", sqlite3_errmsg(g_db));
                    write_log(lb);
                } else {
                    char lb[256];
                    snprintf(lb, sizeof(lb), "C-STORE queued UNLINKED image: acc='%s' sop='%s'", acc, sopUid);
                    write_log(lb);
                }
                sqlite3_finalize(st);
            } else {
                char lb[256];
                snprintf(lb, sizeof(lb), "C-STORE unlinked prepare error: %s", sqlite3_errmsg(g_db));
                write_log(lb);
            }
            continue;
        }

        const char *effStudyUid = studyUid[0] ? studyUid : sopUid;
        char wadoUrl[256], viewerUrl[128];
        snprintf(wadoUrl,   sizeof(wadoUrl),   "/download?sop=%s", sopUid);
        snprintf(viewerUrl, sizeof(viewerUrl), "embedded:%s", acc);

        if (sqlite3_prepare_v2(g_db,
                "INSERT OR IGNORE INTO dicom_studies "
                "(order_id, study_instance_uid, pacs_wado_url, pacs_zero_footprint_viewer_url, total_series_count, total_instance_count) "
                "VALUES (?, ?, ?, ?, 1, 0);", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_int(st,  1, orderId);
            sqlite3_bind_text(st, 2, effStudyUid, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, wadoUrl,     -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, viewerUrl,   -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }

        int studyId = 0;
        if (sqlite3_prepare_v2(g_db, "SELECT study_id FROM dicom_studies WHERE order_id=? LIMIT 1", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, orderId);
            if (sqlite3_step(st) == SQLITE_ROW) studyId = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
        if (studyId <= 0) continue;

        char seriesUid[160];
        snprintf(seriesUid, sizeof(seriesUid), "%s.1", effStudyUid);
        if (sqlite3_prepare_v2(g_db,
                "INSERT OR IGNORE INTO dicom_series (study_id, series_instance_uid, series_number, modality, instance_count) "
                "VALUES (?, ?, 1, ?, 0);", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_int(st,  1, studyId);
            sqlite3_bind_text(st, 2, seriesUid, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, mod[0] ? mod : "OT", -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }

        int seriesId = 0;
        if (sqlite3_prepare_v2(g_db, "SELECT series_id FROM dicom_series WHERE study_id=? LIMIT 1", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, studyId);
            if (sqlite3_step(st) == SQLITE_ROW) seriesId = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
        if (seriesId <= 0) continue;

        if (sqlite3_prepare_v2(g_db,
                "INSERT OR IGNORE INTO dicom_instances (series_id, study_id, sop_instance_uid, modality, file_path) VALUES (?, ?, ?, ?, ?);",
                -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_int(st,  1, seriesId);
            sqlite3_bind_int(st,  2, studyId);
            sqlite3_bind_text(st, 3, sopUid, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, mod[0] ? mod : "OT", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 5, fullPath, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) != SQLITE_DONE) {
                char lb[256];
                snprintf(lb, sizeof(lb), "C-STORE instance insert error: %s", sqlite3_errmsg(g_db));
                write_log(lb);
            }
            sqlite3_finalize(st);
        }

        char upd[1024];
        snprintf(upd, sizeof(upd),
            "UPDATE dicom_series SET instance_count=(SELECT COUNT(*) FROM dicom_instances WHERE series_id=%d) WHERE series_id=%d;",
            seriesId, seriesId);
        sqlite3_exec(g_db, upd, 0, 0, 0);

        snprintf(upd, sizeof(upd),
            "UPDATE dicom_studies SET "
            "total_series_count=(SELECT COUNT(*) FROM dicom_series WHERE study_id=%d), "
            "total_instance_count=(SELECT COALESCE(SUM(instance_count),0) FROM dicom_series WHERE study_id=%d) "
            "WHERE study_id=%d;", studyId, studyId, studyId);
        sqlite3_exec(g_db, upd, 0, 0, 0);

        snprintf(upd, sizeof(upd),
            "INSERT OR IGNORE INTO exam_safety_screenings "
            "(order_id, screened_by_id, verified_two_patient_identifiers, verified_correct_exam_and_site, screening_clearance_status, radiologist_override_notes) "
            "SELECT %d, COALESCE((SELECT user_id FROM staff_users WHERE is_active=1 LIMIT 1), 1), 1, 1, 'CLEARED', 'Auto-cleared on PACS C-STORE image arrival' "
            "WHERE NOT EXISTS (SELECT 1 FROM exam_safety_screenings WHERE order_id=%d);",
            orderId, orderId);
        sqlite3_exec(g_db, upd, 0, 0, 0);

        snprintf(upd, sizeof(upd),
            "UPDATE imaging_orders SET "
            "workflow_status='READY_FOR_READ', "
            "mpps_status='COMPLETED', "
            "arrived_at=COALESCE(arrived_at, datetime('now')), "
            "prep_started_at=COALESCE(prep_started_at, datetime('now')), "
            "scan_started_at=COALESCE(scan_started_at, datetime('now')), "
            "scan_completed_at=COALESCE(scan_completed_at, datetime('now')), "
            "qa_completed_at=COALESCE(qa_completed_at, datetime('now')) "
            "WHERE order_id=%d AND workflow_status IN ('ORDERED','SCHEDULED','IN_TRANSIT','ARRIVED_WAITING','IN_PREP_SCREENING','READY_FOR_SCAN','IN_PROGRESS','IMAGES_CAPTURED_QA');",
            orderId);
        sqlite3_exec(g_db, upd, 0, 0, 0);

        snprintf(upd, sizeof(upd),
            "UPDATE appointment_slots SET slot_status='COMPLETED' WHERE order_id=%d AND slot_status IN ('BOOKED','RESERVED_STAT');",
            orderId);
        sqlite3_exec(g_db, upd, 0, 0, 0);

        anyOrderUpdated = 1;
    }

    fclose(fp);
    LeaveCriticalSection(&g_cstoreLock);

    if (anyOrderUpdated) {
        SyncWorklistFromDB();
    }
}
void InitDB(void) {
    int rc = sqlite3_open("ris.db", &g_db);
    if (rc != SQLITE_OK) {
        printf("Cannot open database: %s\n", sqlite3_errmsg(g_db));
        return;
    }
    sqlite3_busy_timeout(g_db, 5000);

    sqlite3_stmt *stmt;
    int needsInit = 1;
    if (sqlite3_prepare_v2(g_db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='facilities'", -1, &stmt, 0) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) needsInit = 0;
        sqlite3_finalize(stmt);
    }

    if (needsInit) {
        printf("Database empty. Initializing from ris.sql...\n");
        FILE *f = fopen("ris.sql", "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *schema = (char*)malloc((size_t)fsize + 1);
            if (schema) {
                fread(schema, 1, (size_t)fsize, f);
                schema[fsize] = 0;

                char *errMsg = NULL;
                if (sqlite3_exec(g_db, schema, 0, 0, &errMsg) != SQLITE_OK) {
                    printf("SQL Error loading schema: %s\n", errMsg);
                    sqlite3_free(errMsg);
                } else {
                    printf("Schema successfully applied.\n");
                }
                free(schema);
            }
            fclose(f);
        } else {
            printf("WARNING: ris.sql not found. Cannot initialize schema.\n");
        }
    }

    sqlite3_exec(g_db,
        "CREATE TABLE IF NOT EXISTS dicom_instances ("
        "instance_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "series_id INTEGER NOT NULL REFERENCES dicom_series(series_id) ON DELETE CASCADE, "
        "study_id INTEGER NOT NULL REFERENCES dicom_studies(study_id) ON DELETE CASCADE, "
        "sop_instance_uid TEXT NOT NULL UNIQUE, "
        "instance_number INTEGER NOT NULL DEFAULT 1, "
        "modality TEXT NOT NULL, "
        "file_path TEXT NOT NULL, "
        "file_size_bytes INTEGER DEFAULT 0, "
        "rows INTEGER DEFAULT 256, columns INTEGER DEFAULT 256, bits_allocated INTEGER DEFAULT 8, "
        "uploaded_at TEXT DEFAULT (datetime('now')));"
        "CREATE INDEX IF NOT EXISTS idx_dicom_instances_study ON dicom_instances (study_id, series_id, instance_number);"
        "CREATE TABLE IF NOT EXISTS dicom_unlinked_queue ("
        "queue_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "sop_instance_uid TEXT UNIQUE NOT NULL, "
        "study_instance_uid TEXT, "
        "raw_accession TEXT, "
        "raw_patient_id TEXT, "
        "raw_patient_name TEXT, "
        "modality TEXT, "
        "file_path TEXT NOT NULL, "
        "status TEXT DEFAULT 'UNLINKED', "
        "received_at TEXT DEFAULT (datetime('now')));"
        "CREATE TABLE IF NOT EXISTS rsna_reporting_templates ("
        "template_row_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "modality TEXT NOT NULL, anatomy TEXT NOT NULL, pathology TEXT NOT NULL, "
        "v1_label TEXT DEFAULT '', v1_key TEXT DEFAULT '', v1_values TEXT DEFAULT '', "
        "v2_label TEXT DEFAULT '', v2_key TEXT DEFAULT '', v2_values TEXT DEFAULT '', "
        "v3_label TEXT DEFAULT '', v3_key TEXT DEFAULT '', v3_values TEXT DEFAULT '', "
        "v4_label TEXT DEFAULT '', v4_key TEXT DEFAULT '', v4_values TEXT DEFAULT '', "
        "base_template TEXT NOT NULL, findings_template TEXT NOT NULL, impression_template TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS report_typst_documents ("
        "typst_doc_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "report_id INTEGER NOT NULL UNIQUE REFERENCES radiology_reports(report_id) ON DELETE CASCADE, "
        "order_id INTEGER NOT NULL REFERENCES imaging_orders(order_id) ON DELETE CASCADE, "
        "typst_source TEXT NOT NULL, "
        "template_name TEXT NOT NULL DEFAULT 'STANDARD_RADIOLOGY_REPORT', "
        "generated_at TEXT NOT NULL DEFAULT (datetime('now')), "
        "updated_at TEXT NOT NULL DEFAULT (datetime('now')));"
        "CREATE INDEX IF NOT EXISTS idx_report_typst_order ON report_typst_documents (order_id);"
        "CREATE TABLE IF NOT EXISTS typst_report_templates ("
        "typst_template_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "template_code TEXT UNIQUE NOT NULL, "
        "template_name TEXT NOT NULL, "
        "modality_type TEXT NOT NULL DEFAULT 'ALL', "
        "is_default INTEGER NOT NULL DEFAULT 0, "
        "typst_markup TEXT NOT NULL, "
        "updated_at TEXT NOT NULL DEFAULT (datetime('now')));"
        "CREATE TABLE IF NOT EXISTS user_auth_credentials ("
        "user_id INTEGER PRIMARY KEY REFERENCES staff_users(user_id) ON DELETE CASCADE, "
        "password_salt TEXT NOT NULL, "
        "password_hash TEXT NOT NULL, "
        "must_change_password INTEGER NOT NULL DEFAULT 0, "
        "last_login_at TEXT, "
        "updated_at TEXT NOT NULL DEFAULT (datetime('now')));"
        "CREATE TABLE IF NOT EXISTS user_sessions ("
        "session_token TEXT PRIMARY KEY, "
        "user_id INTEGER NOT NULL REFERENCES staff_users(user_id) ON DELETE CASCADE, "
        "created_at TEXT NOT NULL DEFAULT (datetime('now')), "
        "expires_at TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS password_reset_tokens ("
        "token_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "user_id INTEGER NOT NULL REFERENCES staff_users(user_id) ON DELETE CASCADE, "
        "reset_token TEXT UNIQUE NOT NULL, "
        "expires_at TEXT NOT NULL, "
        "used_at TEXT, "
        "created_at TEXT NOT NULL DEFAULT (datetime('now')));"
        "INSERT OR IGNORE INTO typst_report_templates (template_code, template_name, modality_type, is_default, typst_markup) VALUES ("
        "'STANDARD_RADIOLOGY_REPORT', 'Standard Radiology Report (US Letter)', 'ALL', 1, "
        "'#set page(paper: \"us-letter\", margin: (x: 0.75in, y: 0.85in),\n"
        "  header: [#text(size: 9pt, fill: rgb(74, 85, 104))[{facility_name} - {patient_name} - Accession {accession}]],\n"
        "  footer: [#text(size: 8pt, fill: rgb(113, 128, 150))[CONFIDENTIAL MEDICAL RECORD - {facility_name} - Page #counter(page).display()]]\n"
        ")\n#set text(font: \"Helvetica\", size: 10pt)\n\n"
        "#text(size: 14pt, weight: \"bold\")[{facility_name}]\n"
        "#text(size: 9pt, fill: rgb(74, 85, 104))[{facility_address} | Tel: {facility_phone}]\n"
        "#line(length: 100%)\n\n"
        "*Patient:* {patient_name}   |   *Accession:* {accession}\n"
        "*MRN:* {mrn}   |   *DOB:* {dob} ({sex})\n"
        "*Exam:* {exam_name} ({modality})\n"
        "*Referring MD:* Dr. {referring_md}   |   *Status:* {report_status} (v{version}) - {signed_at}\n"
        "#line(length: 100%)\n\n"
        "== CLINICAL HISTORY\n{clinical_history}\n\n"
        "== COMPARISON\n{comparison}\n\n"
        "== TECHNIQUE\n{technique}\n\n"
        "== FINDINGS\n{findings}\n\n"
        "== IMPRESSION\n*{impression}*\n\n"
        "{standardized_assessment}\n"
        "#line(length: 100%)\n"
        "*Electronically Signed By:* Dr. {radiologist} (License/NPI: {radiologist_license}) on {signed_at}\n');",
        0, 0, 0);
}
/* --- HTTP DICOM & API Handlers --- */
void HandleDicomUpload(SOCKET sock, const char *pBuf, int totalLen, int isReplace) {
    char acc[128] = "", sop[256] = "";
    if (isReplace) GetQueryParam(pBuf, "sop", sop, sizeof(sop));
    else           GetQueryParam(pBuf, "acc", acc, sizeof(acc));

    const char *hdrEnd = strstr(pBuf, "\r\n\r\n");
    if (!hdrEnd) {
        char err[] = "{\"error\":\"Invalid upload request\"}";
        SendHttpResponse(sock, "application/json", err, (int)strlen(err));
        return;
    }
    const char *body = hdrEnd + 4;
    int bodyLen = totalLen - (int)(body - pBuf);
    int doRewrite = MemContains(body, bodyLen, "name=\"rewrite\"\r\n\r\n1");

    const char *fileStart = NULL;
    int fileLen = 0;
    const char *partEnd = strstr(body, "\r\n\r\n");
    if (partEnd && (partEnd - body) < bodyLen) {
        fileStart = partEnd + 4;
        const char *bEnd = strstr(body, "\r\n");
        int bLen = bEnd ? (int)(bEnd - body) : 0;
        fileLen = bodyLen - (int)(fileStart - body);
        if (bLen > 2 && bLen < 128) {
            for (int i = 0; i <= fileLen - bLen; i++) {
                if (memcmp(fileStart + i, body, bLen) == 0) {
                    fileLen = i;
                    if (fileLen >= 2 && fileStart[fileLen - 2] == '\r' && fileStart[fileLen - 1] == '\n') fileLen -= 2;
                    break;
                }
            }
        }
    }

    if (!fileStart || fileLen <= 0) {
        char err[] = "{\"error\":\"No DICOM file payload found\"}";
        SendHttpResponse(sock, "application/json", err, (int)strlen(err));
        return;
    }

    char folder[32], filename[128], path[MAX_PATH];
    AllocateImageFile(folder, filename, path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        char err[] = "{\"error\":\"Failed to write DICOM file\"}";
        SendHttpResponse(sock, "application/json", err, (int)strlen(err));
        return;
    }
    fwrite(fileStart, 1, (size_t)fileLen, f);
    fclose(f);

    sqlite3_stmt *st = NULL;
    if (isReplace && sop[0] && !acc[0]) {
        const char *aSql =
            "SELECT o.accession_number FROM dicom_instances di "
            "JOIN dicom_studies st ON di.study_id = st.study_id "
            "JOIN imaging_orders o ON st.order_id = o.order_id "
            "WHERE di.sop_instance_uid = ? LIMIT 1";
        if (sqlite3_prepare_v2(g_db, aSql, -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, sop, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) {
                const char *a = (const char*)sqlite3_column_text(st, 0);
                if (a) strncpy(acc, a, sizeof(acc) - 1);
            }
            sqlite3_finalize(st);
        }
    }

    if (doRewrite && acc[0]) {
        const char *tagSql =
            "SELECT UPPER(p.last_name || '^' || p.first_name), p.mrn, REPLACE(substr(p.dob,1,10),'-',''), "
            "       p.biological_sex, COALESCE(ref.last_name || '^' || ref.first_name, ''), ec.exam_name, "
            "       COALESCE(o.preassigned_study_uid, '1.2.826.0.1.3680043.8.498.' || o.order_id) "
            "FROM imaging_orders o "
            "JOIN patients p ON o.patient_id = p.patient_id "
            "JOIN exam_catalog ec ON o.exam_code = ec.exam_code "
            "LEFT JOIN staff_users ref ON o.referring_md_id = ref.user_id "
            "WHERE o.accession_number = ? LIMIT 1";
        if (sqlite3_prepare_v2(g_db, tagSql, -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, acc, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) {
                const char *pn   = (const char*)sqlite3_column_text(st, 0);
                const char *mrn  = (const char*)sqlite3_column_text(st, 1);
                const char *dob  = (const char*)sqlite3_column_text(st, 2);
                const char *sex  = (const char*)sqlite3_column_text(st, 3);
                const char *refp = (const char*)sqlite3_column_text(st, 4);
                const char *desc = (const char*)sqlite3_column_text(st, 5);
                const char *suid = (const char*)sqlite3_column_text(st, 6);
                char rules[1024], tmpPath[MAX_PATH];
                snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
                snprintf(rules, sizeof(rules),
                    "0x0008,0x0050=%s`0x0010,0x0010=%s`0x0010,0x0020=%s`0x0010,0x0030=%s`0x0010,0x0040=%s`0x0008,0x0090=%s`0x0008,0x1030=%s`0x0020,0x000D=%s",
                    acc, pn ? pn : "", mrn ? mrn : "", dob ? dob : "", sex ? sex : "",
                    refp ? refp : "", desc ? desc : "", suid ? suid : "");
                if (apply_dicom_edits_file(path, tmpPath, rules, "`")) {
                    remove(path);
                    rename(tmpPath, path);
                } else {
                    remove(tmpPath);
                }
            }
            sqlite3_finalize(st);
        }
    }

    if (isReplace && sop[0]) {
        if (sqlite3_prepare_v2(g_db, "UPDATE dicom_instances SET file_path=?, file_size_bytes=?, uploaded_at=datetime('now') WHERE sop_instance_uid=?", -1, &st, 0) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, path, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 2, fileLen);
            sqlite3_bind_text(st, 3, sop, -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        char ok[] = "{\"status\":\"REPLACED\"}";
        SendHttpResponse(sock, "application/json", ok, (int)strlen(ok));
        return;
    }

    int orderId = 0, studyId = 0, seriesId = 0, nextInst = 1;
    char mod[32] = "OT";
    if (sqlite3_prepare_v2(g_db, "SELECT o.order_id, ec.modality_type FROM imaging_orders o JOIN exam_catalog ec ON o.exam_code=ec.exam_code WHERE o.accession_number=?", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, acc, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            orderId = sqlite3_column_int(st, 0);
            const char *m = (const char*)sqlite3_column_text(st, 1);
            if (m) strncpy(mod, m, sizeof(mod) - 1);
        }
        sqlite3_finalize(st);
    }
    if (!orderId) {
        remove(path);
        char err[] = "{\"error\":\"Accession not found\"}";
        SendHttpResponse(sock, "application/json", err, (int)strlen(err));
        return;
    }

    char newSop[128];
    snprintf(newSop, sizeof(newSop), "1.2.826.0.1.3680043.8.498.%d.%lu", orderId, (unsigned long)GetTickCount());

    if (sqlite3_prepare_v2(g_db, "SELECT study_id FROM dicom_studies WHERE order_id=?", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, orderId);
        if (sqlite3_step(st) == SQLITE_ROW) studyId = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    if (!studyId) {
        char sqlIns[512];
        snprintf(sqlIns, sizeof(sqlIns),
            "INSERT INTO dicom_studies (order_id, study_instance_uid, pacs_wado_url, pacs_zero_footprint_viewer_url, total_series_count, total_instance_count) "
            "VALUES (%d, '1.2.826.0.1.3680043.8.498.%d', '/download?sop=%s', 'embedded:%s', 1, 0)",
            orderId, orderId, newSop, acc);
        sqlite3_exec(g_db, sqlIns, 0, 0, 0);
        studyId = (int)sqlite3_last_insert_rowid(g_db);
    }

    if (sqlite3_prepare_v2(g_db, "SELECT series_id FROM dicom_series WHERE study_id=? ORDER BY series_number ASC LIMIT 1", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, studyId);
        if (sqlite3_step(st) == SQLITE_ROW) seriesId = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    if (!seriesId) {
        char sqlSer[512];
        snprintf(sqlSer, sizeof(sqlSer),
            "INSERT INTO dicom_series (study_id, series_instance_uid, series_number, modality, series_description, instance_count) "
            "VALUES (%d, '1.2.826.0.1.3680043.8.498.%d.1', 1, '%s', 'Uploaded Series', 0)", studyId, orderId, mod);
        sqlite3_exec(g_db, sqlSer, 0, 0, 0);
        seriesId = (int)sqlite3_last_insert_rowid(g_db);
    }

    if (sqlite3_prepare_v2(g_db, "SELECT COALESCE(MAX(instance_number),0)+1 FROM dicom_instances WHERE study_id=?", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, studyId);
        if (sqlite3_step(st) == SQLITE_ROW) nextInst = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }

    if (sqlite3_prepare_v2(g_db, "INSERT INTO dicom_instances (series_id, study_id, sop_instance_uid, instance_number, modality, file_path, file_size_bytes) VALUES (?,?,?,?,?,?,?)", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, seriesId);
        sqlite3_bind_int(st, 2, studyId);
        sqlite3_bind_text(st, 3, newSop, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 4, nextInst);
        sqlite3_bind_text(st, 5, mod, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 6, path, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 7, fileLen);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    char upd[256];
    snprintf(upd, sizeof(upd), "UPDATE dicom_series SET instance_count=(SELECT COUNT(*) FROM dicom_instances WHERE series_id=%d) WHERE series_id=%d", seriesId, seriesId);
    sqlite3_exec(g_db, upd, 0, 0, 0);
    snprintf(upd, sizeof(upd), "UPDATE dicom_studies SET total_instance_count=(SELECT COUNT(*) FROM dicom_instances WHERE study_id=%d) WHERE study_id=%d", studyId, studyId);
    sqlite3_exec(g_db, upd, 0, 0, 0);

    char resp[256];
    int rlen = snprintf(resp, sizeof(resp), "{\"status\":\"UPLOADED\",\"sop\":\"%s\"}", newSop);
    SendHttpResponse(sock, "application/json", resp, rlen);
}

void HandleDownloadDicom(SOCKET sock, const char *req) {
    char sop[256];
    if (!GetQueryParam(req, "sop", sop, sizeof(sop)) || !sop[0]) {
        char err[] = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nMissing sop";
        SendBufferCompletely(sock, err, (int)strlen(err));
        return;
    }

    char filePath[512] = "", patName[128] = "UNKNOWN", mrn[64] = "", dob[32] = "";
    char desc[128] = "", mod[32] = "CT", kvp[32] = "", mas[32] = "", site[128] = "Metro Toronto Imaging";
    int instNum = 1;

    const char *sql =
        "SELECT di.file_path, UPPER(p.last_name || '^' || p.first_name), p.mrn, REPLACE(p.dob,'-',''), "
        "       COALESCE(ds.series_description, ec.exam_name), di.modality, "
        "       COALESCE(CAST(st.kvp AS TEXT),''), COALESCE(CAST(st.mas AS TEXT),''), "
        "       COALESCE(f.facility_name,'RIS'), di.instance_number "
        "FROM dicom_instances di "
        "JOIN dicom_series ds ON di.series_id = ds.series_id "
        "JOIN dicom_studies st ON di.study_id = st.study_id "
        "JOIN imaging_orders o ON st.order_id = o.order_id "
        "JOIN patients p ON o.patient_id = p.patient_id "
        "JOIN exam_catalog ec ON o.exam_code = ec.exam_code "
        "LEFT JOIN facilities f ON o.facility_id = f.facility_id "
        "WHERE di.sop_instance_uid = ? LIMIT 1";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, sop, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *s;
            if ((s = (const char*)sqlite3_column_text(stmt, 0))) strncpy(filePath, s, sizeof(filePath) - 1);
            if ((s = (const char*)sqlite3_column_text(stmt, 1))) strncpy(patName,  s, sizeof(patName)  - 1);
            if ((s = (const char*)sqlite3_column_text(stmt, 2))) strncpy(mrn,      s, sizeof(mrn)      - 1);
            if ((s = (const char*)sqlite3_column_text(stmt, 3))) strncpy(dob,      s, sizeof(dob)      - 1);
            if ((s = (const char*)sqlite3_column_text(stmt, 4))) strncpy(desc,     s, sizeof(desc)     - 1);
            if ((s = (const char*)sqlite3_column_text(stmt, 5))) strncpy(mod,      s, sizeof(mod)      - 1);
            if ((s = (const char*)sqlite3_column_text(stmt, 6))) strncpy(kvp,      s, sizeof(kvp)      - 1);
            if ((s = (const char*)sqlite3_column_text(stmt, 7))) strncpy(mas,      s, sizeof(mas)      - 1);
            if ((s = (const char*)sqlite3_column_text(stmt, 8))) strncpy(site,     s, sizeof(site)     - 1);
            instNum = sqlite3_column_int(stmt, 9);
        }
        sqlite3_finalize(stmt);
    }

    /* Fallback to unlinked staging queue for Preview button */
    if (!filePath[0]) {
        if (sqlite3_prepare_v2(g_db, "SELECT file_path FROM dicom_unlinked_queue WHERE sop_instance_uid = ? LIMIT 1", -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, sop, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *s = (const char*)sqlite3_column_text(stmt, 0);
                if (s) strncpy(filePath, s, sizeof(filePath) - 1);
            }
            sqlite3_finalize(stmt);
        }
    }

    if (filePath[0]) {
        FILE *f = fopen(filePath, "rb");
        if (f) {
            fclose(f);
            ServeFile(sock, filePath, "application/dicom");
            return;
        }
    }

    const int W = 256, H = 256;
    unsigned char *dcm = (unsigned char*)calloc(1, 4096 + W * H);
    if (!dcm) return;
    int off = 128;
    memcpy(dcm + off, "DICM", 4); off += 4;

    DcmWriteTagStr(dcm, &off, 0x0002, 0x0010, "UI", "1.2.840.10008.1.2.1");
    DcmWriteTagStr(dcm, &off, 0x0008, 0x0020, "DA", "20261120");
    DcmWriteTagStr(dcm, &off, 0x0008, 0x0030, "TM", "083000");
    DcmWriteTagStr(dcm, &off, 0x0008, 0x0060, "CS", mod);
    DcmWriteTagStr(dcm, &off, 0x0008, 0x0080, "LO", site);
    DcmWriteTagStr(dcm, &off, 0x0008, 0x103E, "LO", desc);
    DcmWriteTagStr(dcm, &off, 0x0010, 0x0010, "PN", patName);
    DcmWriteTagStr(dcm, &off, 0x0010, 0x0020, "LO", mrn);
    DcmWriteTagStr(dcm, &off, 0x0010, 0x0030, "DA", dob);
    if (kvp[0]) DcmWriteTagStr(dcm, &off, 0x0018, 0x0060, "DS", kvp);
    if (mas[0]) DcmWriteTagStr(dcm, &off, 0x0018, 0x1152, "IS", mas);
    DcmWriteTagUS(dcm, &off, 0x0028, 0x0002, 1);
    DcmWriteTagStr(dcm, &off, 0x0028, 0x0004, "CS", "MONOCHROME2");
    DcmWriteTagUS(dcm, &off, 0x0028, 0x0010, H);
    DcmWriteTagUS(dcm, &off, 0x0028, 0x0011, W);
    DcmWriteTagUS(dcm, &off, 0x0028, 0x0100, 8);
    DcmWriteTagUS(dcm, &off, 0x0028, 0x0103, 0);
    DcmWriteTagStr(dcm, &off, 0x0028, 0x1050, "DS", "128");
    DcmWriteTagStr(dcm, &off, 0x0028, 0x1051, "DS", "256");

    uint32_t pxLen = (uint32_t)(W * H);
    dcm[off++] = 0xE0; dcm[off++] = 0x7F; dcm[off++] = 0x10; dcm[off++] = 0x00;
    dcm[off++] = 'O';  dcm[off++] = 'B';  dcm[off++] = 0;    dcm[off++] = 0;
    memcpy(dcm + off, &pxLen, 4); off += 4;

    int cx = W / 2, cy = H / 2, shift = (instNum - 1) * 8;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int dx = x - cx, dy = y - cy;
            double body = (dx * dx) / (105.0 * 105.0) + (dy * dy) / (85.0 * 85.0);
            int val = 10;
            if (body <= 1.0) {
                val = 95;
                if (body > 0.86) val = 205;
                int lx = x - (cx - 38), rx = x - (cx + 38), ly = y - (cy - 5 + shift);
                if (lx * lx + ly * ly < 900 || rx * rx + ly * ly < 900) val = 35;
                int sx = x - cx, sy = y - (cy + 36);
                if (sx * sx + sy * sy < 220) val = 235;
                int fx = x - (cx + 22), fy = y - (cy - 12 + shift);
                if (fx * fx + fy * fy < 110) val = 190;
            }
            dcm[off + y * W + x] = (unsigned char)val;
        }
    }
    off += (int)pxLen;
    SendHttpResponse(sock, "application/dicom", (const char*)dcm, off);
    free(dcm);
}
void HandleApiImages(SOCKET sock, const char *req) {
    char acc[128];
    GetQueryParam(req, "acc", acc, sizeof(acc));

    const char *sql =
        "SELECT di.sop_instance_uid, di.modality, ds.series_number, di.instance_number, "
        "       COALESCE(ds.series_description, '') "
        "FROM dicom_instances di "
        "JOIN dicom_series ds ON di.series_id = ds.series_id "
        "JOIN dicom_studies st ON di.study_id = st.study_id "
        "JOIN imaging_orders o ON st.order_id = o.order_id "
        "WHERE o.accession_number = ? "
        "ORDER BY ds.series_number ASC, di.instance_number ASC";

    sqlite3_stmt *stmt = NULL;
    char *out = (char*)malloc(262144);
    if (!out) return;
    int len = 0;
    out[len++] = '[';
    int count = 0;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, acc, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW && len < 260000) {
            const char *sop  = (const char*)sqlite3_column_text(stmt, 0);
            const char *mod  = (const char*)sqlite3_column_text(stmt, 1);
            int serNum       = sqlite3_column_int(stmt, 2);
            int instNum      = sqlite3_column_int(stmt, 3);
            const char *desc = (const char*)sqlite3_column_text(stmt, 4);
            if (count++ > 0) out[len++] = ',';
            len += snprintf(out + len, 262144 - len,
                "{\"sop\":\"%s\",\"mod\":\"%s #%d\",\"series\":%d,\"instance\":%d,\"desc\":\"%s\"}",
                sop ? sop : "", mod ? mod : "OT", instNum, serNum, instNum, desc ? desc : "");
        }
        sqlite3_finalize(stmt);
    }
    out[len++] = ']';
    out[len] = '\0';
    SendHttpResponse(sock, "application/json", out, len);
    free(out);
}

void HandleDispatchReminder(SOCKET sock, const char *body) {
    int dispatchId = atoi(body);
    if (dispatchId <= 0) {
        char resp[] = "{\"error\":\"Invalid dispatch_id\"}";
        SendHttpResponse(sock, "application/json", resp, (int)strlen(resp));
        return;
    }
    char status[64] = "";
    DWORD exitCode = 0;
    int ok = DispatchSingleQueueRow(g_db, dispatchId, status, sizeof(status), &exitCode);
    char resp[256];
    int len = ok
        ? snprintf(resp, sizeof(resp), "{\"status\":\"%s\",\"exit_code\":%lu}", status, (unsigned long)exitCode)
        : snprintf(resp, sizeof(resp), "{\"error\":\"Dispatch failed (%s)\",\"status\":\"%s\"}", status, status);
    SendHttpResponse(sock, "application/json", resp, len);
}

void HandleApiQuery(SOCKET sock, const char* sql) {
    sqlite3_stmt *stmt;
    char *outJson = (char*)malloc(2 * 1024 * 1024);
    if (!outJson) return;

    int outLen = 0;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, 0) != SQLITE_OK) {
        outLen = snprintf(outJson, 1024, "{\"error\": \"%s\"}", sqlite3_errmsg(g_db));
        SendHttpResponse(sock, "application/json", outJson, outLen);
        free(outJson);
        return;
    }

    outJson[outLen++] = '[';
    int cols = sqlite3_column_count(stmt);
    int rowCount = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (rowCount > 0) outJson[outLen++] = ',';
        outJson[outLen++] = '{';
        for (int i = 0; i < cols; i++) {
            if (i > 0) outJson[outLen++] = ',';

            const char* colName = sqlite3_column_name(stmt, i);
            outJson[outLen++] = '"';
            while (*colName) outJson[outLen++] = *colName++;
            outJson[outLen++] = '"';
            outJson[outLen++] = ':';

            int type = sqlite3_column_type(stmt, i);
            if (type == SQLITE_NULL) {
                memcpy(outJson + outLen, "null", 4);
                outLen += 4;
            } else if (type == SQLITE_INTEGER || type == SQLITE_FLOAT) {
                const char* num = (const char*)sqlite3_column_text(stmt, i);
                while (num && *num) outJson[outLen++] = *num++;
            } else {
                outJson[outLen++] = '"';
                const char* txt = (const char*)sqlite3_column_text(stmt, i);
                if (txt) {
                    while (*txt) {
                        unsigned char uc = (unsigned char)*txt;
                        if (uc == '"' || uc == '\\') { outJson[outLen++] = '\\'; outJson[outLen++] = (char)uc; }
                        else if (uc == '\r') { outJson[outLen++] = '\\'; outJson[outLen++] = 'r'; }
                        else if (uc == '\n') { outJson[outLen++] = '\\'; outJson[outLen++] = 'n'; }
                        else if (uc == '\t') { outJson[outLen++] = '\\'; outJson[outLen++] = 't'; }
                        else if (uc < 0x20)  { outJson[outLen++] = ' '; }
                        else { outJson[outLen++] = (char)uc; }
                        txt++;
                    }
                }
                outJson[outLen++] = '"';
            }
        }
        outJson[outLen++] = '}';
        rowCount++;
    }
    outJson[outLen++] = ']';
    outJson[outLen] = '\0';
    sqlite3_finalize(stmt);

    if ((StrIStr(sql, "INSERT") || StrIStr(sql, "UPDATE") || StrIStr(sql, "DELETE")) &&
        (StrIStr(sql, "imaging_orders") || StrIStr(sql, "patients") || StrIStr(sql, "appointment_slots") ||
         StrIStr(sql, "modalities_and_rooms") || StrIStr(sql, "exam_catalog") || StrIStr(sql, "scanner_protocols"))) {
        SyncWorklistFromDB();
    }

    SendHttpResponse(sock, "application/json", outJson, outLen);
    free(outJson);
}

/* --- Networking Arrays & HTTP Client Handling --- */
void InitArrays(void) {
    memset(g_clients, 0, sizeof(g_clients));
    memset(g_clientBufLen, 0, sizeof(g_clientBufLen));
    memset(g_clientLastTick, 0, sizeof(g_clientLastTick));
    for (int i = 0; i < MAX_CLIENTS; i++) {
        g_clientBuffers[i] = NULL;
        g_clientBufCap[i] = 0;
    }
}

int FindFreeClientSlot(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] == 0) return i;
    }
    return -1;
}

void SetNonBlocking(SOCKET sock) {
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO_VAL, &mode);
}

void RemoveClient(int idx) {
    if (g_clients[idx] != 0) closesocket(g_clients[idx]);
    g_clients[idx] = 0;
    g_clientBufLen[idx] = 0;
}

void AddClient(SOCKET sock) {
    int idx = FindFreeClientSlot();
    if (idx == -1) { closesocket(sock); return; }
    SetNonBlocking(sock);
    g_clients[idx] = sock;
    g_clientBufLen[idx] = 0;
    if (g_clientBuffers[idx] == NULL) {
        g_clientBufCap[idx] = CLIENT_BUF_SIZE;
        g_clientBuffers[idx] = (char*)malloc(g_clientBufCap[idx]);
    }
    memset(g_clientBuffers[idx], 0, g_clientBufCap[idx]);
    g_clientLastTick[idx] = GetTickCount();
}

void CheckPendingAndTimeout(int clientIdx) {
    if (g_clients[clientIdx] == 0) return;
    if (GetTickCount() - g_clientLastTick[clientIdx] >= (DWORD)(g_TelnetTimeout * 1000)) {
        RemoveClient(clientIdx);
    }
}

static int ResetAdminPasswordInDB(char *msgOut, size_t msgSize) {
    if (!g_db) {
        snprintf(msgOut, msgSize, "Database not open.");
        return 0;
    }

    int userId = 0;
    char uname[64] = "admin";
    sqlite3_stmt *st = NULL;

    char salt[33], hash[65];
    GenerateRandomTokenHex(salt, 32);
    HashPasswordSalted(salt, "admin", hash);

    const char *findSql =
        "SELECT u.user_id, u.username "
        "FROM staff_users u "
        "LEFT JOIN roles r ON u.role_id = r.role_id "
        "WHERE LOWER(u.username) = 'admin' OR r.role_code = 'SYSTEM_ADMIN' "
        "ORDER BY CASE WHEN LOWER(u.username) = 'admin' THEN 1 ELSE 2 END, u.user_id ASC "
        "LIMIT 1";
    if (sqlite3_prepare_v2(g_db, findSql, -1, &st, 0) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            userId = sqlite3_column_int(st, 0);
            const char *u = (const char*)sqlite3_column_text(st, 1);
            if (u && u[0]) { strncpy(uname, u, sizeof(uname) - 1); uname[sizeof(uname) - 1] = '\0'; }
        }
        sqlite3_finalize(st);
    }

    if (userId <= 0) {
        int roleId = 0;
        if (sqlite3_prepare_v2(g_db,
                "SELECT role_id FROM roles ORDER BY CASE WHEN role_code='SYSTEM_ADMIN' THEN 1 ELSE 2 END, role_id ASC LIMIT 1",
                -1, &st, 0) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) roleId = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }

        if (roleId <= 0) {
            char rCols[1024] = "", rVals[2048] = "";
            int rColCnt = 0;
            if (sqlite3_prepare_v2(g_db, "PRAGMA table_info(roles);", -1, &st, 0) == SQLITE_OK) {
                while (sqlite3_step(st) == SQLITE_ROW) {
                    const char *cname = (const char*)sqlite3_column_text(st, 1);
                    const char *ctype = (const char*)sqlite3_column_text(st, 2);
                    int notNull = sqlite3_column_int(st, 3);
                    int hasDflt = (sqlite3_column_type(st, 4) != SQLITE_NULL);
                    int isPk    = sqlite3_column_int(st, 5);
                    if (!cname || isPk) continue;

                    char valBuf[160] = "";
                    int include = 1;
                    if      (strcmp(cname, "role_code") == 0)        snprintf(valBuf, sizeof(valBuf), "'SYSTEM_ADMIN'");
                    else if (strcmp(cname, "role_name") == 0)        snprintf(valBuf, sizeof(valBuf), "'System Administrator'");
                    else if (strcmp(cname, "role_description") == 0) snprintf(valBuf, sizeof(valBuf), "'System Administrator'");
                    else if (strncmp(cname, "can_", 4) == 0)         snprintf(valBuf, sizeof(valBuf), "1");
                    else if (notNull && !hasDflt) {
                        if (ctype && (StrIStr(ctype, "INT") || StrIStr(ctype, "NUM") || StrIStr(ctype, "REAL") || StrIStr(ctype, "BOOL"))) {
                            snprintf(valBuf, sizeof(valBuf), "1");
                        } else {
                            snprintf(valBuf, sizeof(valBuf), "'System Administrator'");
                        }
                    } else {
                        include = 0;
                    }

                    if (include) {
                        if (rColCnt > 0) {
                            strncat(rCols, ", ", sizeof(rCols) - strlen(rCols) - 1);
                            strncat(rVals, ", ", sizeof(rVals) - strlen(rVals) - 1);
                        }
                        strncat(rCols, cname,  sizeof(rCols) - strlen(rCols) - 1);
                        strncat(rVals, valBuf, sizeof(rVals) - strlen(rVals) - 1);
                        rColCnt++;
                    }
                }
                sqlite3_finalize(st);
            }

            char rSql[3584];
            snprintf(rSql, sizeof(rSql), "INSERT INTO roles (%s) VALUES (%s);", rCols, rVals);
            if (sqlite3_exec(g_db, rSql, 0, 0, 0) == SQLITE_OK) {
                roleId = (int)sqlite3_last_insert_rowid(g_db);
            } else {
                snprintf(msgOut, msgSize, "Failed to create role: %s", sqlite3_errmsg(g_db));
                write_log(msgOut);
                return 0;
            }
        }

        int facId = 1;
        if (sqlite3_prepare_v2(g_db, "SELECT facility_id FROM facilities ORDER BY facility_id ASC LIMIT 1", -1, &st, 0) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) facId = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }

        char cols[1024] = "", vals[2048] = "";
        int colCount = 0;
        if (sqlite3_prepare_v2(g_db, "PRAGMA table_info(staff_users);", -1, &st, 0) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                const char *cname = (const char*)sqlite3_column_text(st, 1);
                const char *ctype = (const char*)sqlite3_column_text(st, 2);
                int notNull = sqlite3_column_int(st, 3);
                int hasDflt = (sqlite3_column_type(st, 4) != SQLITE_NULL);
                int isPk    = sqlite3_column_int(st, 5);
                if (!cname || isPk) continue;

                char valBuf[160] = "";
                int include = 1;
                if      (strcmp(cname, "username") == 0)               snprintf(valBuf, sizeof(valBuf), "'admin'");
                else if (strcmp(cname, "role_id") == 0)                snprintf(valBuf, sizeof(valBuf), "%d", roleId);
                else if (strcmp(cname, "primary_facility_id") == 0)    snprintf(valBuf, sizeof(valBuf), "%d", facId);
                else if (strcmp(cname, "first_name") == 0)             snprintf(valBuf, sizeof(valBuf), "'System'");
                else if (strcmp(cname, "last_name") == 0)              snprintf(valBuf, sizeof(valBuf), "'Admin'");
                else if (strcmp(cname, "is_active") == 0)              snprintf(valBuf, sizeof(valBuf), "1");
                else if (strcmp(cname, "email") == 0)                  snprintf(valBuf, sizeof(valBuf), "'admin@localhost'");
                else if (strcmp(cname, "preferred_comm_channel") == 0) snprintf(valBuf, sizeof(valBuf), "'PORTAL'");
                else if (strcmp(cname, "password_hash") == 0)          snprintf(valBuf, sizeof(valBuf), "'%s'", hash);
                else if (strcmp(cname, "password_salt") == 0)          snprintf(valBuf, sizeof(valBuf), "'%s'", salt);
                else if (notNull && !hasDflt) {
                    if (ctype && (StrIStr(ctype, "INT") || StrIStr(ctype, "NUM") || StrIStr(ctype, "REAL") || StrIStr(ctype, "BOOL"))) {
                        snprintf(valBuf, sizeof(valBuf), "%d", StrIStr(cname, "facility") ? facId : 0);
                    } else {
                        snprintf(valBuf, sizeof(valBuf), "'N/A'");
                    }
                } else {
                    include = 0;
                }

                if (include) {
                    if (colCount > 0) {
                        strncat(cols, ", ", sizeof(cols) - strlen(cols) - 1);
                        strncat(vals, ", ", sizeof(vals) - strlen(vals) - 1);
                    }
                    strncat(cols, cname,  sizeof(cols) - strlen(cols) - 1);
                    strncat(vals, valBuf, sizeof(vals) - strlen(vals) - 1);
                    colCount++;
                }
            }
            sqlite3_finalize(st);
        }

        char insSql[3584], lastErr[192] = "";
        snprintf(insSql, sizeof(insSql), "INSERT INTO staff_users (%s) VALUES (%s);", cols, vals);

        sqlite3_exec(g_db, "PRAGMA foreign_keys = OFF;", 0, 0, 0);
        int rc = sqlite3_exec(g_db, insSql, 0, 0, 0);
        if (rc == SQLITE_OK) {
            userId = (int)sqlite3_last_insert_rowid(g_db);
        } else {
            strncpy(lastErr, sqlite3_errmsg(g_db), sizeof(lastErr) - 1);
        }
        sqlite3_exec(g_db, "PRAGMA foreign_keys = ON;", 0, 0, 0);

        if (userId <= 0) {
            snprintf(msgOut, msgSize, "Failed to create admin: %s", lastErr[0] ? lastErr : "unknown error");
            write_log(msgOut);
            return 0;
        }
        strcpy(uname, "admin");
    }

    if (sqlite3_prepare_v2(g_db, "UPDATE staff_users SET is_active=1, password_hash=? WHERE user_id=?", -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, hash, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st,  2, userId);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    const char *credSql =
        "INSERT INTO user_auth_credentials (user_id, password_salt, password_hash, must_change_password, updated_at) "
        "VALUES (?, ?, ?, 0, datetime('now')) "
        "ON CONFLICT(user_id) DO UPDATE SET "
        "password_salt=excluded.password_salt, password_hash=excluded.password_hash, updated_at=datetime('now');";
    int ok = 0;
    if (sqlite3_prepare_v2(g_db, credSql, -1, &st, 0) == SQLITE_OK) {
        sqlite3_bind_int(st,  1, userId);
        sqlite3_bind_text(st, 2, salt, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, hash, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_DONE) ok = 1;
        sqlite3_finalize(st);
    }

    if (!ok) {
        snprintf(msgOut, msgSize, "SQL error: %s", sqlite3_errmsg(g_db));
        write_log(msgOut);
        return 0;
    }

    char upd[128];
    snprintf(upd, sizeof(upd), "DELETE FROM user_sessions WHERE user_id=%d;", userId);
    sqlite3_exec(g_db, upd, 0, 0, 0);

    snprintf(msgOut, msgSize, "Reset OK: login '%s' / 'admin'", uname);
    write_log(msgOut);
    return 1;
}
void ProcessHttpClient(int clientIdx, char *pBuf) {
    SOCKET sock = g_clients[clientIdx];
    if (!sock) return;

    if (strncmp(pBuf, "GET / ", 6) == 0 || strncmp(pBuf, "GET / HTTP", 10) == 0 || strncmp(pBuf, "GET /index.html", 15) == 0) {
        ServeFile(sock, "index.html", "text/html");
    }
    else if (strncmp(pBuf, "GET /patients.js", 16) == 0) {
        ServeFile(sock, "patients.js", "application/javascript");
    }
    else if (strncmp(pBuf, "GET /admin.js", 13) == 0) {
        ServeFile(sock, "admin.js", "application/javascript");
    }
    else if (strncmp(pBuf, "GET /comms.js", 13) == 0) {
        ServeFile(sock, "comms.js", "application/javascript");
    }
    else if (strncmp(pBuf, "GET /physician.js", 17) == 0) {
        ServeFile(sock, "physician.js", "application/javascript");
    }
    else if (strncmp(pBuf, "GET /staff_schedule.html", 24) == 0) {
        ServeFile(sock, "staff_schedule.html", "text/html");
    }
    else if (strncmp(pBuf, "GET /staff_schedule.js", 22) == 0) {
        ServeFile(sock, "staff_schedule.js", "application/javascript");
    }
    else if (strncmp(pBuf, "GET /calendar_engine.js", 23) == 0) {
        ServeFile(sock, "calendar_engine.js", "application/javascript");
    }
else if (strncmp(pBuf, "POST /api/relink_dicom", 22) == 0) {
        HandleRelinkDicom(sock, pBuf, g_clientBufLen[clientIdx]);
    }
else if (strncmp(pBuf, "POST /api/upload_unlinked", 25) == 0) {
        HandleUploadUnlinked(sock, pBuf, g_clientBufLen[clientIdx]);
    }
    else if (strncmp(pBuf, "GET /jspdf.umd.min.js", 21) == 0) {
        ServeFile(sock, "jspdf.umd.min.js", "application/javascript");
    }
    else if (strncmp(pBuf, "GET /dicomviewer.js", 19) == 0) {
        ServeFile(sock, "dicomviewer.js", "application/javascript");
    }
else if (strncmp(pBuf, "GET /api/report_typ", 19) == 0) {
        char repIdStr[32] = "", regenStr[16] = "";
        GetQueryParam(pBuf, "report_id", repIdStr, sizeof(repIdStr));
        GetQueryParam(pBuf, "regen", regenStr, sizeof(regenStr));
        int repId = atoi(repIdStr);
        int forceRegen = (strcmp(regenStr, "1") == 0);

        if (repId > 0) {
            char *typBuf = (char*)malloc(32768);
            if (typBuf) {
                typBuf[0] = '\0';
                if (!forceRegen) {
                    sqlite3_stmt *st = NULL;
                    if (sqlite3_prepare_v2(g_db, "SELECT typst_source FROM report_typst_documents WHERE report_id=? LIMIT 1", -1, &st, 0) == SQLITE_OK) {
                        sqlite3_bind_int(st, 1, repId);
                        if (sqlite3_step(st) == SQLITE_ROW) {
                            const char *s = (const char*)sqlite3_column_text(st, 0);
                            if (s) { strncpy(typBuf, s, 32767); typBuf[32767] = '\0'; }
                        }
                        sqlite3_finalize(st);
                    }
                }
                if (!typBuf[0]) {
                    BuildAndStoreReportTypst(g_db, repId, typBuf, 32768);
                }
                if (typBuf[0]) {
                    SendHttpResponse(sock, "text/plain; charset=utf-8", typBuf, (int)strlen(typBuf));
                } else {
                    char err[] = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\nReport not found";
                    SendBufferCompletely(sock, err, (int)strlen(err));
                }
                free(typBuf);
            }
        } else {
            char err[] = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nInvalid report_id";
            SendBufferCompletely(sock, err, (int)strlen(err));
        }
    }
else if (strncmp(pBuf, "GET /api/me", 11) == 0) {
        HandleApiMe(sock, pBuf);
    }
    else if (strncmp(pBuf, "POST /api/login", 15) == 0) {
        char *body = strstr(pBuf, "\r\n\r\n");
        if (body) { body += 4; HandleApiLogin(sock, body); }
    }
    else if (strncmp(pBuf, "POST /api/logout", 16) == 0) {
        HandleApiLogout(sock, pBuf);
    }
else if (strncmp(pBuf, "POST /api/admin_set_password", 28) == 0) {
        char *body = strstr(pBuf, "\r\n\r\n");
        if (body) { body += 4; HandleApiAdminSetPassword(sock, pBuf, body); }
    }
    else if (strncmp(pBuf, "POST /api/change_my_password", 28) == 0) {
        char *body = strstr(pBuf, "\r\n\r\n");
        if (body) { body += 4; HandleApiChangeMyPassword(sock, pBuf, body); }
    }
    else if (strncmp(pBuf, "POST /api/admin_set_password", 28) == 0) {
        char *body = strstr(pBuf, "\r\n\r\n");
        if (body) { body += 4; HandleApiAdminSetPassword(sock, body); }
    }
    else if (strncmp(pBuf, "POST /api/request_password_reset", 32) == 0) {
        char *body = strstr(pBuf, "\r\n\r\n");
        if (body) { body += 4; HandleApiRequestPasswordReset(sock, body); }
    }
    else if (strncmp(pBuf, "POST /api/reset_password", 24) == 0) {
        char *body = strstr(pBuf, "\r\n\r\n");
        if (body) { body += 4; HandleApiResetPassword(sock, body); }
    }
else if (strncmp(pBuf, "GET /billing.js", 15) == 0) {
        ServeFile(sock, "billing.js", "application/javascript");
    }
else if (strncmp(pBuf, "GET /tech_console.js", 20) == 0) {
        ServeFile(sock, "tech_console.js", "application/javascript");
    }
else if (strncmp(pBuf, "GET /typst.js", 13) == 0) {
        ServeFile(sock, "typst.js", "application/javascript");
    }
else if (strncmp(pBuf, "GET /api/report_pdf", 19) == 0) {
    char repIdStr[32] = "";
    GetQueryParam(pBuf, "report_id", repIdStr, sizeof(repIdStr));
    int repId = atoi(repIdStr);
    char pdfPath[MAX_PATH] = "";
    if (repId > 0 && GenerateReportTypstPdf(g_db, repId, pdfPath, sizeof(pdfPath))) {
        ServeFile(sock, pdfPath, "application/pdf");
    } else {
        char err[] = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\nPDF unavailable (check rep_typst_exe in Admin)";
        SendBufferCompletely(sock, err, (int)strlen(err));
    }
}
else if (strncmp(pBuf, "GET /api/me", 11) == 0) {
        HandleApiMe(sock, pBuf);
    }
    else if (strncmp(pBuf, "GET /api/images", 15) == 0) {
        SyncCStoreToDB();
        HandleApiImages(sock, pBuf);
    }
    else if (strncmp(pBuf, "GET /download", 13) == 0) {
        HandleDownloadDicom(sock, pBuf);
    }
    else if (strncmp(pBuf, "POST /upload_new", 16) == 0) {
        HandleDicomUpload(sock, pBuf, g_clientBufLen[clientIdx], 0);
    }
    else if (strncmp(pBuf, "POST /replace", 13) == 0) {
        HandleDicomUpload(sock, pBuf, g_clientBufLen[clientIdx], 1);
    }
    else if (strncmp(pBuf, "POST /api/sync_worklist", 23) == 0) {
        SyncWorklistFromDB();
        SyncCStoreToDB();
        char ok[] = "{\"status\":\"SYNCED\"}";
        SendHttpResponse(sock, "application/json", ok, (int)strlen(ok));
    }
    else if (strncmp(pBuf, "POST /api/query", 15) == 0) {
        char *body = strstr(pBuf, "\r\n\r\n");
        if (body) {
            body += 4;
            char *clPtr = StrIStr(pBuf, "Content-Length:");
            if (clPtr) {
                int cl = atoi(clPtr + 15);
                if (cl >= 0 && cl < (pBuf + g_clientBufLen[clientIdx] - body)) {
                    body[cl] = '\0';
                }
            }
            HandleApiQuery(sock, body);
        }
    }
    else if (strncmp(pBuf, "POST /api/dispatch_reminder", 27) == 0) {
        char *body = strstr(pBuf, "\r\n\r\n");
        if (body) {
            body += 4;
            char *clPtr = StrIStr(pBuf, "Content-Length:");
            if (clPtr) {
                int cl = atoi(clPtr + 15);
                if (cl >= 0 && cl < (pBuf + g_clientBufLen[clientIdx] - body)) body[cl] = '\0';
            }
            HandleDispatchReminder(sock, body);
        } else {
            char err[] = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"Bad Request\"}";
            SendBufferCompletely(sock, err, (int)strlen(err));
        }
    }
    else {
        char err[] = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
        SendBufferCompletely(sock, err, (int)strlen(err));
    }
    RemoveClient(clientIdx);
}

void AppendReceivedBytes(int clientIdx, const char *pBytes, int cbBytes) {
    char *pBuf = g_clientBuffers[clientIdx];
    int curLen = g_clientBufLen[clientIdx];

    for (int idx = 0; idx < cbBytes; idx++) {
        if (curLen >= g_clientBufCap[clientIdx] - 1) {
            g_clientBufCap[clientIdx] *= 2;
            pBuf = (char*)realloc(pBuf, g_clientBufCap[clientIdx]);
            g_clientBuffers[clientIdx] = pBuf;
        }
        pBuf[curLen++] = pBytes[idx];
    }
    g_clientBufLen[clientIdx] = curLen;
    pBuf[curLen] = '\0';

    char *headerEnd = strstr(pBuf, "\r\n\r\n");
    if (headerEnd) {
        int headersLen = (int)(headerEnd - pBuf) + 4;
        char *clPtr = StrIStr(pBuf, "Content-Length:");
        int contentLen = 0;
        if (clPtr) contentLen = atoi(clPtr + 15);
        if (curLen >= headersLen + contentLen) {
            ProcessHttpClient(clientIdx, pBuf);
        }
    }
}

void PollClient(int clientIdx) {
    SOCKET sock = g_clients[clientIdx];
    if (sock == 0 || sock == INVALID_SOCKET) return;
    int cb = recv(sock, g_recvBuf, RECV_SIZE, 0);
    if (cb > 0) {
        g_clientLastTick[clientIdx] = GetTickCount();
        AppendReceivedBytes(clientIdx, g_recvBuf, cb);
    } else if (cb == 0) {
        RemoveClient(clientIdx);
    } else {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            RemoveClient(clientIdx);
        }
    }
    CheckPendingAndTimeout(clientIdx);
}

/* --- Server Execution --- */
int StartHttpServer(void) {
    struct sockaddr_in sin;
    g_listenSocketHttp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocketHttp == INVALID_SOCKET) return 0;

    int opt = 1;
    setsockopt(g_listenSocketHttp, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((u_short)g_HttpPort);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(g_listenSocketHttp, (struct sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) {
        closesocket(g_listenSocketHttp);
        return 0;
    }
    if (listen(g_listenSocketHttp, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(g_listenSocketHttp);
        return 0;
    }
    SetNonBlocking(g_listenSocketHttp);
    return 1;
}

void AcceptNewClients(void) {
    SOCKET s;
    while ((s = accept(g_listenSocketHttp, NULL, NULL)) != INVALID_SOCKET) {
        AddClient(s);
    }
}

void ServerStart(void) {
    if (g_bHttpRunning) return;
    StartHttpServer();
    SyncWorklistFromDB();
    ris_dicom_start();
    g_bHttpRunning = 1;
    ModifyMenu(g_hMenu, ID_TRAY_TOGGLE, MF_BYCOMMAND | MF_STRING | MF_CHECKED, ID_TRAY_TOGGLE, "Stop Server");
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void ServerStop(void) {
    if (!g_bHttpRunning) return;
    ris_dicom_stop();
    for (int i = 0; i < MAX_CLIENTS; i++) RemoveClient(i);
    if (g_listenSocketHttp != INVALID_SOCKET) {
        closesocket(g_listenSocketHttp);
        g_listenSocketHttp = INVALID_SOCKET;
    }
    g_bHttpRunning = 0;
    ModifyMenu(g_hMenu, ID_TRAY_TOGGLE, MF_BYCOMMAND | MF_STRING | MF_UNCHECKED, ID_TRAY_TOGGLE, "Start Server");
}

void ServerLoop(void) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) g_bAppExit = 1;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (!g_bHttpRunning) return;
    AcceptNewClients();
    for (int i = 0; i < MAX_CLIENTS; i++) PollClient(i);

    DWORD now = GetTickCount();
    if (now - g_lastCStoreCheckTick >= 1500) {
        g_lastCStoreCheckTick = now;
        SyncCStoreToDB();
    }
if (now - g_lastOutboundDispatchTick >= 5000) {
        g_lastOutboundDispatchTick = now;
        ProcessOutboundDispatchQueue();
    }
}

/* --- Settings & System Tray UI --- */
BOOL CALLBACK SetFontProc(HWND hChild, LPARAM lParam) {
    SendMessage(hChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int s_confirmDelPatients = 0;
    static int s_confirmDelAll = 0;

    switch (msg) {
        case WM_CREATE: {
            s_confirmDelPatients = 0;
            s_confirmDelAll = 0;

            int y = 20, x = 10;
            char aetClean[32];
            strncpy(aetClean, g_aeCalled, sizeof(aetClean) - 1);
            aetClean[sizeof(aetClean) - 1] = '\0';
            TrimInPlace(aetClean);

            CreateWindow("STATIC", "AET Title:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", aetClean, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x + 90, y, 200, 20, hWnd, (HMENU)ID_EDT_AET, g_hInstance, NULL); y += 30;
            char buf[64]; snprintf(buf, sizeof(buf), "%d", g_TelnetPort);
            CreateWindow("STATIC", "Telnet Port:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, x + 90, y, 60, 20, hWnd, (HMENU)ID_EDT_TELNET, g_hInstance, NULL); y += 30;
            snprintf(buf, sizeof(buf), "%d", g_HttpPort);
            CreateWindow("STATIC", "HTTP Port:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, x + 90, y, 60, 20, hWnd, (HMENU)ID_EDT_HTTP, g_hInstance, NULL); y += 30;
            snprintf(buf, sizeof(buf), "%d", g_DicomPort);
            CreateWindow("STATIC", "DICOM Port:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, x + 90, y, 60, 20, hWnd, (HMENU)ID_EDT_DICOM, g_hInstance, NULL); y += 30;
            snprintf(buf, sizeof(buf), "%d", g_TelnetTimeout);
            CreateWindow("STATIC", "Timeout (sec):", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, x + 90, y, 60, 20, hWnd, (HMENU)ID_EDT_TIMEOUT, g_hInstance, NULL); y += 30;
            HWND hChk = CreateWindow("BUTTON", "Enable Debug Log", BS_CHECKBOX | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, 150, 20, hWnd, (HMENU)ID_CHK_DEBUG, g_hInstance, NULL);
            SendMessage(hChk, BM_SETCHECK, g_DebugLog ? BST_CHECKED : BST_UNCHECKED, 0); y += 30;

            HWND hChkRelay = CreateWindow("BUTTON", "Enable Relay Mode", BS_CHECKBOX | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, 150, 20, hWnd, (HMENU)ID_CHK_RELAY, g_hInstance, NULL);
            SendMessage(hChkRelay, BM_SETCHECK, g_bRelayMode ? BST_CHECKED : BST_UNCHECKED, 0);
            HWND hChkRelayDel = CreateWindow("BUTTON", "Delete After Relay", BS_CHECKBOX | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x + 160, y, 150, 20, hWnd, (HMENU)ID_CHK_RELAY_DEL, g_hInstance, NULL);
            SendMessage(hChkRelayDel, BM_SETCHECK, g_bRelayDelete ? BST_CHECKED : BST_UNCHECKED, 0); y += 30;

            CreateWindow("STATIC", "Relay IP:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", g_relayIp, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x + 90, y, 200, 20, hWnd, (HMENU)ID_EDT_RELAY_IP, g_hInstance, NULL); y += 30;
            snprintf(buf, sizeof(buf), "%d", g_relayPort);
            CreateWindow("STATIC", "Relay Port:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, x + 90, y, 60, 20, hWnd, (HMENU)ID_EDT_RELAY_PORT, g_hInstance, NULL); y += 30;
            CreateWindow("STATIC", "Relay AET:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", g_relayAet, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x + 90, y, 200, 20, hWnd, (HMENU)ID_EDT_RELAY_AET, g_hInstance, NULL); y += 30;

            CreateWindow("STATIC", "Ext Cmd:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", g_relayCmd, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x + 90, y, 160, 20, hWnd, (HMENU)ID_EDT_RELAY_CMD, g_hInstance, NULL);
            CreateWindow("BUTTON", "...", BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x + 255, y, 35, 20, hWnd, (HMENU)ID_BTN_RELAY_BROWSE, g_hInstance, NULL); y += 38;

            CreateWindow("BUTTON", g_bHttpRunning ? "Stop Server" : "Start Server", BS_OWNERDRAW | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x + 40, y, 250, 32, hWnd, (HMENU)ID_BTN_STARTSTOP, g_hInstance, NULL); y += 42;

            CreateWindow("BUTTON", "Delete Patients", BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, 150, 26, hWnd, (HMENU)ID_BTN_DEL_PATIENTS, g_hInstance, NULL);
            CreateWindow("BUTTON", "Delete All Data", BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x + 165, y, 150, 26, hWnd, (HMENU)ID_BTN_DEL_ALL, g_hInstance, NULL); y += 32;

            CreateWindow("BUTTON", "Reset Admin Password to 'admin'", BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, 315, 26, hWnd, (HMENU)ID_BTN_RESET_ADMIN, g_hInstance, NULL); y += 32;

            CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE, x, y, 315, 20, hWnd, (HMENU)ID_LBL_SETTINGS_MSG, g_hInstance, NULL); y += 26;

            CreateWindow("BUTTON", "OK", BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, 80, 25, hWnd, (HMENU)ID_OK, g_hInstance, NULL);
            CreateWindow("BUTTON", "Cancel", BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x + 90, y, 80, 25, hWnd, (HMENU)IDCANCEL, g_hInstance, NULL);
            EnumChildWindows(hWnd, SetFontProc, (LPARAM)GetStockObject(DEFAULT_GUI_FONT));
            return 0;
        }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
            if (lpdis->CtlID == ID_BTN_STARTSTOP) {
                HBRUSH hBrush = CreateSolidBrush(g_bHttpRunning ? RGB(200, 255, 200) : RGB(255, 200, 200));
                FillRect(lpdis->hDC, &lpdis->rcItem, hBrush);
                DeleteObject(hBrush);
                DrawEdge(lpdis->hDC, &lpdis->rcItem, (lpdis->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
                SetBkMode(lpdis->hDC, TRANSPARENT);
                SetTextColor(lpdis->hDC, RGB(0, 0, 0));
                char szText[128];
                GetWindowText(lpdis->hwndItem, szText, sizeof(szText));
                DrawText(lpdis->hDC, szText, -1, &lpdis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (lpdis->itemState & ODS_FOCUS) { RECT rcFocus = lpdis->rcItem; InflateRect(&rcFocus, -3, -3); DrawFocusRect(lpdis->hDC, &rcFocus); }
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetBkMode(hdcStatic, TRANSPARENT);
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        case WM_COMMAND: {
            if (lParam && HIWORD(wParam) == BN_CLICKED) {
                int id = LOWORD(wParam);
                if (id == ID_BTN_STARTSTOP) {
                    if (g_bHttpRunning) { ServerStop(); SetWindowText((HWND)lParam, "Start Server"); }
                    else { ServerStart(); SetWindowText((HWND)lParam, "Stop Server"); }
                    InvalidateRect((HWND)lParam, NULL, TRUE);
                }
                else if (id == ID_CHK_DEBUG || id == ID_CHK_RELAY || id == ID_CHK_RELAY_DEL) {
                    LRESULT chk = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0);
                    SendMessage((HWND)lParam, BM_SETCHECK, chk == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
                }
                else if (id == ID_BTN_RELAY_BROWSE) {
                    char fileBuf[MAX_PATH] = {0};
                    OPENFILENAMEA ofn = {0};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFilter = "Executables (*.exe;*.bat;*.cmd)\0*.exe;*.bat;*.cmd\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = fileBuf;
                    ofn.nMaxFile = sizeof(fileBuf);
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameA(&ofn)) SetDlgItemTextA(hWnd, ID_EDT_RELAY_CMD, fileBuf);
                }
                else if (id == ID_BTN_DEL_PATIENTS) {
                    s_confirmDelAll = 0;
                    SetDlgItemTextA(hWnd, ID_BTN_DEL_ALL, "Delete All Data");
                    if (!s_confirmDelPatients) {
                        s_confirmDelPatients = 1;
                        SetDlgItemTextA(hWnd, ID_BTN_DEL_PATIENTS, "Confirm Delete Patients?");
                        SetDlgItemTextA(hWnd, ID_LBL_SETTINGS_MSG, "Click again to permanently delete all patient data.");
                    } else {
                        s_confirmDelPatients = 0;
                        SetDlgItemTextA(hWnd, ID_BTN_DEL_PATIENTS, "Delete Patients");
                        char err[256] = {0};
                        if (DeletePatientDataFromDB(err, sizeof(err))) {
                            SetDlgItemTextA(hWnd, ID_LBL_SETTINGS_MSG, "All patient, order, report, and DICOM data deleted.");
                        } else {
                            SetDlgItemTextA(hWnd, ID_LBL_SETTINGS_MSG, err);
                        }
                    }
                }
                else if (id == ID_BTN_DEL_ALL) {
                    s_confirmDelPatients = 0;
                    SetDlgItemTextA(hWnd, ID_BTN_DEL_PATIENTS, "Delete Patients");
                    if (!s_confirmDelAll) {
                        s_confirmDelAll = 1;
                        SetDlgItemTextA(hWnd, ID_BTN_DEL_ALL, "Confirm Delete ALL?");
                        SetDlgItemTextA(hWnd, ID_LBL_SETTINGS_MSG, "Click again to wipe ALL database tables.");
                    } else {
                        s_confirmDelAll = 0;
                        SetDlgItemTextA(hWnd, ID_BTN_DEL_ALL, "Delete All Data");
                        char err[256] = {0};
                        if (DeleteAllDataFromDB(err, sizeof(err))) {
                            SetDlgItemTextA(hWnd, ID_LBL_SETTINGS_MSG, "All database tables wiped.");
                        } else {
                            SetDlgItemTextA(hWnd, ID_LBL_SETTINGS_MSG, err);
                        }
                    }
                }
                else if (id == ID_BTN_RESET_ADMIN) {
                    s_confirmDelPatients = 0;
                    s_confirmDelAll = 0;
                    SetDlgItemTextA(hWnd, ID_BTN_DEL_PATIENTS, "Delete Patients");
                    SetDlgItemTextA(hWnd, ID_BTN_DEL_ALL, "Delete All Data");
                    char msgBuf[256] = {0};
                    ResetAdminPasswordInDB(msgBuf, sizeof(msgBuf));
                    SetDlgItemTextA(hWnd, ID_LBL_SETTINGS_MSG, msgBuf);
                }
                else if (id == ID_OK) {
                    BOOL trans;
                    char aetTmp[32] = {0};
                    GetDlgItemTextA(hWnd, ID_EDT_AET, aetTmp, sizeof(aetTmp));
                    if (aetTmp[0]) SetAETitle(aetTmp);

                    int hp = GetDlgItemInt(hWnd, ID_EDT_HTTP, &trans, FALSE); if (trans) g_HttpPort = hp;
                    int tp = GetDlgItemInt(hWnd, ID_EDT_TELNET, &trans, FALSE); if (trans) g_TelnetPort = tp;
                    int dp = GetDlgItemInt(hWnd, ID_EDT_DICOM, &trans, FALSE); if (trans) g_DicomPort = dp;
                    int to = GetDlgItemInt(hWnd, ID_EDT_TIMEOUT, &trans, FALSE); if (trans) g_TelnetTimeout = to;
                    int rp = GetDlgItemInt(hWnd, ID_EDT_RELAY_PORT, &trans, FALSE); if (trans) g_relayPort = rp;

                    g_DebugLog     = (SendDlgItemMessage(hWnd, ID_CHK_DEBUG, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                    g_bRelayMode   = (SendDlgItemMessage(hWnd, ID_CHK_RELAY, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                    g_bRelayDelete = (SendDlgItemMessage(hWnd, ID_CHK_RELAY_DEL, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;

                    GetDlgItemTextA(hWnd, ID_EDT_RELAY_IP,  g_relayIp,  sizeof(g_relayIp));
                    GetDlgItemTextA(hWnd, ID_EDT_RELAY_AET, g_relayAet, sizeof(g_relayAet));
                    GetDlgItemTextA(hWnd, ID_EDT_RELAY_CMD, g_relayCmd, sizeof(g_relayCmd));

                    SaveConfig();
                    DestroyWindow(hWnd);
                }
                else if (id == IDCANCEL) {
                    DestroyWindow(hWnd);
                }
            }
            return 0;
        }
        case WM_DESTROY: {
            s_confirmDelPatients = 0;
            s_confirmDelAll = 0;
            g_hSettingsDlg = NULL;
            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void ShowSettingsDialog(void) {
    if (g_hSettingsDlg != NULL) { SetForegroundWindow(g_hSettingsDlg); return; }
    HWND hParent = g_hMainWnd; RECT rc; GetWindowRect(hParent, &rc); int width = 350, height = 575;
    int left = rc.left + (rc.right - rc.left - width) / 2; int top = rc.top + (rc.bottom - rc.top - height) / 2;
    static int classRegistered = 0;
    if (!classRegistered) {
        WNDCLASSEX wc = {0};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = g_hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "SettingsWndClass";
        RegisterClassEx(&wc);
        classRegistered = 1;
    }
    g_hSettingsDlg = CreateWindowEx(0, "SettingsWndClass", "Server Settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, left, top, width, height, hParent, NULL, g_hInstance, NULL);
}
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TRAYICON) {
        if ((UINT)lParam == WM_RBUTTONUP || (UINT)lParam == WM_LBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(hWnd);
            TrackPopupMenu(g_hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
        }
        return 0;
    }
    if (uMsg == WM_COMMAND) {
        int cmd = LOWORD(wParam);
        if (cmd == ID_TRAY_TOGGLE) { if (g_bHttpRunning) ServerStop(); else ServerStart(); return 0; }
        if (cmd == ID_TRAY_SHOW) { HWND cw = GetConsoleWindow(); if (cw) { ShowWindow(cw, SW_SHOW); SetForegroundWindow(cw); } return 0; }
        if (cmd == ID_TRAY_EXIT) { g_bAppExit = 1; PostQuitMessage(0); return 0; }
        if (cmd == ID_TRAY_SETTINGS) { ShowSettingsDialog(); return 0; }
        return 0;
    }
    if (uMsg == WM_CLOSE) { g_bAppExit = 1; PostQuitMessage(0); return 0; }
    if (uMsg == WM_DESTROY) {
        g_bAppExit = 1;
        ServerStop();
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void CreateTray(void) {
    WNDCLASSEX wc;
    g_hInstance = GetModuleHandle(NULL);
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = szWndClass;
    RegisterClassEx(&wc);

    g_hMainWnd = CreateWindowEx(0, szWndClass, szTrayTip, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, HWND_MESSAGE, NULL, g_hInstance, NULL);
    g_hMenu = CreatePopupMenu();
    AppendMenu(g_hMenu, MF_STRING | (g_bHttpRunning ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_TOGGLE, g_bHttpRunning ? "Stop Server" : "Start Server");
    AppendMenu(g_hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(g_hMenu, MF_STRING, ID_TRAY_SETTINGS, "Settings");
    AppendMenu(g_hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(g_hMenu, MF_STRING, ID_TRAY_SHOW, "Show Console");
    AppendMenu(g_hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(g_hMenu, MF_STRING, ID_TRAY_EXIT, "Exit");

    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = g_hMainWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy(nid.szTip, szTrayTip);
    Shell_NotifyIcon(NIM_ADD, &nid);
}

/* --- Main --- */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    InitDB();
    ris_dicom_init(NULL);
    SyncWorklistFromDB();
    SyncCStoreToDB();
    InitArrays();

    CreateTray();
    ServerStart();

    MSG qmsg;
    for (;;) {
        ServerLoop();
        if (g_bAppExit) break;
        if (PeekMessage(&qmsg, NULL, 0, 0, PM_REMOVE)) {
            if (qmsg.message == WM_QUIT) break;
            TranslateMessage(&qmsg);
            DispatchMessage(&qmsg);
        } else {
            Sleep(10);
        }
    }

    ServerStop();
    ris_dicom_shutdown();
    sqlite3_close(g_db);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clientBuffers[i]) { free(g_clientBuffers[i]); g_clientBuffers[i] = NULL; }
    }
    return 0;
}
