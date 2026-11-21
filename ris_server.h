/* ============================================================================
 * RIS Embeddable DICOM Worklist & PACS Storage Server (Single-Header Library)
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 *
 * Usage:
 *   #include "ris_dicom_server.h"
 *
 * Link (MinGW-w64 GCC):
 *   gcc -Os -s -o app.exe main.c -lws2_32 -lkernel32
 * ============================================================================ */

#ifndef RIS_DICOM_SERVER_H
#define RIS_DICOM_SERVER_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "kernel32.lib")

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Constants ===== */
#define FIELD_COUNT         27
#define FIELD_SIZE          1024
#define ENTRY_SIZE          (FIELD_COUNT * FIELD_SIZE)
#define LINE_SIZE           65536
#define MAX_CSV_LINE        32768
#define BUFFER_SIZE         131072
#define MAX_PATIENT_CSV_MB  (20 * 1024 * 1024)
#define MAX_FIELD_LEN       512
#define MAX_RULES           256
#define MAX_CSV_ROWS        16384
#define MAX_PROC_CODES      256

/* ===== Global State & Configuration ===== */
static int g_DicomPort = 1104;
static int g_DebugLog = 1;
static volatile int g_bRunning = 0;
static volatile int g_bExitApp = 0;
static int g_wsaInitialized = 0;
static int g_locksInitialized = 0;

static SOCKET g_listenSocketDicom = INVALID_SOCKET;
static HANDLE g_hDicomThread = NULL;
static HANDLE g_hBackupThread = NULL;

static char g_aeCalled[32] = "PACS";
static char g_delPwd[128] = "0";
static char szIniFile[MAX_PATH] = {0};
static char g_omittedFields[256] = "0";

static CRITICAL_SECTION g_csvLock;
static CRITICAL_SECTION g_procCodesLock;
static CRITICAL_SECTION g_cstoreLock;

/* Storage Globals */
static char g_storageRoot[MAX_PATH] = "dicomstore";
static char g_storageFolder[32] = "D0000000";
static int g_storageIndex = 0;
static uint8_t *g_findData = NULL;

/* Relay Mode Globals */
static int g_bRelayMode = 0;
static int g_bRelayDelete = 0;
static char g_relayIp[64] = "127.0.0.1";
static int g_relayPort = 104;
static char g_relayAet[32] = "REMOTE_PACS";
static char g_relayCmd[MAX_PATH] = "";

static char g_lineOut[LINE_SIZE];
static char g_iniBuf[256];

/* In-memory Worklist CSV Store */
static char g_csvData[MAX_CSV_ROWS][LINE_SIZE];
static int  g_csvRows = 0;
static int  g_csvInitialized = 0;

/* Procedure Codes Lookup Table */
typedef struct {
    char id[128];
    char procCode[128];
    char modality[128];
    char name[128];
} ProcCodeEntry;

static ProcCodeEntry g_procCodes[MAX_PROC_CODES];
static int g_procCodeCount = 0;
static int g_procCodesLoaded = 0;

/* Patient Directory CSV Buffers */
static unsigned char g_raw_csv_buf[MAX_PATIENT_CSV_MB];
static char g_converted_csv_buf[MAX_PATIENT_CSV_MB];

typedef enum {
    ENCODING_UNKNOWN,
    ENCODING_UTF8,
    ENCODING_UTF8_BOM,
    ENCODING_UTF16_LE,
    ENCODING_UTF16_BE,
    ENCODING_ASCII
} Encoding;

static char *patients_csv_data = NULL;
static long patients_csv_data_len = 0;
static int pts_name_col_idx = -1;
static int pts_id_col_idx = 0;
static int pts_birthdate_col_idx = -1;
static int pts_sex_col_idx = -1;

/* DICOM Editor Structures & Tag Dictionary */
typedef struct {
    uint16_t group;
    uint16_t elem;
    char *new_val;
    int applied;
} EditRule;

typedef struct {
    const char *name;
    uint16_t group;
    uint16_t elem;
    const char *vr;
} TagDict;

static TagDict DICT[] = {
    {"FileMetaInformationVersion", 0x0002, 0x0001, "OB"},
    {"MediaStorageSOPClassUID", 0x0002, 0x0002, "UI"},
    {"MediaStorageSOPInstanceUID", 0x0002, 0x0003, "UI"},
    {"TransferSyntaxUID", 0x0002, 0x0010, "UI"},
    {"ImplementationClassUID", 0x0002, 0x0012, "UI"},
    {"ImplementationVersionName", 0x0002, 0x0013, "SH"},
    {"SourceApplicationEntityTitle", 0x0002, 0x0016, "AE"},
    {"ImageType", 0x0008, 0x0008, "CS"},
    {"InstanceCreationDate", 0x0008, 0x0012, "DA"},
    {"InstanceCreationTime", 0x0008, 0x0013, "TM"},
    {"SOPClassUID", 0x0008, 0x0016, "UI"},
    {"SOPInstanceUID", 0x0008, 0x0018, "UI"},
    {"StudyDate", 0x0008, 0x0020, "DA"},
    {"SeriesDate", 0x0008, 0x0021, "DA"},
    {"AcquisitionDate", 0x0008, 0x0022, "DA"},
    {"ContentDate", 0x0008, 0x0023, "DA"},
    {"StudyTime", 0x0008, 0x0030, "TM"},
    {"SeriesTime", 0x0008, 0x0031, "TM"},
    {"AcquisitionTime", 0x0008, 0x0032, "TM"},
    {"ContentTime", 0x0008, 0x0033, "TM"},
    {"AccessionNumber", 0x0008, 0x0050, "SH"},
    {"Modality", 0x0008, 0x0060, "CS"},
    {"Manufacturer", 0x0008, 0x0070, "LO"},
    {"InstitutionName", 0x0008, 0x0080, "LO"},
    {"InstitutionAddress", 0x0008, 0x0081, "ST"},
    {"ReferringPhysicianName", 0x0008, 0x0090, "PN"},
    {"StationName", 0x0008, 0x1010, "SH"},
    {"StudyDescription", 0x0008, 0x1030, "LO"},
    {"SeriesDescription", 0x0008, 0x103E, "LO"},
    {"InstitutionalDepartmentName", 0x0008, 0x1040, "LO"},
    {"PhysiciansOfRecord", 0x0008, 0x1048, "PN"},
    {"PerformingPhysicianName", 0x0008, 0x1050, "PN"},
    {"NameOfPhysiciansReadingStudy", 0x0008, 0x1060, "PN"},
    {"OperatorsName", 0x0008, 0x1070, "PN"},
    {"ManufacturerModelName", 0x0008, 0x1090, "LO"},
    {"PatientName", 0x0010, 0x0010, "PN"},
    {"PatientID", 0x0010, 0x0020, "LO"},
    {"IssuerOfPatientID", 0x0010, 0x0021, "LO"},
    {"PatientBirthDate", 0x0010, 0x0030, "DA"},
    {"PatientBirthTime", 0x0010, 0x0032, "TM"},
    {"PatientSex", 0x0010, 0x0040, "CS"},
    {"OtherPatientIDs", 0x0010, 0x1000, "LO"},
    {"OtherPatientNames", 0x0010, 0x1001, "PN"},
    {"PatientAge", 0x0010, 0x1010, "AS"},
    {"PatientSize", 0x0010, 0x1020, "DS"},
    {"PatientWeight", 0x0010, 0x1030, "DS"},
    {"StudyInstanceUID", 0x0020, 0x000D, "UI"},
    {"SeriesInstanceUID", 0x0020, 0x000E, "UI"},
    {"StudyID", 0x0020, 0x0010, "SH"},
    {"SeriesNumber", 0x0020, 0x0011, "IS"},
    {"AcquisitionNumber", 0x0020, 0x0012, "IS"},
    {"InstanceNumber", 0x0020, 0x0013, "IS"},
    {"PixelData", 0x7FE0, 0x0010, "OW"},
    {NULL, 0, 0, NULL}
};

/* MWL & Relay Structures */
typedef struct {
    char patientName[128], patientID[128], dob[128], sex[128], reqPhys[128], reqSvc[128], procDesc[128], procReason[128];
    char accNum[128], procID[128], procPri[128], aeTitle[128], spsDate[128], spsTime[128], perfPhys[128];
    char spsDesc[128], spsID[128], station[128], loc[128], status[128], modality[128], refPhys[128], studyDesc[128];
    char protoCode[128], protoScheme[128], protoMeaning[128], studyUID[128];
} MWLEntry;

typedef struct {
    char sopClass[128];
    char sopInst[128];
    char filePath[MAX_PATH];
} RelayTask;

/* INI Keys & Strings */
static const char *szIniServer      = "Server";
static const char *szIniAET         = "AETitle";
static const char *szIniDicomPort   = "DicomPort";
static const char *szIniDebugLog    = "DebugLog";
static const char *szIniDelPwd      = "DeletePassword";
static const char *szIniStorageRoot = "StorageRoot";
static const char *szIniRelayMode   = "RelayMode";
static const char *szIniRelayIp     = "RelayIp";
static const char *szIniRelayPort   = "RelayPort";
static const char *szIniRelayAet    = "RelayAet";
static const char *szIniRelayCmd    = "RelayCmd";
static const char *szIniRelayDel    = "RelayDelete";

static const char *szDefAET         = "PACS";
static const char *szDefDelPwd      = "0";
static const char *szStartup        = "RIS DICOM Server starting...\r\n";
static const char *szConfigFmt      = "Config: AET=%s DicomPort=%u DebugLog=%u RelayMode=%u\r\n";
static const char *szListenDicomFmt = "DICOM SCP listening on port %u\r\n";
static const char *szListenFailFmt  = "ERROR: Failed to listen on port %u\r\n";
static const char *szDicomConnFmt   = "DICOM SCU connected.\r\n";
static const char *szServerStarted  = "DICOM Server STARTED\r\n";
static const char *szServerStopped  = "DICOM Server STOPPED\r\n";
static const char *szCSVHeader      = "PatientName,PatientID,BirthDate,Sex,ReqPhys,ReqSvc,ProcDesc,Reason,Accession,ProcID,Priority,StationAE,StartDate,StartTime,PerfPhys,SPSDesc,SPSID,StationName,Location,Status,Modality,RefPhys,StudyDesc,ProtoCode,ProtoScheme,ProtoMeaning,StudyUID";
static const char *szFakePatient    = "";

/* Forward Declarations */
static Encoding detect_encoding(const unsigned char *buf, long len);
static int utf16le_to_utf8(const wchar_t *input, char *output, int max_output);
static int utf16be_to_utf8(const unsigned char *input, char *output, int max_output);
static char *GetFieldPtr(char *pEntry, int fieldIdx);
static int get_csv_field(const char *line, int target_col, char *out_buf, int max_len);
static bool apply_dicom_edits_file(const char *input_file, const char *output_file, const char *rules_str, const char *delim);
static int AccessionEquals(const char *pLine, const char *pAccession);
static int ParseCSVLineToEntry(const char *pLine, char *pEntry);

/* ===== String & CSV Formatting Helpers ===== */
static int IsWhite(char ival) {
    return ival == ' ' || ival == '\t' || ival == '\r' || ival == '\n';
}

static void TrimInPlace(char *pStr) {
    char *p = pStr;
    while (*p && IsWhite(*p)) p++;
    if (p != pStr) memmove(pStr, p, strlen(p) + 1);
    size_t len = strlen(pStr);
    while (len > 0 && IsWhite(pStr[len - 1])) pStr[--len] = '\0';
}

static char UpperChar(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

static int StrIEquals(const char *a, const char *b) {
    while (UpperChar(*a) == UpperChar(*b)) {
        if (*a == '\0') return 1;
        a++;
        b++;
    }
    return 0;
}

static int StrNIEquals(const char *a, const char *b, int len) {
    for (int i = 0; i < len; i++) {
        if (UpperChar(a[i]) != UpperChar(b[i])) return 0;
        if (a[i] == '\0' || b[i] == '\0') return 0;
    }
    return 1;
}

static int str_icontains(const char *haystack, const char *needle) {
    if (!*needle) return 1;
    for (int i = 0; haystack[i] != '\0'; i++) {
        int j = 0;
        while (needle[j] != '\0' && haystack[i + j] != '\0' &&
               tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (needle[j] == '\0') return 1;
    }
    return 0;
}

static char *GetFieldPtr(char *pEntry, int fieldIdx) {
    return pEntry + (fieldIdx * FIELD_SIZE);
}

static int NeedsQuote(const char *pField) {
    while (*pField) {
        if (*pField == ',' || *pField == '"') return 1;
        pField++;
    }
    return 0;
}

static void BuildCSVLineFromEntry(char *pEntry, char *pOut) {
    char *pCur = pOut;
    for (int fieldIdx = 0; fieldIdx < FIELD_COUNT; fieldIdx++) {
        if (fieldIdx > 0) *pCur++ = ',';
        char *pField = GetFieldPtr(pEntry, fieldIdx);
        int q = NeedsQuote(pField);
        if (q) *pCur++ = '"';
        for (const char *s = pField; *s; s++) {
            if (q && *s == '"') *pCur++ = '"';
            if (pCur - pOut < LINE_SIZE - 2) *pCur++ = *s;
        }
        if (q) *pCur++ = '"';
    }
    *pCur = '\0';
}

static void StripDashes(char *pStr) {
    char *dst = pStr;
    while (*pStr) {
        if (*pStr != '-') *dst++ = *pStr;
        pStr++;
    }
    *dst = '\0';
}

static void StripColons(char *pStr) {
    char *dst = pStr;
    char *orig = pStr;
    while (*pStr) {
        if (*pStr != ':') *dst++ = *pStr;
        pStr++;
    }
    *dst = '\0';
    if (strlen(orig) == 4) {
        strcat(orig, "00");
    }
}

static void FormatPatientName(char *pName) {
    TrimInPlace(pName);
    if (*pName == '\0' || strchr(pName, '^') != NULL) return;
    char temp[512];
    char *comma = strchr(pName, ',');
    if (comma) {
        *comma = '\0';
        char last[256], rest[256];
        strncpy(last, pName, 255); last[255] = '\0';
        strncpy(rest, comma + 1, 255); rest[255] = '\0';
        TrimInPlace(last);
        TrimInPlace(rest);
        char *space = strchr(rest, ' ');
        while (space) { *space = '^'; space = strchr(space + 1, ' '); }
        snprintf(temp, sizeof(temp), "%s^%s", last, rest);
        strncpy(pName, temp, FIELD_SIZE - 1); pName[FIELD_SIZE - 1] = '\0';
        int len = (int)strlen(pName);
        while (len > 0 && pName[len - 1] == '^') pName[--len] = '\0';
    } else {
        char *space1 = strchr(pName, ' ');
        if (!space1) return;
        char *lastSpace = strrchr(pName, ' ');
        if (lastSpace == space1) {
            *space1 = '\0';
            snprintf(temp, sizeof(temp), "%s^%s", space1 + 1, pName);
            strncpy(pName, temp, FIELD_SIZE - 1); pName[FIELD_SIZE - 1] = '\0';
        } else {
            *lastSpace = '\0';
            char last[256]; strncpy(last, lastSpace + 1, 255); last[255] = '\0';
            char rest[256]; strncpy(rest, pName, 255); rest[255] = '\0';
            char *sp = strchr(rest, ' ');
            while (sp) { *sp = '^'; sp = strchr(sp + 1, ' '); }
            snprintf(temp, sizeof(temp), "%s^%s", last, rest);
            strncpy(pName, temp, FIELD_SIZE - 1); pName[FIELD_SIZE - 1] = '\0';
        }
    }
}

static int get_csv_field(const char *line, int target_col, char *out_buf, int max_len) {
    int current_col = 0, in_quotes = 0, pos = 0, out_pos = 0;
    while (line[pos] != '\0' && current_col <= target_col) {
        char c = line[pos];
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ',' && !in_quotes) {
            if (current_col == target_col) break;
            current_col++;
        } else {
            if (current_col == target_col && out_pos < max_len - 1) {
                out_buf[out_pos++] = c;
            }
        }
        pos++;
    }
    out_buf[out_pos] = '\0';
    TrimInPlace(out_buf);
    return (current_col == target_col);
}

/* ===== Logging ===== */
static void write_log(const char *msg) {
    if (!g_DebugLog) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    FILE *fp = fopen("ris_server.log", "a");
    if (fp) {
        fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] %s\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        fclose(fp);
    }
    printf("[%04d-%02d-%02d %02d:%02d:%02d.%03d] %s\n",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
    fflush(stdout);
}

/* ===== DICOM Low-Level PDU & Element Helpers ===== */
static void send_file_pdata_tf(SOCKET s, uint8_t pc_id, const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return;

    uint8_t pdu_buf[32768];
    int chunk_size = 32768 - 12;

    while (1) {
        uint8_t data_chunk[32768];
        size_t bytes_read = fread(data_chunk, 1, chunk_size, f);
        if (bytes_read == 0) break;

        uint8_t msg_ctrl = feof(f) ? 0x02 : 0x00;

        uint32_t pdv_len = 2 + (uint32_t)bytes_read;
        uint32_t pdu_len = 4 + pdv_len;

        pdu_buf[0] = 0x04; pdu_buf[1] = 0x00;
        pdu_buf[2] = (pdu_len >> 24) & 0xFF; pdu_buf[3] = (pdu_len >> 16) & 0xFF;
        pdu_buf[4] = (pdu_len >> 8) & 0xFF;  pdu_buf[5] = pdu_len & 0xFF;
        pdu_buf[6] = (pdv_len >> 24) & 0xFF; pdu_buf[7] = (pdv_len >> 16) & 0xFF;
        pdu_buf[8] = (pdv_len >> 8) & 0xFF;  pdu_buf[9] = pdv_len & 0xFF;
        pdu_buf[10] = pc_id; pdu_buf[11] = msg_ctrl;

        memcpy(&pdu_buf[12], data_chunk, bytes_read);
        send(s, (char *)pdu_buf, 6 + pdu_len, 0);
        if (feof(f)) break;
    }
    fclose(f);
}

static void send_pdata_tf(SOCKET s, uint8_t pc_id, uint8_t msg_ctrl, const uint8_t *data, uint32_t data_len) {
    uint32_t pdv_len = 2 + data_len;
    uint32_t pdu_len = 4 + pdv_len;
    static uint8_t pdu_buf[BUFFER_SIZE];
    if (6 + pdu_len > sizeof(pdu_buf)) return;

    pdu_buf[0] = 0x04; pdu_buf[1] = 0x00;
    pdu_buf[2] = (pdu_len >> 24) & 0xFF; pdu_buf[3] = (pdu_len >> 16) & 0xFF;
    pdu_buf[4] = (pdu_len >> 8) & 0xFF;  pdu_buf[5] = pdu_len & 0xFF;
    pdu_buf[6] = (pdv_len >> 24) & 0xFF; pdu_buf[7] = (pdv_len >> 16) & 0xFF;
    pdu_buf[8] = (pdv_len >> 8) & 0xFF;  pdu_buf[9] = pdv_len & 0xFF;
    pdu_buf[10] = pc_id; pdu_buf[11] = msg_ctrl;
    memcpy(&pdu_buf[12], data, data_len);
    send(s, (char *)pdu_buf, 6 + pdu_len, 0);
}

static bool dicom_add_str(uint8_t *buf, int *off, int max, uint16_t g, uint16_t e, const char *s) {
    uint32_t len = s ? (uint32_t)strlen(s) : 0;
    uint32_t pad = (len % 2) ? 1 : 0;
    uint32_t tot = len + pad;
    if (*off + 8 + (int)tot > max) return false;
    buf[(*off)++] = g & 0xFF; buf[(*off)++] = (g >> 8) & 0xFF;
    buf[(*off)++] = e & 0xFF; buf[(*off)++] = (e >> 8) & 0xFF;
    buf[(*off)++] = tot & 0xFF; buf[(*off)++] = (tot >> 8) & 0xFF;
    buf[(*off)++] = 0; buf[(*off)++] = 0;
    if (len > 0) { memcpy(&buf[*off], s, len); *off += len; }
    if (pad) buf[(*off)++] = ' ';
    return true;
}

static bool dicom_add_us(uint8_t *buf, int *off, int max, uint16_t g, uint16_t e, uint16_t v) {
    if (*off + 10 > max) return false;
    buf[(*off)++] = g & 0xFF; buf[(*off)++] = (g >> 8) & 0xFF;
    buf[(*off)++] = e & 0xFF; buf[(*off)++] = (e >> 8) & 0xFF;
    buf[(*off)++] = 2; buf[(*off)++] = 0; buf[(*off)++] = 0; buf[(*off)++] = 0;
    buf[(*off)++] = v & 0xFF; buf[(*off)++] = (v >> 8) & 0xFF;
    return true;
}

static bool dicom_add_ul(uint8_t *buf, int *off, int max, uint16_t g, uint16_t e, uint32_t v) {
    if (*off + 12 > max) return false;
    buf[(*off)++] = g & 0xFF; buf[(*off)++] = (g >> 8) & 0xFF;
    buf[(*off)++] = e & 0xFF; buf[(*off)++] = (e >> 8) & 0xFF;
    buf[(*off)++] = 4; buf[(*off)++] = 0; buf[(*off)++] = 0; buf[(*off)++] = 0;
    buf[(*off)++] = v & 0xFF; buf[(*off)++] = (v >> 8) & 0xFF;
    buf[(*off)++] = (v >> 16) & 0xFF; buf[(*off)++] = (v >> 24) & 0xFF;
    return true;
}

static bool dicom_seq_hdr(uint8_t *buf, int *off, int max, uint16_t g, uint16_t e) {
    if (*off + 8 > max) return false;
    buf[(*off)++] = g & 0xFF; buf[(*off)++] = (g >> 8) & 0xFF;
    buf[(*off)++] = e & 0xFF; buf[(*off)++] = (e >> 8) & 0xFF;
    buf[(*off)++] = 0xFF; buf[(*off)++] = 0xFF; buf[(*off)++] = 0xFF; buf[(*off)++] = 0xFF;
    return true;
}

static bool dicom_item_hdr(uint8_t *buf, int *off, int max) {
    if (*off + 8 > max) return false;
    buf[(*off)++] = 0xFE; buf[(*off)++] = 0xFF; buf[(*off)++] = 0x00; buf[(*off)++] = 0xE0;
    buf[(*off)++] = 0xFF; buf[(*off)++] = 0xFF; buf[(*off)++] = 0xFF; buf[(*off)++] = 0xFF;
    return true;
}

static bool dicom_item_delim(uint8_t *buf, int *off, int max) {
    if (*off + 8 > max) return false;
    buf[(*off)++] = 0xFE; buf[(*off)++] = 0xFF; buf[(*off)++] = 0x0D; buf[(*off)++] = 0xE0;
    buf[(*off)++] = 0x00; buf[(*off)++] = 0x00; buf[(*off)++] = 0x00; buf[(*off)++] = 0x00;
    return true;
}

static bool dicom_seq_delim(uint8_t *buf, int *off, int max) {
    if (*off + 8 > max) return false;
    buf[(*off)++] = 0xFE; buf[(*off)++] = 0xFF; buf[(*off)++] = 0xDD; buf[(*off)++] = 0xE0;
    buf[(*off)++] = 0x00; buf[(*off)++] = 0x00; buf[(*off)++] = 0x00; buf[(*off)++] = 0x00;
    return true;
}

static bool dicom_add_string(uint8_t *buf, int *off, int max, uint16_t g, uint16_t e, const char *s) {
    return dicom_add_str(buf, off, max, g, e, s);
}

static void dicom_get_string(const uint8_t *buf, int len, uint16_t g, uint16_t e, char *out) {
    uint8_t tg[4] = { (uint8_t)(g & 0xFF), (uint8_t)(g >> 8), (uint8_t)(e & 0xFF), (uint8_t)(e >> 8) };
    for (int i = 0; i < len - 8; i++) {
        if (buf[i] == tg[0] && buf[i + 1] == tg[1] && buf[i + 2] == tg[2] && buf[i + 3] == tg[3]) {
            int vlen = 0, offset = 0;
            bool is_vr = false;
            char v1 = (char)buf[i + 4], v2 = (char)buf[i + 5];
            if ((v1 >= 'A' && v1 <= 'Z') && (v2 >= 'A' && v2 <= 'Z')) {
                if ((v1 == 'D' && v2 == 'A') || (v1 == 'T' && v2 == 'M') || (v1 == 'C' && v2 == 'S') || (v1 == 'S' && v2 == 'H') ||
                    (v1 == 'L' && v2 == 'O') || (v1 == 'P' && v2 == 'N') || (v1 == 'U' && v2 == 'I') || (v1 == 'A' && v2 == 'E') ||
                    (v1 == 'A' && v2 == 'S') || (v1 == 'I' && v2 == 'S') || (v1 == 'D' && v2 == 'S') || (v1 == 'S' && v2 == 'T') ||
                    (v1 == 'L' && v2 == 'T') || (v1 == 'U' && v2 == 'T') || (v1 == 'O' && v2 == 'B') || (v1 == 'O' && v2 == 'W') ||
                    (v1 == 'S' && v2 == 'Q') || (v1 == 'U' && v2 == 'N') || (v1 == 'F' && v2 == 'D') || (v1 == 'F' && v2 == 'L') ||
                    (v1 == 'S' && v2 == 'L') || (v1 == 'S' && v2 == 'S') || (v1 == 'U' && v2 == 'L') || (v1 == 'U' && v2 == 'S')) {
                    is_vr = true;
                }
            }
            if (is_vr) {
                if ((v1 == 'O' && v2 == 'B') || (v1 == 'O' && v2 == 'W') || (v1 == 'S' && v2 == 'Q') || (v1 == 'U' && v2 == 'N')) {
                    vlen = buf[i + 8] | (buf[i + 9] << 8) | (buf[i + 10] << 16) | (buf[i + 11] << 24);
                    offset = 12;
                } else {
                    vlen = buf[i + 6] | (buf[i + 7] << 8);
                    offset = 8;
                }
            } else {
                vlen = buf[i + 4] | (buf[i + 5] << 8) | (buf[i + 6] << 16) | (buf[i + 7] << 24);
                offset = 8;
            }
            if (vlen > 0 && vlen < 128 && i + offset + vlen <= len) {
                memcpy(out, &buf[i + offset], vlen);
                out[vlen] = '\0';
                for (int j = vlen - 1; j >= 0 && (out[j] == ' ' || out[j] == '\0'); j--) out[j] = '\0';
                return;
            }
        }
    }
}

/* ===== INI Configuration ===== */
static void ResolveIniPath(void) {
    GetModuleFileNameA(NULL, szIniFile, MAX_PATH);
    size_t len = strlen(szIniFile);
    if (len > 4 && szIniFile[len - 4] == '.') {
        szIniFile[len - 3] = 'i';
        szIniFile[len - 2] = 'n';
        szIniFile[len - 1] = 'i';
    }
}

static void SetAETitle(const char *pSrc) {
    memset(g_aeCalled, ' ', sizeof(g_aeCalled) - 1);
    g_aeCalled[sizeof(g_aeCalled) - 1] = '\0';
    size_t nLen = strlen(pSrc);
    if (nLen > sizeof(g_aeCalled) - 1) nLen = sizeof(g_aeCalled) - 1;
    memcpy(g_aeCalled, pSrc, nLen);
}

static void LoadConfig(void) {
    GetPrivateProfileStringA(szIniServer, szIniAET, szDefAET, g_iniBuf, sizeof(g_iniBuf), szIniFile);
    SetAETitle(g_iniBuf);
    GetPrivateProfileStringA(szIniServer, szIniDelPwd, szDefDelPwd, g_delPwd, sizeof(g_delPwd), szIniFile);
    TrimInPlace(g_delPwd);
    GetPrivateProfileStringA(szIniServer, "omittedfields", "0", g_omittedFields, sizeof(g_omittedFields), szIniFile);
    TrimInPlace(g_omittedFields);
    GetPrivateProfileStringA(szIniServer, szIniStorageRoot, "dicomstore", g_storageRoot, sizeof(g_storageRoot), szIniFile);
    TrimInPlace(g_storageRoot);

    g_DicomPort = GetPrivateProfileIntA(szIniServer, szIniDicomPort, 1104, szIniFile);
    g_DebugLog  = GetPrivateProfileIntA(szIniServer, szIniDebugLog, 1, szIniFile);

    g_bRelayMode   = GetPrivateProfileIntA(szIniServer, szIniRelayMode, 0, szIniFile);
    g_bRelayDelete = GetPrivateProfileIntA(szIniServer, szIniRelayDel, 0, szIniFile);
    GetPrivateProfileStringA(szIniServer, szIniRelayIp, "127.0.0.1", g_relayIp, sizeof(g_relayIp), szIniFile);
    g_relayPort    = GetPrivateProfileIntA(szIniServer, szIniRelayPort, 104, szIniFile);
    GetPrivateProfileStringA(szIniServer, szIniRelayAet, "REMOTE_PACS", g_relayAet, sizeof(g_relayAet), szIniFile);
    GetPrivateProfileStringA(szIniServer, szIniRelayCmd, "", g_relayCmd, sizeof(g_relayCmd), szIniFile);
}

static void SaveConfig(void) {
    char buf[32];
    WritePrivateProfileStringA(szIniServer, szIniAET, g_aeCalled, szIniFile);
    WritePrivateProfileStringA(szIniServer, szIniDelPwd, g_delPwd, szIniFile);
    WritePrivateProfileStringA(szIniServer, "omittedfields", g_omittedFields, szIniFile);
    WritePrivateProfileStringA(szIniServer, szIniStorageRoot, g_storageRoot, szIniFile);

    snprintf(buf, sizeof(buf), "%d", g_DicomPort);
    WritePrivateProfileStringA(szIniServer, szIniDicomPort, buf, szIniFile);
    snprintf(buf, sizeof(buf), "%d", g_DebugLog);
    WritePrivateProfileStringA(szIniServer, szIniDebugLog, buf, szIniFile);

    snprintf(buf, sizeof(buf), "%d", g_bRelayMode);
    WritePrivateProfileStringA(szIniServer, szIniRelayMode, buf, szIniFile);
    snprintf(buf, sizeof(buf), "%d", g_bRelayDelete);
    WritePrivateProfileStringA(szIniServer, szIniRelayDel, buf, szIniFile);
    WritePrivateProfileStringA(szIniServer, szIniRelayIp, g_relayIp, szIniFile);
    snprintf(buf, sizeof(buf), "%d", g_relayPort);
    WritePrivateProfileStringA(szIniServer, szIniRelayPort, buf, szIniFile);
    WritePrivateProfileStringA(szIniServer, szIniRelayAet, g_relayAet, szIniFile);
    WritePrivateProfileStringA(szIniServer, szIniRelayCmd, g_relayCmd, szIniFile);
}

/* ===== Relay Logic ===== */
static DWORD WINAPI RelayThread(LPVOID lpParam) {
    RelayTask *task = (RelayTask *)lpParam;

    if (g_relayCmd[0] != '\0') {
        write_log("RelayMode: Executing external command...");
        char cmdLine[MAX_PATH * 2];
        snprintf(cmdLine, sizeof(cmdLine), "\"%s\" \"%s\"", g_relayCmd, task->filePath);

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            write_log("RelayMode: External command finished.");
        } else {
            write_log("RelayMode: Failed to execute external command.");
        }
    }

    write_log("RelayMode: Connecting to remote PACS...");

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        free(task);
        return 0;
    }

    struct sockaddr_in server;
    server.sin_addr.s_addr = inet_addr(g_relayIp);
    server.sin_family = AF_INET;
    server.sin_port = htons((u_short)g_relayPort);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        write_log("RelayMode: Connection to remote PACS failed");
        closesocket(sock);
        free(task);
        return 0;
    }

    uint8_t pdu[1024];
    memset(pdu, 0, sizeof(pdu));
    pdu[0] = 0x01; pdu[1] = 0x00;
    pdu[6] = 0x00; pdu[7] = 0x01;
    memset(pdu + 10, ' ', 16); memcpy(pdu + 10, g_relayAet, strlen(g_relayAet) > 16 ? 16 : strlen(g_relayAet));
    memset(pdu + 26, ' ', 16); memcpy(pdu + 26, g_aeCalled, strlen(g_aeCalled) > 16 ? 16 : strlen(g_aeCalled));

    int off = 74;
    uint8_t app_ctx[] = { 0x10, 0x00, 0x00, 0x15, '1','.','2','.','8','4','0','.','1','0','0','0','8','.','3','.','1','.','1','.','1' };
    memcpy(pdu + off, app_ctx, sizeof(app_ctx));
    off += sizeof(app_ctx);

    int pc_len_off = off + 2;
    pdu[off++] = 0x20; pdu[off++] = 0x00; pdu[off++] = 0x00; pdu[off++] = 0x00;
    pdu[off++] = 0x01; pdu[off++] = 0x00; pdu[off++] = 0x00; pdu[off++] = 0x00;
    pdu[off++] = 0x30; pdu[off++] = 0x00;
    int as_len = (int)strlen(task->sopClass);
    pdu[off++] = (as_len >> 8) & 0xFF; pdu[off++] = as_len & 0xFF;
    memcpy(pdu + off, task->sopClass, as_len);
    off += as_len;

    const char *ts_imp = "1.2.840.10008.1.2";
    int ts_len = (int)strlen(ts_imp);
    pdu[off++] = 0x40; pdu[off++] = 0x00;
    pdu[off++] = (ts_len >> 8) & 0xFF; pdu[off++] = ts_len & 0xFF;
    memcpy(pdu + off, ts_imp, ts_len);
    off += ts_len;

    int pc_len = off - pc_len_off - 2;
    pdu[pc_len_off] = (pc_len >> 8) & 0xFF;
    pdu[pc_len_off + 1] = pc_len & 0xFF;

    uint8_t user_info[] = { 0x50, 0x00, 0x00, 0x1E, 0x51, 0x00, 0x00, 0x04, 0x00, 0x00, 0x40, 0x00, 0x52, 0x00, 0x00, 0x12, '1','.','2','.','2','7','6','.','0','.','7','2','3','.','1','.','9','9' };
    memcpy(pdu + off, user_info, sizeof(user_info));
    off += sizeof(user_info);

    uint32_t len = (uint32_t)(off - 6);
    pdu[2] = (len >> 24) & 0xFF; pdu[3] = (len >> 16) & 0xFF;
    pdu[4] = (len >> 8) & 0xFF;  pdu[5] = len & 0xFF;
    send(sock, (char *)pdu, off, 0);

    char resp[4096];
    recv(sock, resp, sizeof(resp), 0);

    uint8_t cmd_buf[512];
    int cmd_len = 0;
    dicom_add_ul(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0000, 0);
    dicom_add_string(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0002, task->sopClass);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0100, 0x0001);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0110, 1);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0700, 0x0000);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0800, 0x0101);
    dicom_add_string(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x1000, task->sopInst);

    uint32_t gl = (uint32_t)(cmd_len - 12);
    cmd_buf[8] = gl & 0xFF; cmd_buf[9] = (gl >> 8) & 0xFF;
    cmd_buf[10] = (gl >> 16) & 0xFF; cmd_buf[11] = (gl >> 24) & 0xFF;

    send_pdata_tf(sock, 1, 0x03, cmd_buf, cmd_len);
    send_file_pdata_tf(sock, 1, task->filePath);
    recv(sock, resp, sizeof(resp), 0);

    uint8_t rel_rq[] = { 0x05, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 };
    send(sock, (char *)rel_rq, sizeof(rel_rq), 0);
    recv(sock, resp, sizeof(resp), 0);

    closesocket(sock);
    write_log("RelayMode: Successfully relayed image to remote PACS.");

    if (g_bRelayDelete) {
        remove(task->filePath);

        char tempCsv[MAX_PATH] = "c-store_temp.csv";
        int changed = 0;
        EnterCriticalSection(&g_cstoreLock);
        FILE *fp = fopen("c-store.csv", "r");
        FILE *outFp = fopen(tempCsv, "w");
        if (fp && outFp) {
            char cLine[1024];
            int firstLine = 1;
            while (fgets(cLine, sizeof(cLine), fp)) {
                if (firstLine) { fprintf(outFp, "%s", cLine); firstLine = 0; continue; }
                char cSop[128] = {0};
                get_csv_field(cLine, 4, cSop, sizeof(cSop));
                if (strcmp(cSop, task->sopInst) == 0) {
                    changed = 1;
                } else {
                    fprintf(outFp, "%s", cLine);
                }
            }
            fclose(fp);
            fclose(outFp);
            if (changed) {
                remove("c-store.csv");
                rename(tempCsv, "c-store.csv");
            } else {
                remove(tempCsv);
            }
        } else {
            if (fp) fclose(fp);
            if (outFp) fclose(outFp);
        }
        LeaveCriticalSection(&g_cstoreLock);
        write_log("RelayMode: Deleted local file and tracking entry.");
    }

    free(task);
    return 0;
}

/* ===== Backup & Export ===== */
static void ExportCSVsOnExit(void) {
    char backupDir[MAX_PATH];
    GetPrivateProfileStringA(szIniServer, "LocalDir", "backup_csvs", backupDir, sizeof(backupDir), szIniFile);
    CreateDirectoryA(backupDir, NULL);

    EnterCriticalSection(&g_csvLock);
    char worklistPath[MAX_PATH];
    snprintf(worklistPath, sizeof(worklistPath), "%s\\worklist_backup.csv", backupDir);
    FILE *fpW = fopen(worklistPath, "w");
    if (fpW) {
        for (int r = 0; r < g_csvRows; r++) {
            fprintf(fpW, "%s\n", g_csvData[r]);
        }
        fclose(fpW);
    }
    LeaveCriticalSection(&g_csvLock);

    EnterCriticalSection(&g_cstoreLock);
    FILE *fpC = fopen("c-store.csv", "r");
    if (fpC) {
        char cStorePath[MAX_PATH];
        snprintf(cStorePath, sizeof(cStorePath), "%s\\c-store_backup.csv", backupDir);
        FILE *fpC_out = fopen(cStorePath, "w");
        if (fpC_out) {
            char readBuf[2048];
            while (fgets(readBuf, sizeof(readBuf), fpC)) {
                fputs(readBuf, fpC_out);
            }
            fclose(fpC_out);
        }
        fclose(fpC);
    }
    LeaveCriticalSection(&g_cstoreLock);

    write_log("CSV files written to local directory on exit.");
}

static DWORD WINAPI PeriodicBackupThread(LPVOID lpParam) {
    (void)lpParam;
    while (!g_bExitApp) {
        for (int i = 0; i < (45 * 60); i++) {
            if (g_bExitApp) break;
            Sleep(1000);
        }
        if (g_bExitApp) break;

        char backupDir[MAX_PATH];
        GetPrivateProfileStringA(szIniServer, "LocalDir", "backup_csvs", backupDir, sizeof(backupDir), szIniFile);
        CreateDirectoryA(backupDir, NULL);

        EnterCriticalSection(&g_csvLock);
        char worklistPath[MAX_PATH];
        snprintf(worklistPath, sizeof(worklistPath), "%s\\worklist_backup.csv", backupDir);
        FILE *fpW = fopen(worklistPath, "w");
        if (fpW) {
            for (int r = 0; r < g_csvRows; r++) {
                fprintf(fpW, "%s\n", g_csvData[r]);
            }
            fclose(fpW);
        }
        LeaveCriticalSection(&g_csvLock);

        EnterCriticalSection(&g_cstoreLock);
        FILE *fpC = fopen("c-store.csv", "r");
        if (fpC) {
            char cStorePath[MAX_PATH];
            snprintf(cStorePath, sizeof(cStorePath), "%s\\c-store_backup.csv", backupDir);
            FILE *fpC_out = fopen(cStorePath, "w");
            if (fpC_out) {
                char readBuf[2048];
                while (fgets(readBuf, sizeof(readBuf), fpC)) {
                    fputs(readBuf, fpC_out);
                }
                fclose(fpC_out);
            }
            fclose(fpC);
        }
        LeaveCriticalSection(&g_cstoreLock);

        write_log("Periodic CSV backup to local directory completed.");
    }
    return 0;
}

/* ===== DICOM Editor Implementation ===== */
static const char *get_vr_for_tag(uint16_t group, uint16_t elem) {
    for (int i = 0; DICT[i].name != NULL; i++) {
        if (DICT[i].group == group && DICT[i].elem == elem) {
            return DICT[i].vr;
        }
    }
    return "LO";
}

static void inject_tag(FILE *fout, EditRule *rule, int is_explicit) {
    fwrite(&rule->group, 2, 1, fout);
    fwrite(&rule->elem, 2, 1, fout);

    uint32_t new_len = (uint32_t)strlen(rule->new_val);
    int needs_pad = (new_len % 2 != 0);
    if (needs_pad) new_len++;

    const char *vr = get_vr_for_tag(rule->group, rule->elem);

    if (is_explicit) {
        fwrite(vr, 1, 2, fout);
        if (memcmp(vr, "OB", 2) == 0 || memcmp(vr, "OW", 2) == 0 || memcmp(vr, "OF", 2) == 0 ||
            memcmp(vr, "SQ", 2) == 0 || memcmp(vr, "UT", 2) == 0 || memcmp(vr, "UN", 2) == 0 ||
            memcmp(vr, "UR", 2) == 0 || memcmp(vr, "UC", 2) == 0) {
            uint16_t res = 0;
            fwrite(&res, 2, 1, fout);
            fwrite(&new_len, 4, 1, fout);
        } else {
            uint16_t l16 = (uint16_t)new_len;
            fwrite(&l16, 2, 1, fout);
        }
    } else {
        fwrite(&new_len, 4, 1, fout);
    }

    fwrite(rule->new_val, 1, strlen(rule->new_val), fout);
    if (needs_pad) {
        char pad = (memcmp(vr, "UI", 2) == 0) ? '\0' : ' ';
        fwrite(&pad, 1, 1, fout);
    }
}

static void unescape_string(char *str) {
    char *read = str, *write = str;
    while (*read) {
        if (*read == '\\' && *(read + 1)) {
            read++;
            switch (*read) {
                case 'n': *write++ = '\n'; break;
                case 't': *write++ = '\t'; break;
                case 'r': *write++ = '\r'; break;
                case '\\': *write++ = '\\'; break;
                case '"': *write++ = '"'; break;
                case 'x':
                    if (*(read + 1) && *(read + 2)) {
                        char hex[3] = { *(read + 1), *(read + 2), '\0' };
                        *write++ = (char)strtol(hex, NULL, 16);
                        read += 2;
                    } else {
                        *write++ = 'x';
                    }
                    break;
                default: *write++ = *read; break;
            }
        } else {
            *write++ = *read;
        }
        read++;
    }
    *write = '\0';
}

static int parse_tag(const char *tag_str, uint16_t *group, uint16_t *elem) {
    unsigned int g = 0, e = 0;
    char clean[128];
    int j = 0;

    for (int i = 0; tag_str[i] && j < 127; i++) {
        if (tag_str[i] != ' ' && tag_str[i] != '\t') {
            clean[j++] = tag_str[i];
        }
    }
    clean[j] = '\0';

    for (int i = 0; DICT[i].name != NULL; i++) {
        if (strcmp(clean, DICT[i].name) == 0) {
            *group = DICT[i].group;
            *elem = DICT[i].elem;
            return 1;
        }
    }

    if (sscanf(tag_str, " ( %x , %x )", &g, &e) == 2) { *group = (uint16_t)g; *elem = (uint16_t)e; return 1; }
    if (sscanf(tag_str, " 0x%x , 0x%x", &g, &e) == 2) { *group = (uint16_t)g; *elem = (uint16_t)e; return 1; }
    if (sscanf(tag_str, " %x , %x", &g, &e) == 2) { *group = (uint16_t)g; *elem = (uint16_t)e; return 1; }
    if (strlen(clean) == 8 && sscanf(clean, "%04x%04x", &g, &e) == 2) {
        *group = (uint16_t)g; *elem = (uint16_t)e; return 1;
    }
    return 0;
}

static EditRule *find_rule(EditRule *rules, int num_rules, uint16_t group, uint16_t elem) {
    for (int i = 0; i < num_rules; i++) {
        if (rules[i].group == group && rules[i].elem == elem)
            return &rules[i];
    }
    return NULL;
}

static int compare_rules(const void *a, const void *b) {
    EditRule *r1 = (EditRule *)a;
    EditRule *r2 = (EditRule *)b;
    if (r1->group != r2->group) return r1->group - r2->group;
    return r1->elem - r2->elem;
}

static bool apply_dicom_edits_file(const char *input_file, const char *output_file, const char *rules_str, const char *delim) {
    EditRule rules[MAX_RULES];
    int num_rules = 0;

    char *args_copy = strdup(rules_str);
    if (!args_copy) return false;
    char *p = args_copy;
    size_t delim_len = strlen(delim);
    if (delim_len == 0) { free(args_copy); return false; }

    while (p && *p && num_rules < MAX_RULES) {
        char *next = strstr(p, delim);
        if (next) {
            *next = '\0';
            next += delim_len;
        }

        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char *tag_str = p;
            char *val_str = eq + 1;

            uint16_t g, e;
            if (parse_tag(tag_str, &g, &e)) {
                rules[num_rules].group = g;
                rules[num_rules].elem = e;
                rules[num_rules].new_val = strdup(val_str);
                rules[num_rules].applied = 0;
                if (rules[num_rules].new_val) unescape_string(rules[num_rules].new_val);
                num_rules++;
            }
        }
        p = next;
    }
    free(args_copy);

    qsort(rules, num_rules, sizeof(EditRule), compare_rules);

    FILE *fin = fopen(input_file, "rb");
    if (!fin) {
        for (int i = 0; i < num_rules; i++) free(rules[i].new_val);
        return false;
    }

    FILE *fout = fopen(output_file, "wb");
    if (!fout) {
        fclose(fin);
        for (int i = 0; i < num_rules; i++) free(rules[i].new_val);
        return false;
    }

    char header[132];
    size_t bytes_read_hdr = fread(header, 1, 132, fin);
    int has_dicm = (bytes_read_hdr == 132 && strncmp(header + 128, "DICM", 4) == 0);
    if (has_dicm) {
        fwrite(header, 1, 132, fout);
    } else {
        rewind(fin);
    }

    int in_meta = 1;
    int is_explicit = 1;
    int ts_explicit = 1;

    uint16_t group, elem;
    while (fread(&group, 2, 1, fin) == 1) {
        if (fread(&elem, 2, 1, fin) != 1) break;

        if (in_meta && group > 0x0002) {
            in_meta = 0;
            is_explicit = ts_explicit;
        }

        int is_sq_item = (group == 0xFFFE);

        if (!is_sq_item) {
            for (int i = 0; i < num_rules; i++) {
                if (!rules[i].applied) {
                    if ((rules[i].group < group) ||
                        (rules[i].group == group && rules[i].elem < elem)) {
                        int use_explicit = (rules[i].group <= 0x0002) ? ts_explicit : is_explicit;
                        inject_tag(fout, &rules[i], use_explicit);
                        rules[i].applied = 1;
                    }
                }
            }
        }

        char vr[3] = {0};
        uint32_t length = 0;
        int is_32bit_len = 0;

        if (is_explicit && !is_sq_item) {
            if (fread(vr, 1, 2, fin) != 2) break;
            if (memcmp(vr, "OB", 2) == 0 || memcmp(vr, "OW", 2) == 0 || memcmp(vr, "OF", 2) == 0 ||
                memcmp(vr, "SQ", 2) == 0 || memcmp(vr, "UT", 2) == 0 || memcmp(vr, "UN", 2) == 0 ||
                memcmp(vr, "UR", 2) == 0 || memcmp(vr, "UC", 2) == 0) {
                uint16_t reserved;
                fread(&reserved, 2, 1, fin);
                fread(&length, 4, 1, fin);
                is_32bit_len = 1;
            } else {
                uint16_t len16;
                fread(&len16, 2, 1, fin);
                length = len16;
            }
        } else {
            fread(&length, 4, 1, fin);
            is_32bit_len = 1;
        }

        EditRule *rule = find_rule(rules, num_rules, group, elem);

        fwrite(&group, 2, 1, fout);
        fwrite(&elem, 2, 1, fout);

        if (rule && !is_sq_item) {
            rule->applied = 1;
            uint32_t new_len = (uint32_t)strlen(rule->new_val);
            int needs_pad = (new_len % 2 != 0);
            if (needs_pad) new_len++;

            if (is_explicit) {
                fwrite(vr, 1, 2, fout);
                if (is_32bit_len) {
                    uint16_t res = 0;
                    fwrite(&res, 2, 1, fout);
                    fwrite(&new_len, 4, 1, fout);
                } else {
                    uint16_t l16 = (uint16_t)new_len;
                    fwrite(&l16, 2, 1, fout);
                }
            } else {
                fwrite(&new_len, 4, 1, fout);
            }

            fwrite(rule->new_val, 1, strlen(rule->new_val), fout);
            if (needs_pad) {
                char pad = (memcmp(vr, "UI", 2) == 0) ? '\0' : ' ';
                fwrite(&pad, 1, 1, fout);
            }

            if (length > 0 && length != 0xFFFFFFFF) {
                fseek(fin, (long)length, SEEK_CUR);
            }
        } else {
            if (is_explicit && !is_sq_item) {
                fwrite(vr, 1, 2, fout);
                if (is_32bit_len) {
                    uint16_t res = 0;
                    fwrite(&res, 2, 1, fout);
                    fwrite(&length, 4, 1, fout);
                } else {
                    uint16_t l16 = (uint16_t)length;
                    fwrite(&l16, 2, 1, fout);
                }
            } else {
                fwrite(&length, 4, 1, fout);
            }

            if (group == 0x0002 && elem == 0x0010 && length > 0) {
                char ts[128] = {0};
                fread(ts, 1, (length < sizeof(ts) ? length : sizeof(ts) - 1), fin);
                fwrite(ts, 1, length, fout);

                if (strncmp(ts, "1.2.840.10008.1.2.1", 19) == 0) {
                    ts_explicit = 1;
                } else if (strncmp(ts, "1.2.840.10008.1.2", 17) == 0) {
                    ts_explicit = 0;
                }
            } else if (length > 0 && length != 0xFFFFFFFF) {
                char buffer[8192];
                uint32_t remaining = length;
                while (remaining > 0) {
                    uint32_t to_read = remaining > sizeof(buffer) ? (uint32_t)sizeof(buffer) : remaining;
                    size_t bytes = fread(buffer, 1, to_read, fin);
                    if (bytes == 0) break;
                    fwrite(buffer, 1, bytes, fout);
                    remaining -= (uint32_t)bytes;
                }
            }
        }
    }

    for (int i = 0; i < num_rules; i++) {
        if (!rules[i].applied) {
            int use_explicit = (rules[i].group <= 0x0002) ? ts_explicit : is_explicit;
            inject_tag(fout, &rules[i], use_explicit);
            rules[i].applied = 1;
        }
    }

    fclose(fin);
    fclose(fout);
    for (int i = 0; i < num_rules; i++) free(rules[i].new_val);
    return true;
}

/* ===== Worklist CSV & Synchronization Functions ===== */
static int ParseCSVLineToEntry(const char *pLine, char *pEntry) {
    memset(pEntry, 0, ENTRY_SIZE);
    int fieldIdx = 0, charIdx = 0, quoteState = 0;
    char *pDest = pEntry;
    const char *si = pLine;
    while (*si && *si != '\r' && *si != '\n') {
        char charCode = *si;
        if (charCode == '~') charCode = '"';
        if (charCode == '"') { quoteState = !quoteState; si++; continue; }
        if (charCode == ',' && !quoteState) {
            *(pDest + charIdx) = '\0';
            fieldIdx++;
            if (fieldIdx >= FIELD_COUNT) break;
            pDest += FIELD_SIZE;
            charIdx = 0;
            si++;
            continue;
        }
        if (charIdx < FIELD_SIZE - 1) { *(pDest + charIdx) = charCode; charIdx++; }
        si++;
    }
    *(pDest + charIdx) = '\0';
    for (int fi = 0; fi < FIELD_COUNT; fi++) TrimInPlace(GetFieldPtr(pEntry, fi));
    return 1;
}

static int ValidateEntry(char *pEntry) {
    return (*GetFieldPtr(pEntry, 8) != '\0');
}

static void EnsureCsvInitialized(void) {
    if (g_csvInitialized) return;
    g_csvInitialized = 1;
    strncpy(g_csvData[0], szCSVHeader, LINE_SIZE - 1);
    g_csvData[0][LINE_SIZE - 1] = '\0';
    char entry[ENTRY_SIZE];
    ParseCSVLineToEntry(szFakePatient, entry);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char today[32];
    snprintf(today, sizeof(today), "%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
    strncpy(GetFieldPtr(entry, 12), today, FIELD_SIZE - 1);
    char fakePat[LINE_SIZE];
    BuildCSVLineFromEntry(entry, fakePat);
    strncpy(g_csvData[1], fakePat, LINE_SIZE - 1);
    g_csvData[1][LINE_SIZE - 1] = '\0';
    g_csvRows = 2;
}

static int AccessionEquals(const char *pLine, const char *pAccession) {
    char tempEntry[ENTRY_SIZE];
    ParseCSVLineToEntry(pLine, tempEntry);
    return strcmp(GetFieldPtr(tempEntry, 8), pAccession) == 0;
}

static int UpdateCsvByPatient(char *pEntry, const char *pLineOut) {
    EnsureCsvInitialized();
    char *pAcc = GetFieldPtr(pEntry, 8);
    EnterCriticalSection(&g_csvLock);
    int found = 0;
    for (int i = 1; i < g_csvRows; i++) {
        if (AccessionEquals(g_csvData[i], pAcc)) {
            strncpy(g_csvData[i], pLineOut, LINE_SIZE - 1);
            g_csvData[i][LINE_SIZE - 1] = '\0';
            found = 1;
            break;
        }
    }
    if (!found && g_csvRows < MAX_CSV_ROWS) {
        strncpy(g_csvData[g_csvRows], pLineOut, LINE_SIZE - 1);
        g_csvData[g_csvRows][LINE_SIZE - 1] = '\0';
        g_csvRows++;
    }
    LeaveCriticalSection(&g_csvLock);
    return found;
}

static void GenerateStudyUIDIfNeeded(char *pEntry) {
    char *pUID = GetFieldPtr(pEntry, 26);
    if (*pUID == '\0') {
        DWORD ticks = GetTickCount();
        int r = rand() % 10000;
        snprintf(pUID, FIELD_SIZE, "1.2.840.113619.2.1.%u.%d", (unsigned int)ticks, r);
    }
}

static void UpdateWorklistFromDicomTags(const char *acc, const uint8_t *dbuf, int dlen) {
    if (!acc || !acc[0]) return;
    char upName[128] = {0}, upId[128] = {0}, upMod[128] = {0}, upStudyDesc[128] = {0};
    char upDob[128] = {0}, upSex[128] = {0}, upReqPhys[128] = {0}, upReqProcDesc[128] = {0};
    char upReason[128] = {0}, upProcId[128] = {0}, upStationAe[128] = {0}, upStudyDate[128] = {0};
    char upStudyTime[128] = {0}, upPerfPhys[128] = {0}, upSpsDesc[128] = {0}, upSpsId[128] = {0};
    char upStationName[128] = {0}, upRefPhys[128] = {0}, upStudyUID[128] = {0};

    dicom_get_string(dbuf, dlen, 0x0010, 0x0010, upName);
    dicom_get_string(dbuf, dlen, 0x0010, 0x0020, upId);
    dicom_get_string(dbuf, dlen, 0x0008, 0x0060, upMod);
    dicom_get_string(dbuf, dlen, 0x0008, 0x1030, upStudyDesc);
    dicom_get_string(dbuf, dlen, 0x0010, 0x0030, upDob);
    dicom_get_string(dbuf, dlen, 0x0010, 0x0040, upSex);
    dicom_get_string(dbuf, dlen, 0x0032, 0x1032, upReqPhys);
    dicom_get_string(dbuf, dlen, 0x0032, 0x1060, upReqProcDesc);
    dicom_get_string(dbuf, dlen, 0x0040, 0x1002, upReason);
    dicom_get_string(dbuf, dlen, 0x0040, 0x1001, upProcId);
    dicom_get_string(dbuf, dlen, 0x0040, 0x0001, upStationAe);
    dicom_get_string(dbuf, dlen, 0x0008, 0x0020, upStudyDate);
    dicom_get_string(dbuf, dlen, 0x0008, 0x0030, upStudyTime);
    dicom_get_string(dbuf, dlen, 0x0008, 0x1050, upPerfPhys);
    dicom_get_string(dbuf, dlen, 0x0040, 0x0007, upSpsDesc);
    dicom_get_string(dbuf, dlen, 0x0040, 0x0009, upSpsId);
    dicom_get_string(dbuf, dlen, 0x0008, 0x1010, upStationName);
    dicom_get_string(dbuf, dlen, 0x0008, 0x0090, upRefPhys);
    dicom_get_string(dbuf, dlen, 0x0020, 0x000D, upStudyUID);

    TrimInPlace(upName); TrimInPlace(upId); TrimInPlace(upMod); TrimInPlace(upStudyDesc);
    TrimInPlace(upDob); TrimInPlace(upSex); TrimInPlace(upReqPhys); TrimInPlace(upReqProcDesc);
    TrimInPlace(upReason); TrimInPlace(upProcId); TrimInPlace(upStationAe); TrimInPlace(upStudyDate);
    TrimInPlace(upStudyTime); TrimInPlace(upPerfPhys); TrimInPlace(upSpsDesc); TrimInPlace(upSpsId);
    TrimInPlace(upStationName); TrimInPlace(upRefPhys); TrimInPlace(upStudyUID);

    EnterCriticalSection(&g_csvLock);
    for (int r = 1; r < g_csvRows; r++) {
        if (AccessionEquals(g_csvData[r], acc)) {
            char entry[ENTRY_SIZE];
            ParseCSVLineToEntry(g_csvData[r], entry);
            int changed = 0;

            if (upName[0] && !GetFieldPtr(entry, 0)[0]) { strncpy(GetFieldPtr(entry, 0), upName, FIELD_SIZE - 1); changed = 1; }
            if (upId[0] && !GetFieldPtr(entry, 1)[0]) { strncpy(GetFieldPtr(entry, 1), upId, FIELD_SIZE - 1); changed = 1; }
            if (upDob[0] && !GetFieldPtr(entry, 2)[0]) { strncpy(GetFieldPtr(entry, 2), upDob, FIELD_SIZE - 1); changed = 1; }
            if (upSex[0] && !GetFieldPtr(entry, 3)[0]) { strncpy(GetFieldPtr(entry, 3), upSex, FIELD_SIZE - 1); changed = 1; }
            if (upReqPhys[0] && !GetFieldPtr(entry, 4)[0]) { strncpy(GetFieldPtr(entry, 4), upReqPhys, FIELD_SIZE - 1); changed = 1; }
            if (upReqProcDesc[0] && !GetFieldPtr(entry, 6)[0]) { strncpy(GetFieldPtr(entry, 6), upReqProcDesc, FIELD_SIZE - 1); changed = 1; }
            if (upReason[0] && !GetFieldPtr(entry, 7)[0]) { strncpy(GetFieldPtr(entry, 7), upReason, FIELD_SIZE - 1); changed = 1; }
            if (upProcId[0] && !GetFieldPtr(entry, 9)[0]) { strncpy(GetFieldPtr(entry, 9), upProcId, FIELD_SIZE - 1); changed = 1; }
            if (upStationAe[0] && !GetFieldPtr(entry, 11)[0]) { strncpy(GetFieldPtr(entry, 11), upStationAe, FIELD_SIZE - 1); changed = 1; }
            if (upStudyDate[0] && !GetFieldPtr(entry, 12)[0]) { strncpy(GetFieldPtr(entry, 12), upStudyDate, FIELD_SIZE - 1); changed = 1; }
            if (upStudyTime[0] && !GetFieldPtr(entry, 13)[0]) { strncpy(GetFieldPtr(entry, 13), upStudyTime, FIELD_SIZE - 1); changed = 1; }
            if (upPerfPhys[0] && !GetFieldPtr(entry, 14)[0]) { strncpy(GetFieldPtr(entry, 14), upPerfPhys, FIELD_SIZE - 1); changed = 1; }
            if (upSpsDesc[0] && !GetFieldPtr(entry, 15)[0]) { strncpy(GetFieldPtr(entry, 15), upSpsDesc, FIELD_SIZE - 1); changed = 1; }
            if (upSpsId[0] && !GetFieldPtr(entry, 16)[0]) { strncpy(GetFieldPtr(entry, 16), upSpsId, FIELD_SIZE - 1); changed = 1; }
            if (upStationName[0] && !GetFieldPtr(entry, 17)[0]) { strncpy(GetFieldPtr(entry, 17), upStationName, FIELD_SIZE - 1); changed = 1; }
            if (upMod[0] && !GetFieldPtr(entry, 20)[0]) { strncpy(GetFieldPtr(entry, 20), upMod, FIELD_SIZE - 1); changed = 1; }
            if (upRefPhys[0] && !GetFieldPtr(entry, 21)[0]) { strncpy(GetFieldPtr(entry, 21), upRefPhys, FIELD_SIZE - 1); changed = 1; }
            if (upStudyDesc[0] && !GetFieldPtr(entry, 22)[0]) { strncpy(GetFieldPtr(entry, 22), upStudyDesc, FIELD_SIZE - 1); changed = 1; }
            if (upStudyUID[0] && !GetFieldPtr(entry, 26)[0]) { strncpy(GetFieldPtr(entry, 26), upStudyUID, FIELD_SIZE - 1); changed = 1; }

            if (changed) {
                char lineOut[LINE_SIZE];
                BuildCSVLineFromEntry(entry, lineOut);
                strncpy(g_csvData[r], lineOut, LINE_SIZE - 1);
            }
            break;
        }
    }
    LeaveCriticalSection(&g_csvLock);
}

static void UpdateAccessionImages(const char *oldAcc, char *newEntry) {
    if (!oldAcc || !oldAcc[0]) return;
    char newAcc[128], newName[128], newId[128], newDob[128];
    strncpy(newName, GetFieldPtr(newEntry, 0), 127); newName[127] = '\0';
    strncpy(newId, GetFieldPtr(newEntry, 1), 127); newId[127] = '\0';
    strncpy(newDob, GetFieldPtr(newEntry, 2), 127); newDob[127] = '\0';
    strncpy(newAcc, GetFieldPtr(newEntry, 8), 127); newAcc[127] = '\0';

    int changed = 0;
    char tempCsv[MAX_PATH] = "c-store_temp.csv";

    EnterCriticalSection(&g_cstoreLock);
    FILE *fp = fopen("c-store.csv", "r");
    FILE *outFp = fopen(tempCsv, "w");
    if (fp && outFp) {
        char cLine[1024];
        int firstLine = 1;
        while (fgets(cLine, sizeof(cLine), fp)) {
            if (firstLine) {
                fprintf(outFp, "%s", cLine);
                firstLine = 0;
                continue;
            }
            char cAcc[128] = {0};
            get_csv_field(cLine, 0, cAcc, sizeof(cAcc));

            if (strcmp(cAcc, oldAcc) == 0) {
                char cSop[128] = {0}, cFolder[128] = {0}, cFile[128] = {0}, cMod[128] = {0}, cSize[128] = {0}, cTime[128] = {0};
                get_csv_field(cLine, 4, cSop, sizeof(cSop));
                get_csv_field(cLine, 5, cFolder, sizeof(cFolder));
                get_csv_field(cLine, 6, cFile, sizeof(cFile));
                get_csv_field(cLine, 7, cMod, sizeof(cMod));
                get_csv_field(cLine, 8, cSize, sizeof(cSize));
                get_csv_field(cLine, 9, cTime, sizeof(cTime));

                fprintf(outFp, "\"%s\",\"%s\",\"%s\",\"\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\n",
                        newAcc, newName, newId, cSop, cFolder, cFile, cMod, cSize, cTime);
                changed = 1;

                char path[MAX_PATH], tempPath[MAX_PATH];
                snprintf(path, sizeof(path), "%s\\%s\\%s", g_storageRoot, cFolder, cFile);
                snprintf(tempPath, sizeof(tempPath), "%s\\%s\\temp_%s", g_storageRoot, cFolder, cFile);

                char rules[512];
                snprintf(rules, sizeof(rules), "0x0008,0x0050=%s`0x0010,0x0010=%s`0x0010,0x0020=%s`0x0010,0x0030=%s",
                         newAcc, newName, newId, newDob);
                if (apply_dicom_edits_file(path, tempPath, rules, "`")) {
                    remove(path);
                    rename(tempPath, path);
                } else {
                    remove(tempPath);
                }
            } else {
                fprintf(outFp, "%s", cLine);
            }
        }
        fclose(fp);
        fclose(outFp);

        if (changed) {
            remove("c-store.csv");
            rename(tempCsv, "c-store.csv");
        } else {
            remove(tempCsv);
        }
    } else {
        if (fp) fclose(fp);
        if (outFp) fclose(outFp);
    }
    LeaveCriticalSection(&g_cstoreLock);

    if (strcmp(oldAcc, newAcc) != 0) {
        EnterCriticalSection(&g_csvLock);
        for (int i = 1; i < g_csvRows; i++) {
            if (AccessionEquals(g_csvData[i], oldAcc)) {
                for (int j = i; j < g_csvRows - 1; j++) {
                    strncpy(g_csvData[j], g_csvData[j + 1], LINE_SIZE - 1);
                }
                g_csvRows--;
                break;
            }
        }
        LeaveCriticalSection(&g_csvLock);
    }
}

/* ===== Procedure Codes Loading ===== */
static void LoadProcedureCodes(void) {
    EnterCriticalSection(&g_procCodesLock);
    FILE *fp = fopen("procedurecodes.csv", "r");
    if (!fp) {
        g_procCodesLoaded = 1;
        LeaveCriticalSection(&g_procCodesLock);
        return;
    }
    char line[LINE_SIZE];
    g_procCodeCount = 0;
    int firstLine = 1;
    while (fgets(line, sizeof(line), fp) && g_procCodeCount < MAX_PROC_CODES) {
        if (firstLine) { firstLine = 0; continue; }
        char *newline = strchr(line, '\n'); if (newline) *newline = '\0';
        newline = strchr(line, '\r'); if (newline) *newline = '\0';
        char *fields[4] = {
            g_procCodes[g_procCodeCount].id,
            g_procCodes[g_procCodeCount].procCode,
            g_procCodes[g_procCodeCount].modality,
            g_procCodes[g_procCodeCount].name
        };
        char *p = line;
        for (int i = 0; i < 4; i++) {
            char *comma = strchr(p, ',');
            if (comma) {
                *comma = '\0';
                strncpy(fields[i], p, 127); fields[i][127] = '\0';
                TrimInPlace(fields[i]);
                p = comma + 1;
            } else {
                strncpy(fields[i], p, 127); fields[i][127] = '\0';
                TrimInPlace(fields[i]);
                break;
            }
        }
        g_procCodeCount++;
    }
    fclose(fp);
    g_procCodesLoaded = 1;
    LeaveCriticalSection(&g_procCodesLock);
    write_log("Procedure codes loaded");
}

/* ===== Patient Directory CSV Helpers ===== */
static long get_line_end(const char *data, long start, long len) {
    long pos = start;
    while (pos < len && data[pos] != '\r' && data[pos] != '\n') pos++;
    return pos;
}

static void skip_newline(const char *data, long *pos, long len) {
    while (*pos < len && (data[*pos] == '\r' || data[*pos] == '\n')) (*pos)++;
}

static Encoding detect_encoding(const unsigned char *buf, long len) {
    if (len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) return ENCODING_UTF8_BOM;
    if (len >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) return ENCODING_UTF16_LE;
    if (len >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) return ENCODING_UTF16_BE;
    for (long i = 0; i < (len > 1024 ? 1024 : len); i++) {
        if (buf[i] > 127) return ENCODING_UTF8;
    }
    return ENCODING_ASCII;
}

static int utf16le_to_utf8(const wchar_t *input, char *output, int max_output) {
    int out_pos = 0, in_idx = 0;
    while (input[in_idx] != L'\0' && out_pos < max_output - 4) {
        wchar_t ch = input[in_idx++];
        if (ch < 0x80) output[out_pos++] = (char)ch;
        else if (ch < 0x800) {
            output[out_pos++] = (char)(0xC0 | (ch >> 6));
            output[out_pos++] = (char)(0x80 | (ch & 0x3F));
        } else {
            output[out_pos++] = (char)(0xE0 | (ch >> 12));
            output[out_pos++] = (char)(0x80 | ((ch >> 6) & 0x3F));
            output[out_pos++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    output[out_pos] = '\0';
    return out_pos;
}

static int utf16be_to_utf8(const unsigned char *input, char *output, int max_output) {
    int out_pos = 0, in_idx = 0;
    while (in_idx + 1 < max_output * 2) {
        unsigned char hi = input[in_idx++];
        unsigned char lo = input[in_idx++];
        wchar_t ch = (wchar_t)((hi << 8) | lo);
        if (ch == 0) break;
        if (ch < 0x80) output[out_pos++] = (char)ch;
        else if (ch < 0x800) {
            output[out_pos++] = (char)(0xC0 | (ch >> 6));
            output[out_pos++] = (char)(0x80 | (ch & 0x3F));
        } else {
            output[out_pos++] = (char)(0xE0 | (ch >> 12));
            output[out_pos++] = (char)(0x80 | ((ch >> 6) & 0x3F));
            output[out_pos++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    output[out_pos] = '\0';
    return out_pos;
}

static char *load_and_convert_csv(const char *filename, long *data_len) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);
    if (file_size >= (long)sizeof(g_raw_csv_buf) - 4) { fclose(fp); return NULL; }
    size_t bytes_read = fread(g_raw_csv_buf, 1, file_size, fp);
    fclose(fp);
    if (bytes_read == 0) return NULL;
    g_raw_csv_buf[bytes_read] = '\0';

    Encoding enc = detect_encoding(g_raw_csv_buf, (long)bytes_read);
    char *converted = g_converted_csv_buf;
    switch (enc) {
        case ENCODING_UTF8_BOM:
            memcpy(converted, g_raw_csv_buf, bytes_read + 1);
            *data_len = (long)bytes_read - 3;
            memmove(converted, converted + 3, bytes_read - 2);
            break;
        case ENCODING_UTF8:
            memcpy(converted, g_raw_csv_buf, bytes_read + 1);
            *data_len = (long)bytes_read;
            break;
        case ENCODING_UTF16_LE:
            *data_len = utf16le_to_utf8((wchar_t *)(g_raw_csv_buf + 2), converted, (int)bytes_read);
            break;
        case ENCODING_UTF16_BE:
            *data_len = utf16be_to_utf8(g_raw_csv_buf + 2, converted, (int)bytes_read);
            break;
        default:
            memcpy(converted, g_raw_csv_buf, bytes_read + 1);
            *data_len = (long)bytes_read;
            break;
    }
    return converted;
}

static int init_patients_csv(void) {
    if (!patients_csv_data) {
        patients_csv_data = load_and_convert_csv("patients.csv", &patients_csv_data_len);
        if (!patients_csv_data || patients_csv_data_len == 0) return -1;
        long h_end = get_line_end(patients_csv_data, 0, patients_csv_data_len);
        char header[8192];
        long h_len = (h_end > 8191) ? 8191 : h_end;
        memcpy(header, patients_csv_data, h_len);
        header[h_len] = '\0';
        TrimInPlace(header);
        int col_idx = 0;
        char field_buf[MAX_FIELD_LEN];
        while (get_csv_field(header, col_idx, field_buf, sizeof(field_buf))) {
            if (str_icontains(field_buf, "Birth") || str_icontains(field_buf, "DOB")) pts_birthdate_col_idx = col_idx;
            else if (str_icontains(field_buf, "Sex") || str_icontains(field_buf, "Gender")) pts_sex_col_idx = col_idx;
            else if (str_icontains(field_buf, "ID") || str_icontains(field_buf, "PatID")) pts_id_col_idx = col_idx;
            else if ((str_icontains(field_buf, "Name") || str_icontains(field_buf, "Patient")) && pts_name_col_idx == -1) {
                pts_name_col_idx = col_idx;
            }
            col_idx++;
        }
        if (pts_name_col_idx < 0) {
            pts_name_col_idx = 0;
            pts_id_col_idx = 1;
            pts_birthdate_col_idx = 2;
            pts_sex_col_idx = 3;
        }
    }
    return 0;
}

static int get_patient_names(const char *filter, char *output, int max_output, int max_count) {
    if (init_patients_csv() != 0) { snprintf(output, max_output, "[]"); return -1; }
    output[0] = '\0';
    int count = 0;
    long data_start = get_line_end(patients_csv_data, 0, patients_csv_data_len);
    skip_newline(patients_csv_data, &data_start, patients_csv_data_len);
    snprintf(output, max_output, "[");
    long line_start = data_start;
    while (line_start < patients_csv_data_len && (max_count == 0 || count < max_count)) {
        long line_end = get_line_end(patients_csv_data, line_start, patients_csv_data_len);
        if (line_end <= line_start) {
            line_start = line_end + 1;
            skip_newline(patients_csv_data, &line_start, patients_csv_data_len);
            continue;
        }
        long len = line_end - line_start;
        if (len > 8191) len = 8191;
        char line[8192];
        memcpy(line, &patients_csv_data[line_start], len);
        line[len] = '\0';
        TrimInPlace(line);
        char name_buf[MAX_FIELD_LEN];
        if (get_csv_field(line, pts_name_col_idx, name_buf, sizeof(name_buf))) {
            if (filter && strlen(filter) > 0) {
                if (!str_icontains(name_buf, filter)) {
                    line_start = line_end;
                    skip_newline(patients_csv_data, &line_start, patients_csv_data_len);
                    continue;
                }
            }
            if (count > 0) strncat(output, ",", max_output - strlen(output) - 1);
            char escaped_name[MAX_FIELD_LEN * 2];
            int ei = 0;
            for (int ni = 0; name_buf[ni] && ei < MAX_FIELD_LEN * 2 - 1; ni++) {
                if (name_buf[ni] == '"' || name_buf[ni] == '\\') escaped_name[ei++] = '\\';
                escaped_name[ei++] = name_buf[ni];
            }
            escaped_name[ei] = '\0';
            char formatted_name[MAX_FIELD_LEN * 2 + 4];
            snprintf(formatted_name, sizeof(formatted_name), "\"%s\"", escaped_name);
            strncat(output, formatted_name, max_output - strlen(output) - 1);
            count++;
        }
        line_start = line_end;
        skip_newline(patients_csv_data, &line_start, patients_csv_data_len);
    }
    strncat(output, "]", max_output - strlen(output) - 1);
    return count;
}

static int get_patient_details(const char *patient_name, char *output, int max_output) {
    if (init_patients_csv() != 0) return -1;
    long data_start = get_line_end(patients_csv_data, 0, patients_csv_data_len);
    skip_newline(patients_csv_data, &data_start, patients_csv_data_len);
    long line_start = data_start;
    while (line_start < patients_csv_data_len) {
        long line_end = get_line_end(patients_csv_data, line_start, patients_csv_data_len);
        if (line_end <= line_start) {
            line_start = line_end + 1;
            skip_newline(patients_csv_data, &line_start, patients_csv_data_len);
            continue;
        }
        long len = line_end - line_start;
        if (len > 8191) len = 8191;
        char line[8192];
        memcpy(line, &patients_csv_data[line_start], len);
        line[len] = '\0';
        TrimInPlace(line);
        char name_buf[MAX_FIELD_LEN];
        if (get_csv_field(line, pts_name_col_idx, name_buf, sizeof(name_buf))) {
            if (StrIEquals(name_buf, patient_name)) {
                char id_buf[MAX_FIELD_LEN] = {0}, birthdate_buf[MAX_FIELD_LEN] = {0}, sex_buf[MAX_FIELD_LEN] = {0};
                if (pts_id_col_idx >= 0) get_csv_field(line, pts_id_col_idx, id_buf, sizeof(id_buf));
                if (pts_birthdate_col_idx >= 0) get_csv_field(line, pts_birthdate_col_idx, birthdate_buf, sizeof(birthdate_buf));
                if (pts_sex_col_idx >= 0) get_csv_field(line, pts_sex_col_idx, sex_buf, sizeof(sex_buf));
                snprintf(output, max_output, "{\"patient_id\":\"%s\",\"name\":\"%s\",\"birthdate\":\"%s\",\"sex\":\"%s\"}",
                         id_buf, name_buf, birthdate_buf, sex_buf);
                return 0;
            }
        }
        line_start = line_end;
        skip_newline(patients_csv_data, &line_start, patients_csv_data_len);
    }
    snprintf(output, max_output, "{\"error\": \"Patient not found\"}");
    return -1;
}

/* ===== DICOM Storage Functions ===== */
static void AllocateImageFile(char *outFolder, char *outFilename, char *outPath) {
    EnterCriticalSection(&g_cstoreLock);
    if (g_storageIndex >= 10000000) {
        int n = atoi(g_storageFolder + 1);
        snprintf(g_storageFolder, sizeof(g_storageFolder), "D%07d", n + 1);
        g_storageIndex = 0;
        char dirBuf[MAX_PATH];
        snprintf(dirBuf, sizeof(dirBuf), "%s\\%s", g_storageRoot, g_storageFolder);
        CreateDirectoryA(dirBuf, NULL);
    }
    strcpy(outFolder, g_storageFolder);
    snprintf(outFilename, 128, "I%07d.dcm", g_storageIndex);
    snprintf(outPath, MAX_PATH, "%s\\%s\\%s", g_storageRoot, outFolder, outFilename);
    g_storageIndex++;
    LeaveCriticalSection(&g_cstoreLock);
}

static void RecordStoredImage(const char *acc, const char *pName, const char *pId, const char *sop,
                              const char *folder, const char *filename, const char *modality, long size) {
    EnterCriticalSection(&g_cstoreLock);
    FILE *fp = fopen("c-store.csv", "a");
    if (fp) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(fp, "\"%s\",\"%s\",\"%s\",\"\",\"%s\",\"%s\",\"%s\",\"%s\",\"%ld\",\"%04d-%02d-%02d %02d:%02d:%02d\"\n",
                acc ? acc : "", pName ? pName : "", pId ? pId : "", sop ? sop : "",
                folder, filename, modality ? modality : "OT", size,
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fclose(fp);
    }
    LeaveCriticalSection(&g_cstoreLock);
}

static void LoadCStoreTracking(void) {
    EnterCriticalSection(&g_cstoreLock);
    FILE *fp = fopen("c-store.csv", "r");
    if (fp) {
        fclose(fp);
    } else {
        fp = fopen("c-store.csv", "w");
        if (fp) {
            fprintf(fp, "\"Accession\",\"PatientName\",\"PatientID\",\"StudyUID\",\"SOPInstanceUID\",\"Folder\",\"Filename\",\"Modality\",\"FileSize\",\"Timestamp\"\n");
            fclose(fp);
        }
    }
    LeaveCriticalSection(&g_cstoreLock);
}

/* ===== MWL Parsing ===== */
static void trim_mwl(char *str) {
    if (!str) return;
    char *p = str;
    while (*p == ' ' || *p == '"') p++;
    if (p != str) memmove(str, p, strlen(p) + 1);
    int len = (int)strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '"' || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
}

static void parse_csv_line_mwl(char *line, MWLEntry *e) {
    memset(e, 0, sizeof(MWLEntry));
    char *fields[] = {
        e->patientName, e->patientID, e->dob, e->sex, e->reqPhys, e->reqSvc, e->procDesc, e->procReason,
        e->accNum, e->procID, e->procPri, e->aeTitle, e->spsDate, e->spsTime, e->perfPhys, e->spsDesc,
        e->spsID, e->station, e->loc, e->status, e->modality, e->refPhys, e->studyDesc, e->protoCode,
        e->protoScheme, e->protoMeaning, e->studyUID
    };
    char *start = line;
    for (int i = 0; i < 27; i++) {
        char *end = strchr(start, ',');
        if (end) {
            *end = '\0';
            strncpy(fields[i], start, 127); fields[i][127] = '\0';
            trim_mwl(fields[i]);
            start = end + 1;
        } else {
            strncpy(fields[i], start, 127); fields[i][127] = '\0';
            trim_mwl(fields[i]);
            break;
        }
    }
}

/* ===== DICOM Protocol Handlers ===== */
static void handle_association_rq(SOCKET clientSocket, char *buffer, int bytesRead) {
    write_log("Parsing A-ASSOCIATE-RQ...");
    uint8_t ac_pdu[2048] = {0};
    ac_pdu[0] = 0x02; ac_pdu[1] = 0x00; ac_pdu[6] = 0x00; ac_pdu[7] = 0x01;
    if (bytesRead < 42) return;
    memcpy(&ac_pdu[10], &buffer[10], 16);
    memcpy(&ac_pdu[26], &buffer[26], 16);

    int offset = 74;
    uint8_t app_ctx[] = { 0x10, 0x00, 0x00, 0x15, '1','.','2','.','8','4','0','.','1','0','0','0','8','.','3','.','1','.','1','.','1' };
    memcpy(&ac_pdu[offset], app_ctx, sizeof(app_ctx));
    offset += sizeof(app_ctx);

    int i = 74;
    while (i < bytesRead - 4) {
        uint8_t item_type = (uint8_t)buffer[i];
        uint16_t item_len = ((uint8_t)buffer[i + 2] << 8) | (uint8_t)buffer[i + 3];
        if (item_type == 0x20 && i + 4 + item_len <= bytesRead) {
            write_log("Accepting PC ID");
            uint8_t pc_id = (uint8_t)buffer[i + 4];
            ac_pdu[offset++] = 0x21; ac_pdu[offset++] = 0x00; ac_pdu[offset++] = 0x00; ac_pdu[offset++] = 0x19;
            ac_pdu[offset++] = pc_id; ac_pdu[offset++] = 0x00; ac_pdu[offset++] = 0x00; ac_pdu[offset++] = 0x00;
            uint8_t ts[] = { 0x40, 0x00, 0x00, 0x11, '1','.','2','.','8','4','0','.','1','0','0','0','8','.','1','.','2' };
            memcpy(&ac_pdu[offset], ts, sizeof(ts));
            offset += sizeof(ts);
        }
        i += 4 + item_len;
    }

    uint8_t user_info[] = { 0x50, 0x00, 0x00, 0x1E, 0x51, 0x00, 0x00, 0x04, 0x00, 0x00, 0x40, 0x00, 0x52, 0x00, 0x00, 0x12, '1','.','2','.','2','7','6','.','0','.','7','2','3','.','1','.','9','9' };
    memcpy(&ac_pdu[offset], user_info, sizeof(user_info));
    offset += sizeof(user_info);

    uint32_t pdu_len = (uint32_t)(offset - 6);
    ac_pdu[2] = (pdu_len >> 24) & 0xFF; ac_pdu[3] = (pdu_len >> 16) & 0xFF;
    ac_pdu[4] = (pdu_len >> 8) & 0xFF;  ac_pdu[5] = pdu_len & 0xFF;
    send(clientSocket, (char *)ac_pdu, offset, 0);
}

static void handle_c_echo_rq(SOCKET clientSocket, uint8_t pc_id, uint16_t msg_id) {
    uint8_t cmd_buf[512];
    int cmd_len = 0;
    dicom_add_ul(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0000, 0);
    dicom_add_string(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0002, "1.2.840.10008.1.1");
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0100, 0x8030);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0120, msg_id);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0800, 0x0101);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0900, 0x0000);
    uint32_t gl = (uint32_t)(cmd_len - 12);
    cmd_buf[8] = gl & 0xFF; cmd_buf[9] = (gl >> 8) & 0xFF;
    cmd_buf[10] = (gl >> 16) & 0xFF; cmd_buf[11] = (gl >> 24) & 0xFF;
    send_pdata_tf(clientSocket, pc_id, 0x03, cmd_buf, cmd_len);
}

static int is_field_omitted(uint16_t g, uint16_t e) {
    if (!g_omittedFields[0] || strcmp(g_omittedFields, "0") == 0) return 0;
    char tagStr[16];
    snprintf(tagStr, sizeof(tagStr), "%04X%04X", g, e);
    char buf[256];
    strncpy(buf, g_omittedFields, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *token = strtok(buf, ", ");
    while (token) {
        TrimInPlace(token);
        if (StrIEquals(token, tagStr)) return 1;
        token = strtok(NULL, ", ");
    }
    return 0;
}

static void handle_c_find_rq(SOCKET clientSocket, uint8_t pc_id, uint16_t msg_id, const uint8_t *req_buf, int req_len) {
    char qDate[128] = {0}, qTime[128] = {0}, qAET[128] = {0}, qMod[128] = {0};
    dicom_get_string(req_buf, req_len, 0x0008, 0x0060, qMod);
    dicom_get_string(req_buf, req_len, 0x0040, 0x0001, qAET);
    dicom_get_string(req_buf, req_len, 0x0040, 0x0002, qDate);
    dicom_get_string(req_buf, req_len, 0x0040, 0x0003, qTime);

    if (qDate[0] == '\0') dicom_get_string(req_buf, req_len, 0x0008, 0x0020, qDate);
    if (qTime[0] == '\0') dicom_get_string(req_buf, req_len, 0x0008, 0x0030, qTime);

    EnsureCsvInitialized();
    EnterCriticalSection(&g_csvLock);
    for (int row = 1; row < g_csvRows; row++) {
        MWLEntry e;
        char tempLine[LINE_SIZE];
        strncpy(tempLine, g_csvData[row], LINE_SIZE - 1);
        tempLine[LINE_SIZE - 1] = '\0';
        parse_csv_line_mwl(tempLine, &e);

        int statCode = atoi(e.status);
        if (statCode < 2 || statCode > 4) continue;
        if (qMod[0] != '\0' && strcmp(qMod, e.modality) != 0) continue;
        if (qAET[0] != '\0' && strcmp(qAET, e.aeTitle) != 0) continue;
        if (qDate[0] != '\0') {
            char *dash = strchr(qDate, '-');
            if (dash) {
                char d1[32] = {0}, d2[32] = {0};
                strncpy(d1, qDate, dash - qDate);
                strcpy(d2, dash + 1);
                if (d1[0] != '\0' && strcmp(e.spsDate, d1) < 0) continue;
                if (d2[0] != '\0' && strcmp(e.spsDate, d2) > 0) continue;
            } else {
                if (strcmp(e.spsDate, qDate) != 0) continue;
            }
        }
        if (qTime[0] != '\0') {
            char *dash = strchr(qTime, '-');
            if (dash) {
                char t1[32] = {0}, t2[32] = {0};
                strncpy(t1, qTime, dash - qTime);
                strcpy(t2, dash + 1);
                if (t1[0] != '\0' && strcmp(e.spsTime, t1) < 0) continue;
                if (t2[0] != '\0' && strcmp(e.spsTime, t2) > 0) continue;
            } else {
                if (strncmp(e.spsTime, qTime, strlen(qTime)) != 0) continue;
            }
        }

        /* Inline VR Sanitizers (strict YYYYMMDD and HHMMSS output) */
        char safeDate[16] = {0}, safeTime[16] = {0}, safeDob[16] = {0};
        int i, j;

        for (i = 0, j = 0; e.spsDate[i] && j < 15; i++) {
            if (isdigit((unsigned char)e.spsDate[i])) safeDate[j++] = e.spsDate[i];
        }
        safeDate[j] = '\0';

        for (i = 0, j = 0; e.spsTime[i] && j < 15; i++) {
            if (isdigit((unsigned char)e.spsTime[i])) safeTime[j++] = e.spsTime[i];
        }
        safeTime[j] = '\0';
        if (strlen(safeTime) == 2) strcat(safeTime, "0000");
        else if (strlen(safeTime) == 4) strcat(safeTime, "00");

        for (i = 0, j = 0; e.dob[i] && j < 15; i++) {
            if (isdigit((unsigned char)e.dob[i])) safeDob[j++] = e.dob[i];
        }
        safeDob[j] = '\0';

        uint8_t cmd_buf[512];
        int cmd_len = 0;
        dicom_add_ul(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0000, 0);
        dicom_add_string(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0002, "1.2.840.10008.5.1.4.31");
        dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0100, 0x8020);
        dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0120, msg_id);
        dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0800, 0x0102);
        dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0900, 0xFF00);
        uint32_t gl = (uint32_t)(cmd_len - 12);
        cmd_buf[8] = gl & 0xFF; cmd_buf[9] = (gl >> 8) & 0xFF;
        cmd_buf[10] = (gl >> 16) & 0xFF; cmd_buf[11] = (gl >> 24) & 0xFF;
        send_pdata_tf(clientSocket, pc_id, 0x03, cmd_buf, cmd_len);

        uint8_t data_buf[8192];
        int data_len = 0;

        /* 1. Root level attributes */
        if (!is_field_omitted(0x0008, 0x0020)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0020, safeDate);
        if (!is_field_omitted(0x0008, 0x0030)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0030, safeTime);
        if (!is_field_omitted(0x0008, 0x0050)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0050, e.accNum);
        if (!is_field_omitted(0x0008, 0x0090)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0090, e.refPhys);

        if (!is_field_omitted(0x0010, 0x0010)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0010, 0x0010, e.patientName);
        if (!is_field_omitted(0x0010, 0x0020)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0010, 0x0020, e.patientID);
        if (!is_field_omitted(0x0010, 0x0030)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0010, 0x0030, safeDob);
        if (!is_field_omitted(0x0010, 0x0040)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0010, 0x0040, e.sex);

        if (!is_field_omitted(0x0020, 0x000D)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0020, 0x000D, e.studyUID);
        if (!is_field_omitted(0x0032, 0x1032)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0032, 0x1032, e.reqPhys);
        if (!is_field_omitted(0x0032, 0x1033)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0032, 0x1033, e.reqSvc);
        if (!is_field_omitted(0x0032, 0x1060)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0032, 0x1060, e.procDesc);

        /* 2. Scheduled Procedure Step Sequence */
        dicom_seq_hdr(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0100);
        dicom_item_hdr(data_buf, &data_len, sizeof(data_buf));
        if (!is_field_omitted(0x0008, 0x0060)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0060, e.modality);
        if (!is_field_omitted(0x0040, 0x0001)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0001, e.aeTitle);
        if (!is_field_omitted(0x0040, 0x0002)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0002, safeDate);
        if (!is_field_omitted(0x0040, 0x0003)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0003, safeTime);
        if (!is_field_omitted(0x0040, 0x0006)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0006, e.perfPhys);
        if (!is_field_omitted(0x0040, 0x0007)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0007, e.spsDesc);

        /* Protocol Code Sequence */
        dicom_seq_hdr(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0008);
        dicom_item_hdr(data_buf, &data_len, sizeof(data_buf));
        if (!is_field_omitted(0x0008, 0x0100)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0100, e.protoCode);
        if (!is_field_omitted(0x0008, 0x0102)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0102, e.protoScheme);
        if (!is_field_omitted(0x0008, 0x0104)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0104, e.protoMeaning);
        dicom_item_delim(data_buf, &data_len, sizeof(data_buf));
        dicom_seq_delim(data_buf, &data_len, sizeof(data_buf));

        if (!is_field_omitted(0x0040, 0x0009)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0009, e.spsID);
        if (!is_field_omitted(0x0040, 0x0010)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0010, e.station);
        if (!is_field_omitted(0x0040, 0x0011)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0011, e.loc);

        dicom_item_delim(data_buf, &data_len, sizeof(data_buf));
        dicom_seq_delim(data_buf, &data_len, sizeof(data_buf));

        /* 3. Trailing root level attributes */
        if (!is_field_omitted(0x0040, 0x1001)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x1001, e.procID);
        if (!is_field_omitted(0x0040, 0x1002)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x1002, e.procReason);
        if (!is_field_omitted(0x0040, 0x2004)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x2004, safeDate);
        if (!is_field_omitted(0x0040, 0x2005)) dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x2005, safeTime);

        send_pdata_tf(clientSocket, pc_id, 0x02, data_buf, data_len);
    }
    LeaveCriticalSection(&g_csvLock);

    uint8_t cmd_buf[512];
    int cmd_len = 0;
    dicom_add_ul(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0000, 0);
    dicom_add_string(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0002, "1.2.840.10008.5.1.4.31");
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0100, 0x8020);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0120, msg_id);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0800, 0x0101);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0900, 0x0000);
    uint32_t gl = (uint32_t)(cmd_len - 12);
    cmd_buf[8] = gl & 0xFF; cmd_buf[9] = (gl >> 8) & 0xFF;
    cmd_buf[10] = (gl >> 16) & 0xFF; cmd_buf[11] = (gl >> 24) & 0xFF;
    send_pdata_tf(clientSocket, pc_id, 0x03, cmd_buf, cmd_len);
}

static void handle_c_store_rq(SOCKET clientSocket, uint8_t pc_id, uint16_t msg_id,
                              uint8_t *cmd_buf, int cmd_len, uint8_t *data_buf, int data_len) {
    char sopClass[128] = {0}, sopInst[128] = {0}, acc[128] = {0}, pName[128] = {0}, pId[128] = {0}, mod[128] = {0}, studyDesc[128] = {0};
    char pDob[128] = {0}, pSex[128] = {0}, reqPhys[128] = {0}, reqProcDesc[128] = {0};
    char reason[128] = {0}, procId[128] = {0}, stationAe[128] = {0}, studyDate[128] = {0};
    char studyTime[128] = {0}, perfPhys[128] = {0}, spsDesc[128] = {0}, spsId[128] = {0};
    char stationName[128] = {0}, refPhys[128] = {0}, studyUID[128] = {0};

    dicom_get_string(cmd_buf, cmd_len, 0x0000, 0x0002, sopClass);
    dicom_get_string(cmd_buf, cmd_len, 0x0000, 0x1000, sopInst);
    if (sopInst[0] == '\0') dicom_get_string(data_buf, data_len, 0x0008, 0x0018, sopInst);

    dicom_get_string(data_buf, data_len, 0x0008, 0x0050, acc);
    dicom_get_string(data_buf, data_len, 0x0010, 0x0010, pName);
    dicom_get_string(data_buf, data_len, 0x0010, 0x0020, pId);
    dicom_get_string(data_buf, data_len, 0x0008, 0x0060, mod);
    dicom_get_string(data_buf, data_len, 0x0008, 0x1030, studyDesc);

    dicom_get_string(data_buf, data_len, 0x0010, 0x0030, pDob);
    dicom_get_string(data_buf, data_len, 0x0010, 0x0040, pSex);
    dicom_get_string(data_buf, data_len, 0x0032, 0x1032, reqPhys);
    dicom_get_string(data_buf, data_len, 0x0032, 0x1060, reqProcDesc);
    dicom_get_string(data_buf, data_len, 0x0040, 0x1002, reason);
    dicom_get_string(data_buf, data_len, 0x0040, 0x1001, procId);
    dicom_get_string(data_buf, data_len, 0x0040, 0x0001, stationAe);
    dicom_get_string(data_buf, data_len, 0x0008, 0x0020, studyDate);
    dicom_get_string(data_buf, data_len, 0x0008, 0x0030, studyTime);
    dicom_get_string(data_buf, data_len, 0x0008, 0x1050, perfPhys);
    dicom_get_string(data_buf, data_len, 0x0040, 0x0007, spsDesc);
    dicom_get_string(data_buf, data_len, 0x0040, 0x0009, spsId);
    dicom_get_string(data_buf, data_len, 0x0008, 0x1010, stationName);
    dicom_get_string(data_buf, data_len, 0x0008, 0x0090, refPhys);
    dicom_get_string(data_buf, data_len, 0x0020, 0x000D, studyUID);

    TrimInPlace(acc); TrimInPlace(pName); TrimInPlace(pId); TrimInPlace(mod); TrimInPlace(studyDesc);
    TrimInPlace(pDob); TrimInPlace(pSex); TrimInPlace(reqPhys); TrimInPlace(reqProcDesc);
    TrimInPlace(reason); TrimInPlace(procId); TrimInPlace(stationAe); TrimInPlace(studyDate);
    TrimInPlace(studyTime); TrimInPlace(perfPhys); TrimInPlace(spsDesc); TrimInPlace(spsId);
    TrimInPlace(stationName); TrimInPlace(refPhys); TrimInPlace(studyUID);

    if (acc[0] == '\0') {
        int maxVal = 0;
        EnsureCsvInitialized();
        EnterCriticalSection(&g_csvLock);
        for (int i = 1; i < g_csvRows; i++) {
            char entry[ENTRY_SIZE];
            ParseCSVLineToEntry(g_csvData[i], entry);
            char *existingAcc = GetFieldPtr(entry, 8);
            if (strncmp(existingAcc, "UNLINKED", 8) == 0) {
                int val = atoi(existingAcc + 8);
                if (val > maxVal) maxVal = val;
            }
        }
        LeaveCriticalSection(&g_csvLock);
        snprintf(acc, sizeof(acc), "UNLINKED%06d", maxVal + 1);
    }

    char folder[32], filename[128], path[MAX_PATH];
    AllocateImageFile(folder, filename, path);
    FILE *fp = fopen(path, "wb");
    uint16_t status = 0xA700;

    if (fp) {
        fwrite(data_buf, 1, data_len, fp);
        fclose(fp);
        RecordStoredImage(acc, pName, pId, sopInst, folder, filename, mod, data_len);

        if (g_bRelayMode) {
            RelayTask *task = (RelayTask *)malloc(sizeof(RelayTask));
            if (task) {
                strncpy(task->sopClass, sopClass[0] ? sopClass : "1.2.840.10008.5.1.4.1.1.2", 127);
                task->sopClass[127] = '\0';
                strncpy(task->sopInst, sopInst, 127);
                task->sopInst[127] = '\0';
                strncpy(task->filePath, path, MAX_PATH - 1);
                task->filePath[MAX_PATH - 1] = '\0';
                HANDLE hThread = CreateThread(NULL, 0, RelayThread, task, 0, NULL);
                if (hThread) CloseHandle(hThread);
            }
        }

        EnsureCsvInitialized();
        EnterCriticalSection(&g_csvLock);
        int found = 0;
        for (int i = 1; i < g_csvRows; i++) {
            if (AccessionEquals(g_csvData[i], acc)) {
                found = 1;
                char entry[ENTRY_SIZE];
                ParseCSVLineToEntry(g_csvData[i], entry);
                int changed = 0;

                if (pName[0] && !GetFieldPtr(entry, 0)[0]) { strncpy(GetFieldPtr(entry, 0), pName, FIELD_SIZE - 1); changed = 1; }
                if (pId[0] && !GetFieldPtr(entry, 1)[0]) { strncpy(GetFieldPtr(entry, 1), pId, FIELD_SIZE - 1); changed = 1; }
                if (pDob[0] && !GetFieldPtr(entry, 2)[0]) { strncpy(GetFieldPtr(entry, 2), pDob, FIELD_SIZE - 1); changed = 1; }
                if (pSex[0] && !GetFieldPtr(entry, 3)[0]) { strncpy(GetFieldPtr(entry, 3), pSex, FIELD_SIZE - 1); changed = 1; }
                if (reqPhys[0] && !GetFieldPtr(entry, 4)[0]) { strncpy(GetFieldPtr(entry, 4), reqPhys, FIELD_SIZE - 1); changed = 1; }
                if (reqProcDesc[0] && !GetFieldPtr(entry, 6)[0]) { strncpy(GetFieldPtr(entry, 6), reqProcDesc, FIELD_SIZE - 1); changed = 1; }
                if (reason[0] && !GetFieldPtr(entry, 7)[0]) { strncpy(GetFieldPtr(entry, 7), reason, FIELD_SIZE - 1); changed = 1; }
                if (procId[0] && !GetFieldPtr(entry, 9)[0]) { strncpy(GetFieldPtr(entry, 9), procId, FIELD_SIZE - 1); changed = 1; }
                if (stationAe[0] && !GetFieldPtr(entry, 11)[0]) { strncpy(GetFieldPtr(entry, 11), stationAe, FIELD_SIZE - 1); changed = 1; }
                if (studyDate[0] && !GetFieldPtr(entry, 12)[0]) { strncpy(GetFieldPtr(entry, 12), studyDate, FIELD_SIZE - 1); changed = 1; }
                if (studyTime[0] && !GetFieldPtr(entry, 13)[0]) { strncpy(GetFieldPtr(entry, 13), studyTime, FIELD_SIZE - 1); changed = 1; }
                if (perfPhys[0] && !GetFieldPtr(entry, 14)[0]) { strncpy(GetFieldPtr(entry, 14), perfPhys, FIELD_SIZE - 1); changed = 1; }
                if (spsDesc[0] && !GetFieldPtr(entry, 15)[0]) { strncpy(GetFieldPtr(entry, 15), spsDesc, FIELD_SIZE - 1); changed = 1; }
                if (spsId[0] && !GetFieldPtr(entry, 16)[0]) { strncpy(GetFieldPtr(entry, 16), spsId, FIELD_SIZE - 1); changed = 1; }
                if (stationName[0] && !GetFieldPtr(entry, 17)[0]) { strncpy(GetFieldPtr(entry, 17), stationName, FIELD_SIZE - 1); changed = 1; }
                if (mod[0] && !GetFieldPtr(entry, 20)[0]) { strncpy(GetFieldPtr(entry, 20), mod, FIELD_SIZE - 1); changed = 1; }
                if (refPhys[0] && !GetFieldPtr(entry, 21)[0]) { strncpy(GetFieldPtr(entry, 21), refPhys, FIELD_SIZE - 1); changed = 1; }
                if (studyDesc[0] && !GetFieldPtr(entry, 22)[0]) { strncpy(GetFieldPtr(entry, 22), studyDesc, FIELD_SIZE - 1); changed = 1; }
                if (studyUID[0] && !GetFieldPtr(entry, 26)[0]) { strncpy(GetFieldPtr(entry, 26), studyUID, FIELD_SIZE - 1); changed = 1; }

                if (changed) {
                    char lineOut[LINE_SIZE];
                    BuildCSVLineFromEntry(entry, lineOut);
                    strncpy(g_csvData[i], lineOut, LINE_SIZE - 1);
                }
                break;
            }
        }
        if (!found) {
            char newEntry[ENTRY_SIZE];
            memset(newEntry, 0, ENTRY_SIZE);
            strncpy(GetFieldPtr(newEntry, 0), pName[0] ? pName : "Unknown Patient", FIELD_SIZE - 1);
            strncpy(GetFieldPtr(newEntry, 1), pId[0] ? pId : "UNKNOWN", FIELD_SIZE - 1);
            if (pDob[0]) strncpy(GetFieldPtr(newEntry, 2), pDob, FIELD_SIZE - 1);
            if (pSex[0]) strncpy(GetFieldPtr(newEntry, 3), pSex, FIELD_SIZE - 1);
            if (reqPhys[0]) strncpy(GetFieldPtr(newEntry, 4), reqPhys, FIELD_SIZE - 1);
            if (reqProcDesc[0]) strncpy(GetFieldPtr(newEntry, 6), reqProcDesc, FIELD_SIZE - 1);
            if (reason[0]) strncpy(GetFieldPtr(newEntry, 7), reason, FIELD_SIZE - 1);
            strncpy(GetFieldPtr(newEntry, 8), acc, FIELD_SIZE - 1);
            if (procId[0]) strncpy(GetFieldPtr(newEntry, 9), procId, FIELD_SIZE - 1);
            if (stationAe[0]) strncpy(GetFieldPtr(newEntry, 11), stationAe, FIELD_SIZE - 1);
            if (studyDate[0]) strncpy(GetFieldPtr(newEntry, 12), studyDate, FIELD_SIZE - 1);
            if (studyTime[0]) strncpy(GetFieldPtr(newEntry, 13), studyTime, FIELD_SIZE - 1);
            if (perfPhys[0]) strncpy(GetFieldPtr(newEntry, 14), perfPhys, FIELD_SIZE - 1);
            if (spsDesc[0]) strncpy(GetFieldPtr(newEntry, 15), spsDesc, FIELD_SIZE - 1);
            if (spsId[0]) strncpy(GetFieldPtr(newEntry, 16), spsId, FIELD_SIZE - 1);
            if (stationName[0]) strncpy(GetFieldPtr(newEntry, 17), stationName, FIELD_SIZE - 1);
            strncpy(GetFieldPtr(newEntry, 19), "5", FIELD_SIZE - 1);
            strncpy(GetFieldPtr(newEntry, 20), mod[0] ? mod : "OT", FIELD_SIZE - 1);
            if (refPhys[0]) strncpy(GetFieldPtr(newEntry, 21), refPhys, FIELD_SIZE - 1);
            if (studyDesc[0]) strncpy(GetFieldPtr(newEntry, 22), studyDesc, FIELD_SIZE - 1);
            if (studyUID[0]) strncpy(GetFieldPtr(newEntry, 26), studyUID, FIELD_SIZE - 1);

            char lineOut[LINE_SIZE];
            BuildCSVLineFromEntry(newEntry, lineOut);
            if (g_csvRows < MAX_CSV_ROWS) {
                strncpy(g_csvData[g_csvRows], lineOut, LINE_SIZE - 1);
                g_csvRows++;
            }
        }
        LeaveCriticalSection(&g_csvLock);

        status = 0x0000;
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "C-STORE OK -> %s\\%s (%d B) acc=%s", folder, filename, data_len, acc);
        write_log(log_msg);
    }

    uint8_t rsp_buf[512];
    int rsp_len = 0;
    dicom_add_ul(rsp_buf, &rsp_len, sizeof(rsp_buf), 0x0000, 0x0000, 0);
    dicom_add_string(rsp_buf, &rsp_len, sizeof(rsp_buf), 0x0000, 0x0002, sopClass[0] ? sopClass : "1.2.840.10008.5.1.4.1.1.2");
    dicom_add_us(rsp_buf, &rsp_len, sizeof(rsp_buf), 0x0000, 0x0100, 0x8001);
    dicom_add_us(rsp_buf, &rsp_len, sizeof(rsp_buf), 0x0000, 0x0120, msg_id);
    dicom_add_us(rsp_buf, &rsp_len, sizeof(rsp_buf), 0x0000, 0x0800, 0x0102);
    dicom_add_us(rsp_buf, &rsp_len, sizeof(rsp_buf), 0x0000, 0x0900, status);
    if (sopInst[0]) dicom_add_string(rsp_buf, &rsp_len, sizeof(rsp_buf), 0x0000, 0x1000, sopInst);

    uint32_t gl = (uint32_t)(rsp_len - 12);
    rsp_buf[8] = gl & 0xFF; rsp_buf[9] = (gl >> 8) & 0xFF;
    rsp_buf[10] = (gl >> 16) & 0xFF; rsp_buf[11] = (gl >> 24) & 0xFF;
    send_pdata_tf(clientSocket, pc_id, 0x03, rsp_buf, rsp_len);
}

static void handle_pdata_tf(SOCKET clientSocket, char *buffer, int bytesRead) {
    if (bytesRead < 6) return;
    uint32_t pdu_len = ((uint32_t)(uint8_t)buffer[2] << 24) | ((uint32_t)(uint8_t)buffer[3] << 16) |
                       ((uint32_t)(uint8_t)buffer[4] << 8) | (uint32_t)(uint8_t)buffer[5];
    int pos = 6;
    int end = 6 + (int)pdu_len;
    if (end > bytesRead) end = bytesRead;

    static uint8_t g_findCmd[16384];
    static int g_findCmdLen = 0;
    static int g_findDataCapacity = 64 * 1024 * 1024;
    static int g_findDataLen = 0;
    static uint8_t g_findPcId = 0;
    static uint16_t g_findMsgId = 0;
    static uint16_t g_findCommand = 0;

    if (g_findData == NULL) {
        g_findData = (uint8_t *)malloc(g_findDataCapacity);
    }

    while (pos + 6 <= end) {
        uint32_t pdv_len = ((uint32_t)(uint8_t)buffer[pos] << 24) | ((uint32_t)(uint8_t)buffer[pos + 1] << 16) |
                           ((uint32_t)(uint8_t)buffer[pos + 2] << 8) | (uint32_t)(uint8_t)buffer[pos + 3];
        uint8_t pc_id = (uint8_t)buffer[pos + 4];
        uint8_t msg_ctrl = (uint8_t)buffer[pos + 5];
        int data_off = pos + 6;
        int data_len = (int)pdv_len - 2;
        if (data_len < 0 || data_off + data_len > end) break;
        bool is_command = (msg_ctrl & 0x01) != 0;
        bool is_last_frag = (msg_ctrl & 0x02) != 0;

        if (is_command) {
            if (g_findCmdLen + data_len < (int)sizeof(g_findCmd)) {
                memcpy(g_findCmd + g_findCmdLen, buffer + data_off, data_len);
                g_findCmdLen += data_len;
            }
            g_findPcId = pc_id;
            if (is_last_frag) {
                g_findCommand = 0;
                g_findMsgId = 0;
                for (int i = 0; i <= g_findCmdLen - 10; i++) {
                    /* Command Field (0000,0100) */
                    if (g_findCmd[i] == 0x00 && g_findCmd[i + 1] == 0x00 && g_findCmd[i + 2] == 0x00 &&
                        g_findCmd[i + 3] == 0x01 && g_findCmd[i + 4] == 0x02 && g_findCmd[i + 5] == 0x00) {
                        g_findCommand = (uint16_t)g_findCmd[i + 8] | ((uint16_t)g_findCmd[i + 9] << 8);
                    }
                    /* Message ID (0000,0110) */
                    if (g_findCmd[i] == 0x00 && g_findCmd[i + 1] == 0x00 && g_findCmd[i + 2] == 0x10 &&
                        g_findCmd[i + 3] == 0x01 && g_findCmd[i + 4] == 0x02 && g_findCmd[i + 5] == 0x00) {
                        g_findMsgId = (uint16_t)g_findCmd[i + 8] | ((uint16_t)g_findCmd[i + 9] << 8);
                    }
                }
                if (g_findCommand == 0x0030) {
                    handle_c_echo_rq(clientSocket, g_findPcId, g_findMsgId);
                    g_findCmdLen = 0;
                    g_findDataLen = 0;
                }
            }
        } else {
            if (g_findData && g_findDataLen + data_len < g_findDataCapacity) {
                memcpy(g_findData + g_findDataLen, buffer + data_off, data_len);
                g_findDataLen += data_len;
            }
            if (is_last_frag) {
                if (g_findCommand == 0x0020) {
                    handle_c_find_rq(clientSocket, g_findPcId, g_findMsgId, g_findData, g_findDataLen);
                } else if (g_findCommand == 0x0001) {
                    handle_c_store_rq(clientSocket, g_findPcId, g_findMsgId, g_findCmd, g_findCmdLen, g_findData, g_findDataLen);
                }
                g_findCmdLen = 0;
                g_findDataLen = 0;
            }
        }
        pos += 4 + (int)pdv_len;
    }
}

static int recv_pdu(SOCKET s, uint8_t *buf, int max_len) {
    int total = 0;
    while (total < 6) {
        int r = recv(s, (char *)buf + total, 6 - total, 0);
        if (r <= 0) return r;
        total += r;
    }
    uint32_t pdu_len = ((uint32_t)buf[2] << 24) | ((uint32_t)buf[3] << 16) |
                       ((uint32_t)buf[4] << 8) | (uint32_t)buf[5];
    if (pdu_len + 6 > (uint32_t)max_len) return -1;
    while (pdu_len > 0) {
        int r = recv(s, (char *)buf + total, pdu_len, 0);
        if (r <= 0) return r;
        total += r;
        pdu_len -= r;
    }
    return total;
}

static int StartDicomServer(void) {
    struct sockaddr_in sin;
    g_listenSocketDicom = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocketDicom == INVALID_SOCKET) {
        printf(szListenFailFmt, g_DicomPort);
        return 0;
    }
    int opt = 1;
    setsockopt(g_listenSocketDicom, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((u_short)g_DicomPort);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_listenSocketDicom, (struct sockaddr *)&sin, sizeof(sin)) == SOCKET_ERROR) {
        printf(szListenFailFmt, g_DicomPort);
        closesocket(g_listenSocketDicom);
        g_listenSocketDicom = INVALID_SOCKET;
        return 0;
    }
    if (listen(g_listenSocketDicom, SOMAXCONN) == SOCKET_ERROR) {
        printf(szListenFailFmt, g_DicomPort);
        closesocket(g_listenSocketDicom);
        g_listenSocketDicom = INVALID_SOCKET;
        return 0;
    }
    printf(szListenDicomFmt, g_DicomPort);
    return 1;
}

static void DicomNetworkLoop(void) {
    char buffer[BUFFER_SIZE];
    while (!g_bExitApp && g_listenSocketDicom != INVALID_SOCKET) {
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(g_listenSocketDicom, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int activity = select((int)g_listenSocketDicom + 1, &readfds, NULL, NULL, &tv);
        if (activity > 0 && FD_ISSET(g_listenSocketDicom, &readfds)) {
            SOCKET clientSocket = accept(g_listenSocketDicom, NULL, NULL);
            if (clientSocket == INVALID_SOCKET) continue;
            write_log(szDicomConnFmt);

            int bytesRead;
            int closed = 0;
            while (!closed && (bytesRead = recv_pdu(clientSocket, (uint8_t *)buffer, BUFFER_SIZE)) > 0) {
                switch ((unsigned char)buffer[0]) {
                    case 0x01:
                        handle_association_rq(clientSocket, buffer, bytesRead);
                        break;
                    case 0x04:
                        handle_pdata_tf(clientSocket, buffer, bytesRead);
                        break;
                    case 0x05: {
                        uint8_t rel_rp[] = { 0x06, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 };
                        send(clientSocket, (char *)rel_rp, sizeof(rel_rp), 0);
                        closed = 1;
                        break;
                    }
                    case 0x02: {
                        uint8_t abort_pdu[] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02 };
                        send(clientSocket, (char *)abort_pdu, sizeof(abort_pdu), 0);
                        closed = 1;
                        break;
                    }
                }
            }
            closesocket(clientSocket);
        }
    }
}

static DWORD WINAPI DicomThread(LPVOID lpParam) {
    (void)lpParam;
    DicomNetworkLoop();
    return 0;
}

/* ============================================================================
 * Public Embeddable API
 * ============================================================================ */

/* Initialize configuration, locks, Winsock, storage folders, and CSV stores.
 * Pass NULL for ini_path to automatically use <executable_name>.ini. */
static int ris_dicom_init(const char *ini_path) {
    srand((unsigned int)GetTickCount());

    if (ini_path && ini_path[0]) {
        strncpy(szIniFile, ini_path, MAX_PATH - 1);
        szIniFile[MAX_PATH - 1] = '\0';
    } else {
        ResolveIniPath();
    }

    printf("%s", szStartup);
    LoadConfig();
    printf(szConfigFmt, g_aeCalled, g_DicomPort, g_DebugLog, g_bRelayMode);

    if (!g_locksInitialized) {
        InitializeCriticalSection(&g_csvLock);
        InitializeCriticalSection(&g_procCodesLock);
        InitializeCriticalSection(&g_cstoreLock);
        g_locksInitialized = 1;
    }

    CreateDirectoryA(g_storageRoot, NULL);
    char dirBuf[MAX_PATH];
    snprintf(dirBuf, sizeof(dirBuf), "%s\\%s", g_storageRoot, g_storageFolder);
    CreateDirectoryA(dirBuf, NULL);

    LoadCStoreTracking();

    if (!g_wsaInitialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return 0;
        }
        g_wsaInitialized = 1;
    }

    EnsureCsvInitialized();
    EnterCriticalSection(&g_csvLock);
    strncpy(g_csvData[0], szCSVHeader, LINE_SIZE - 1);
    g_csvData[0][LINE_SIZE - 1] = '\0';
    g_csvRows = 1;
    LeaveCriticalSection(&g_csvLock);

    LoadProcedureCodes();
    return 1;
}

/* Start the DICOM SCP listener and periodic backup background threads. */
static int ris_dicom_start(void) {
    if (g_bRunning) return 1;
    g_bExitApp = 0;

    if (!StartDicomServer()) {
        return 0;
    }

    g_bRunning = 1;
    g_hDicomThread = CreateThread(NULL, 0, DicomThread, NULL, 0, NULL);
    g_hBackupThread = CreateThread(NULL, 0, PeriodicBackupThread, NULL, 0, NULL);
    printf("%s", szServerStarted);
    return 1;
}

/* Stop the DICOM SCP server and wait for worker threads to exit cleanly. */
static void ris_dicom_stop(void) {
    if (!g_bRunning) return;
    g_bExitApp = 1;

    if (g_listenSocketDicom != INVALID_SOCKET) {
        closesocket(g_listenSocketDicom);
        g_listenSocketDicom = INVALID_SOCKET;
    }

    if (g_hDicomThread) {
        WaitForSingleObject(g_hDicomThread, 3000);
        CloseHandle(g_hDicomThread);
        g_hDicomThread = NULL;
    }
    if (g_hBackupThread) {
        WaitForSingleObject(g_hBackupThread, 3000);
        CloseHandle(g_hBackupThread);
        g_hBackupThread = NULL;
    }

    g_bRunning = 0;
    printf("%s", szServerStopped);
}

/* Stop server, export CSV backups, and release all resources. */
static void ris_dicom_shutdown(void) {
    ris_dicom_stop();
    ExportCSVsOnExit();

    if (g_findData) {
        free(g_findData);
        g_findData = NULL;
    }

    patients_csv_data = NULL;
    patients_csv_data_len = 0;

    if (g_wsaInitialized) {
        WSACleanup();
        g_wsaInitialized = 0;
    }
    if (g_locksInitialized) {
        DeleteCriticalSection(&g_csvLock);
        DeleteCriticalSection(&g_procCodesLock);
        DeleteCriticalSection(&g_cstoreLock);
        g_locksInitialized = 0;
    }
}

/* Programmatically insert or update a worklist row from a 27-field CSV line.
 * Returns 1 if updated, 0 if inserted, -1 if invalid (missing Accession). */
static int ris_dicom_upsert_worklist_csv(const char *csv_line, const char *old_accession) {
    if (!csv_line || !csv_line[0]) return -1;
    char entry[ENTRY_SIZE];
    ParseCSVLineToEntry(csv_line, entry);
    if (!ValidateEntry(entry)) return -1;

    StripDashes(GetFieldPtr(entry, 2));
    StripDashes(GetFieldPtr(entry, 12));
    StripColons(GetFieldPtr(entry, 13));
    FormatPatientName(GetFieldPtr(entry, 0));
    GenerateStudyUIDIfNeeded(entry);

    if (old_accession && old_accession[0]) {
        UpdateAccessionImages(old_accession, entry);
    }

    BuildCSVLineFromEntry(entry, g_lineOut);
    return UpdateCsvByPatient(entry, g_lineOut);
}

/* Remove a worklist row by Accession Number. Returns 1 if removed, 0 if not found. */
static int ris_dicom_delete_worklist_accession(const char *accession) {
    if (!accession || !accession[0]) return 0;
    int removed = 0;
    EnterCriticalSection(&g_csvLock);
    for (int i = 1; i < g_csvRows; i++) {
        if (AccessionEquals(g_csvData[i], accession)) {
            for (int j = i; j < g_csvRows - 1; j++) {
                strncpy(g_csvData[j], g_csvData[j + 1], LINE_SIZE - 1);
            }
            g_csvRows--;
            removed = 1;
            break;
        }
    }
    LeaveCriticalSection(&g_csvLock);
    return removed;
}

static int ris_dicom_is_running(void) {
    return g_bRunning;
}

#ifdef __cplusplus
}
#endif

#endif /* RIS_DICOM_SERVER_H */