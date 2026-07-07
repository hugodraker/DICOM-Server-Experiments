/*
 * =============================================================================
 * DICOM MWL Server Implementation
 * * License: Public Domain - No Warranty or Fitness for Purpose
 * Released without any warranty, express or implied, including merchantability
 * or fitness for a particular purpose. Use at your own risk.
 * * Version: 1.3 (Group Length Injection, Tag Dictionary & Strict Order Fixes)
 * Date: 2026-07-07
 * =============================================================================
 * gcc -Os -s -ffunction-sections -fdata-sections -fno-ident -fno-asynchronous-unwind-tables -fno-exceptions -fno-rtti -fno-stack-protector -mwindows -Wl,--gc-sections,--strip-all,-s -o DICOM_WORKLIST_SCP.exe DICOM_WORKLIST_SCP.c -lws2_32 -static-libgcc
 */

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#pragma comment(lib, "ws2_32.lib")

#define BUFFER_SIZE 16384
#define MAX_CSV_LINE 2048

// =========================================================================
// Configuration & Data Structures
// =========================================================================

typedef struct {
    char aetitle[17];
    int port;
    bool debug;
    char log_file[256];
} ServerConfig;

typedef struct {
    char patientName[65], patientID[65], dob[17], sex[17];
    char reqPhys[65], reqSvc[65], procDesc[65], procReason[65];
    char accNum[65], procID[65], procPri[17];
    char aeTitle[17], spsDate[17], spsTime[17], perfPhys[65];
    char spsDesc[65], spsID[65], station[65], loc[65], status[17];
    char modality[17], refPhys[65], studyDesc[65];
} MWLEntry;

ServerConfig cfg;
FILE *log_fp = NULL;

void write_log(const char *msg) {
    if (!cfg.debug) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char ts[64];
    snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d.%03d", 
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    
    if (log_fp) {
        fprintf(log_fp, "[%s] %s\n", ts, msg);
        fflush(log_fp);
    }
    printf("[%s] %s\n", ts, msg);
}

// =========================================================================
// Initialization & CSV Handling
// =========================================================================

void init_config(const char *exe_path) {
    char exe_name[256];
    const char *p = strrchr(exe_path, '\\');
    p = p ? p + 1 : exe_path;
    strncpy(exe_name, p, sizeof(exe_name) - 1);
    exe_name[sizeof(exe_name) - 1] = '\0';
    char *dot = strrchr(exe_name, '.');
    if (dot) *dot = '\0';

    char ini_name[256];
    snprintf(ini_name, sizeof(ini_name), "%s.ini", exe_name);
    snprintf(cfg.log_file, sizeof(cfg.log_file), "%s.log", exe_name);
    
    strcpy(cfg.aetitle, "MWLSERVER");
    cfg.port = 104;
    cfg.debug = true;

    FILE *f = fopen(ini_name, "r");
    if (!f) {
        f = fopen(ini_name, "w");
        if (f) {
            fprintf(f, "AETITLE=MWLSERVER\nPORT=104\nDEBUG=1\n");
            fclose(f);
        }
        cfg.port = 104;
    } else {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            char k[64], v[64];
            if (sscanf(line, "%63[^=]=%63s", k, v) == 2) {
                if (strcmp(k, "AETITLE") == 0) {
                    strncpy(cfg.aetitle, v, 16);
                    cfg.aetitle[16] = '\0';
                }
                else if (strcmp(k, "PORT") == 0) cfg.port = atoi(v);
                else if (strcmp(k, "DEBUG") == 0) cfg.debug = atoi(v);
            }
        }
        fclose(f);
    }
    log_fp = fopen(cfg.log_file, "a");
}

void trim_field(char *str) {
    if (!str) return;
    char *p = str;
    while (*p == ' ' || *p == '"') p++;
    memmove(str, p, strlen(p) + 1);
    int len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '"' || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
}

// Improved CSV Parsing to prevent shifting empty fields
void parse_csv_line(char *line, MWLEntry *e) {
    memset(e, 0, sizeof(MWLEntry));
    char *fields[] = { 
        e->patientName, e->patientID, e->dob, e->sex, e->reqPhys, e->reqSvc, e->procDesc, e->procReason, 
        e->accNum, e->procID, e->procPri, e->aeTitle, e->spsDate, e->spsTime, e->perfPhys, 
        e->spsDesc, e->spsID, e->station, e->loc, e->status, e->modality, e->refPhys, e->studyDesc 
    };
    
    char *start = line;
    for (int i = 0; i < 23; i++) {
        char *end = strchr(start, ',');
        if (end) {
            *end = '\0';
            strncpy(fields[i], start, 64);
            fields[i][64] = '\0';
            trim_field(fields[i]);
            start = end + 1;
        } else {
            strncpy(fields[i], start, 64);
            fields[i][64] = '\0';
            trim_field(fields[i]);
            break;
        }
    }
}

void ensure_csv_exists() {
    FILE *f = fopen("patients.csv", "r");
    if (!f) {
        f = fopen("patients.csv", "w");
        if (f) {
            fprintf(f, "\"PatientName\",\"PatientID\",\"BirthDate\",\"Sex\",\"ReqPhys\",\"ReqSvc\",\"ProcDesc\",\"Reason\",\"Accession\",\"ProcID\",\"Priority\",\"StationAE\",\"StartDate\",\"StartTime\",\"PerfPhys\",\"SPSDesc\",\"SPSID\",\"StationName\",\"Location\",\"Status\",\"Modality\",\"RefPhys\",\"StudyDesc\"\n");
            fprintf(f, "\"DOE^JOHN\",\"P001\",\"19800101\",\"M\",\"SMITH^DR\",\"RAD\",\"CT Head\",\"Headache\",\"ACC001\",\"PROC001\",\"1\",\"%s\",\"20260706\",\"120000\",\"JONES^DR\",\"CT Head w/o\",\"SPS001\",\"CT01\",\"Room 1\",\"SCHEDULED\",\"CT\",\"WILSON^DR\",\"Routine\"\n", cfg.aetitle);
            fclose(f);
            write_log("Created sample patients.csv");
        }
    } else {
        fclose(f);
    }
}

// =========================================================================
// DICOM Element Writers (Implicit VR Little Endian with Bounds Checking)
// =========================================================================

bool add_elem_string(uint8_t* buf, int* offset, int max_len, uint16_t group, uint16_t elem, const char* str) {
    uint32_t len = str ? strlen(str) : 0;
    uint32_t pad = (len % 2 != 0) ? 1 : 0;
    uint32_t total_len = len + pad;
    
    if (*offset + 8 + (int)total_len > max_len) return false;
    
    buf[(*offset)++] = group & 0xFF; buf[(*offset)++] = (group >> 8) & 0xFF;
    buf[(*offset)++] = elem & 0xFF;  buf[(*offset)++] = (elem >> 8) & 0xFF;
    
    buf[(*offset)++] = total_len & 0xFF;         buf[(*offset)++] = (total_len >> 8) & 0xFF;
    buf[(*offset)++] = (total_len >> 16) & 0xFF; buf[(*offset)++] = (total_len >> 24) & 0xFF;
    
    if (len > 0) { memcpy(&buf[*offset], str, len); *offset += len; }
    if (pad) { buf[(*offset)++] = ' '; }
    return true;
}

bool add_elem_us(uint8_t* buf, int* offset, int max_len, uint16_t group, uint16_t elem, uint16_t val) {
    if (*offset + 10 > max_len) return false;
    buf[(*offset)++] = group & 0xFF; buf[(*offset)++] = (group >> 8) & 0xFF;
    buf[(*offset)++] = elem & 0xFF;  buf[(*offset)++] = (elem >> 8) & 0xFF;
    buf[(*offset)++] = 2; buf[(*offset)++] = 0; buf[(*offset)++] = 0; buf[(*offset)++] = 0;
    buf[(*offset)++] = val & 0xFF; buf[(*offset)++] = (val >> 8) & 0xFF;
    return true;
}

// Fixed: Helper function for (0000, 0000) Command Group Length writes
bool add_elem_ul(uint8_t* buf, int* offset, int max_len, uint16_t group, uint16_t elem, uint32_t val) {
    if (*offset + 12 > max_len) return false;
    buf[(*offset)++] = group & 0xFF; buf[(*offset)++] = (group >> 8) & 0xFF;
    buf[(*offset)++] = elem & 0xFF;  buf[(*offset)++] = (elem >> 8) & 0xFF;
    buf[(*offset)++] = 4; buf[(*offset)++] = 0; buf[(*offset)++] = 0; buf[(*offset)++] = 0;
    buf[(*offset)++] = val & 0xFF; buf[(*offset)++] = (val >> 8) & 0xFF;
    buf[(*offset)++] = (val >> 16) & 0xFF; buf[(*offset)++] = (val >> 24) & 0xFF;
    return true;
}

bool add_sequence_header(uint8_t* buf, int* offset, int max_len, uint16_t group, uint16_t elem) {
    if (*offset + 8 > max_len) return false;
    buf[(*offset)++] = group & 0xFF; buf[(*offset)++] = (group >> 8) & 0xFF;
    buf[(*offset)++] = elem & 0xFF;  buf[(*offset)++] = (elem >> 8) & 0xFF;
    buf[(*offset)++] = 0xFF; buf[(*offset)++] = 0xFF; buf[(*offset)++] = 0xFF; buf[(*offset)++] = 0xFF;
    return true;
}

bool add_item_header(uint8_t* buf, int* offset, int max_len) {
    if (*offset + 8 > max_len) return false;
    buf[(*offset)++] = 0xFE; buf[(*offset)++] = 0xFF; // FFFE
    buf[(*offset)++] = 0x00; buf[(*offset)++] = 0xE0; // E000
    buf[(*offset)++] = 0xFF; buf[(*offset)++] = 0xFF; buf[(*offset)++] = 0xFF; buf[(*offset)++] = 0xFF;
    return true;
}

bool add_item_delim(uint8_t* buf, int* offset, int max_len) {
    if (*offset + 8 > max_len) return false;
    buf[(*offset)++] = 0xFE; buf[(*offset)++] = 0xFF; // FFFE
    buf[(*offset)++] = 0x0D; buf[(*offset)++] = 0xE0; // E00D
    buf[(*offset)++] = 0x00; buf[(*offset)++] = 0x00; buf[(*offset)++] = 0x00; buf[(*offset)++] = 0x00;
    return true;
}

bool add_seq_delim(uint8_t* buf, int* offset, int max_len) {
    if (*offset + 8 > max_len) return false;
    buf[(*offset)++] = 0xFE; buf[(*offset)++] = 0xFF; // FFFE
    buf[(*offset)++] = 0xDD; buf[(*offset)++] = 0xE0; // E0DD
    buf[(*offset)++] = 0x00; buf[(*offset)++] = 0x00; buf[(*offset)++] = 0x00; buf[(*offset)++] = 0x00;
    return true;
}

void send_pdata_tf(SOCKET s, uint8_t pc_id, uint8_t msg_ctrl, const uint8_t* data, uint32_t data_len) {
    uint32_t pdv_len = 2 + data_len;
    uint32_t pdu_len = 4 + pdv_len;
    uint8_t* pdu_buf = (uint8_t*)malloc(6 + pdu_len);
    if (!pdu_buf) return;
    
    pdu_buf[0] = 0x04; pdu_buf[1] = 0x00;
    pdu_buf[2] = (pdu_len >> 24) & 0xFF; pdu_buf[3] = (pdu_len >> 16) & 0xFF;
    pdu_buf[4] = (pdu_len >> 8) & 0xFF;  pdu_buf[5] = pdu_len & 0xFF;
    pdu_buf[6] = (pdv_len >> 24) & 0xFF; pdu_buf[7] = (pdv_len >> 16) & 0xFF;
    pdu_buf[8] = (pdv_len >> 8) & 0xFF;  pdu_buf[9] = pdv_len & 0xFF;
    pdu_buf[10] = pc_id; pdu_buf[11] = msg_ctrl;
    memcpy(&pdu_buf[12], data, data_len);
    
    send(s, (char*)pdu_buf, 6 + pdu_len, 0);
    free(pdu_buf);
}

// =========================================================================
// Protocol Handlers
// =========================================================================

void handle_association_rq(SOCKET clientSocket, char* buffer, int bytesRead) {
    write_log("Parsing A-ASSOCIATE-RQ (Dynamically mapping Contexts)...");
    
    uint8_t ac_pdu[2048] = {0};
    ac_pdu[0] = 0x02; ac_pdu[1] = 0x00;
    ac_pdu[6] = 0x00; ac_pdu[7] = 0x01; // Protocol version
    
    if (bytesRead < 42) return;
    memcpy(&ac_pdu[10], &buffer[10], 16); 
    memcpy(&ac_pdu[26], &buffer[26], 16); 

    int offset = 74;
    
    // Item 1: App Context
    uint8_t app_ctx[] = { 0x10, 0x00, 0x00, 0x15, '1','.','2','.','8','4','0','.','1','0','0','0','8','.','3','.','1','.','1','.','1' };
    memcpy(&ac_pdu[offset], app_ctx, sizeof(app_ctx)); offset += sizeof(app_ctx);

    // Dynamic Context Scanning
    int i = 74;
    while (i < bytesRead - 4) {
        uint8_t item_type = buffer[i];
        uint16_t item_len = ((uint8_t)buffer[i+2] << 8) | (uint8_t)buffer[i+3];
        
        if (item_type == 0x20 && i + 4 + item_len <= bytesRead) { // Presentation Context Request
            uint8_t pc_id = buffer[i+4];
            char log_msg[128];
            snprintf(log_msg, sizeof(log_msg), "    -> Accepting Requested PC ID: %d", pc_id);
            write_log(log_msg);
            
            ac_pdu[offset++] = 0x21; ac_pdu[offset++] = 0x00; 
            ac_pdu[offset++] = 0x00; ac_pdu[offset++] = 0x19; // Length = 25 bytes
            ac_pdu[offset++] = pc_id; 
            ac_pdu[offset++] = 0x00; ac_pdu[offset++] = 0x00; ac_pdu[offset++] = 0x00;
            
            // Accept with Implicit VR Little Endian (1.2.840.10008.1.2)
            uint8_t ts[] = { 0x40, 0x00, 0x00, 0x11, '1','.','2','.','8','4','0','.','1','0','0','0','8','.','1','.','2' };
            memcpy(&ac_pdu[offset], ts, sizeof(ts)); offset += sizeof(ts);
        }
        i += 4 + item_len;
    }

    // User Info (Max PDU = 16384)
    uint8_t user_info[] = {
        0x50, 0x00, 0x00, 0x1E, 
        0x51, 0x00, 0x00, 0x04, 0x00, 0x00, 0x40, 0x00, 
        0x52, 0x00, 0x00, 0x12, '1','.','2','.','2','7','6','.','0','.','7','2','3','.','1','.','9','9'
    };
    memcpy(&ac_pdu[offset], user_info, sizeof(user_info)); offset += sizeof(user_info);

    uint32_t pdu_len = offset - 6;
    ac_pdu[2] = (pdu_len >> 24) & 0xFF; ac_pdu[3] = (pdu_len >> 16) & 0xFF;
    ac_pdu[4] = (pdu_len >> 8) & 0xFF;  ac_pdu[5] = pdu_len & 0xFF;

    send(clientSocket, (char*)ac_pdu, offset, 0);
}


void handle_c_echo_rq(SOCKET clientSocket, uint8_t pc_id, uint16_t msg_id) {
    write_log("Received C-ECHO-RQ. Sending Success.");
    uint8_t cmd_buf[256]; int cmd_len = 0;
    
    // Command Group 0 Length Placeholder
    add_elem_ul(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0000, 0);
    
    add_elem_string(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0002, "1.2.840.10008.1.1"); 
    add_elem_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0100, 0x8030); // 0x8030 = C-ECHO-RSP
    add_elem_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0120, msg_id); 
    add_elem_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0800, 0x0101); // No Dataset
    add_elem_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0900, 0x0000); // Success Status
    
    // Calculate and rewrite actual Group 0 length
    uint32_t gl = cmd_len - 12;
    cmd_buf[8] = gl & 0xFF; cmd_buf[9] = (gl >> 8) & 0xFF;
    cmd_buf[10] = (gl >> 16) & 0xFF; cmd_buf[11] = (gl >> 24) & 0xFF;
    
    send_pdata_tf(clientSocket, pc_id, 0x03, cmd_buf, cmd_len);
}

void handle_c_find_rq(SOCKET clientSocket, uint8_t pc_id, uint16_t msg_id) {
    write_log("Received C-FIND-RQ. Building Worklist...");
    ensure_csv_exists();

    FILE *f = fopen("patients.csv", "r");
    if (f) {
        char line[MAX_CSV_LINE];
        int row = 0;
        
        while (fgets(line, sizeof(line), f)) {
            if (row++ == 0) continue;
            
            MWLEntry e;
            parse_csv_line(line, &e);

            // 1. Send Pending Command PDU
            uint8_t cmd_buf[256]; int cmd_len = 0;
            add_elem_ul(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0000, 0); // Group 0 Length Placeholder
            add_elem_string(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0002, "1.2.840.10008.5.1.4.31");
            add_elem_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0100, 0x8020);  // 0x8020 = C-FIND-RSP
            add_elem_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0120, msg_id);
            add_elem_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0800, 0x0102);  // Dataset is Present
            add_elem_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0900, 0xFF00);  // Status: Pending
            
            // Set actual Group 0 length
            uint32_t gl = cmd_len - 12;
            cmd_buf[8] = gl & 0xFF; cmd_buf[9] = (gl >> 8) & 0xFF;
            cmd_buf[10] = (gl >> 16) & 0xFF; cmd_buf[11] = (gl >> 24) & 0xFF;
            
            send_pdata_tf(clientSocket, pc_id, 0x03, cmd_buf, cmd_len);

            // 2. Send Dataset PDU - Strictly Ascending
            uint8_t data_buf[4096]; int data_len = 0;
            
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0050, e.accNum);
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0060, e.modality);
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0090, e.refPhys);
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x1030, e.studyDesc);
            
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0010, 0x0010, e.patientName);
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0010, 0x0020, e.patientID);
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0010, 0x0030, e.dob);
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0010, 0x0040, e.sex);
            
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0032, 0x1032, e.reqPhys);
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0032, 0x1033, e.reqSvc);
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0032, 0x1060, e.procDesc);
            
            // Scheduled Procedure Sequence (0040,0100)
            add_sequence_header(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0100);
            add_item_header(data_buf, &data_len, sizeof(data_buf));
                add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0001, e.aeTitle);
                add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0002, e.spsDate);
                add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0003, e.spsTime);
                add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0006, e.perfPhys);
                add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0007, e.spsDesc);
                add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0009, e.spsID);
                add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0010, e.station);
                add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0011, e.loc);
                add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0020, e.status);
            add_item_delim(data_buf, &data_len, sizeof(data_buf));
            add_seq_delim(data_buf, &data_len, sizeof(data_buf));
            
            // Requested Procedure Data MUST come strictly after 0040,0100 to maintain DICOM Tag Order
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x1001, e.procID);
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x1002, e.procReason); 
            add_elem_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x1003, e.procPri);

            send_pdata_tf(clientSocket, pc_id, 0x02, data_buf, data_len);
        }
        fclose(f);
    }

    // 3. Send Final Success Response
    uint8_t cmd_buf[256]; int cmd_len = 0;
    add_elem_ul(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0000, 0); // Group 0 Length Placeholder
    add_elem_string(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0002, "1.2.840.10008.5.1.4.31");
    add_elem_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0100, 0x8020);  // 0x8020 = C-FIND-RSP
    add_elem_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0120, msg_id);
    add_elem_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0800, 0x0101);  // No Data
    add_elem_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0900, 0x0000);  // Success
    
    // Set actual Group 0 length
    uint32_t gl = cmd_len - 12;
    cmd_buf[8] = gl & 0xFF; cmd_buf[9] = (gl >> 8) & 0xFF;
    cmd_buf[10] = (gl >> 16) & 0xFF; cmd_buf[11] = (gl >> 24) & 0xFF;
    
    send_pdata_tf(clientSocket, pc_id, 0x03, cmd_buf, cmd_len);
    write_log("C-FIND Finished.");
}

void handle_pdata_tf(SOCKET clientSocket, char* buffer, int bytesRead) {
    if (bytesRead < 14) return;
    uint8_t pc_id = buffer[10];
    uint16_t command = 0, msg_id = 0;

    for(int i = 12; i < bytesRead - 10; i++) {
        if(buffer[i]==0x00 && buffer[i+1]==0x00 && buffer[i+2]==0x00 && buffer[i+3]==0x01 && buffer[i+4]==0x02) {
            command = ((uint8_t)buffer[i+9] << 8) | (uint8_t)buffer[i+8];
        }
        if(buffer[i]==0x00 && buffer[i+1]==0x00 && buffer[i+2]==0x10 && buffer[i+3]==0x01 && buffer[i+4]==0x02) {
            msg_id = ((uint8_t)buffer[i+9] << 8) | (uint8_t)buffer[i+8];
        }
    }

    if (command == 0x0030) handle_c_echo_rq(clientSocket, pc_id, msg_id);
    else if (command == 0x0020) handle_c_find_rq(clientSocket, pc_id, msg_id);
}

// Ensure full TCP frames are read safely to protect against stream fragmentation
int recv_pdu(SOCKET s, uint8_t *buf, int max_len) {
    int total = 0;
    while (total < 6) {
        int r = recv(s, (char*)buf + total, 6 - total, 0);
        if (r <= 0) return r; 
        total += r;
    }
    
    uint32_t pdu_len = ((uint32_t)buf[2] << 24) | ((uint32_t)buf[3] << 16) |
                       ((uint32_t)buf[4] << 8)  | (uint32_t)buf[5];
                       
    if (pdu_len + 6 > (uint32_t)max_len) {
        return -1; 
    }
    
    while (pdu_len > 0) {
        int r = recv(s, (char*)buf + total, pdu_len, 0);
        if (r <= 0) return r;
        total += r;
        pdu_len -= r;
    }
    return total;
}

// =========================================================================
// Main Network Loop
// =========================================================================

int main(int argc, char* argv[]) {
    init_config(argv[0]);
    write_log("--- Starting Robust DICOM SCP ---");
    ensure_csv_exists();

    WSADATA wsaData; 
    SOCKET listenSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    char buffer[BUFFER_SIZE];

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    serverAddr.sin_family = AF_INET; 
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(cfg.port);

    bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "SCP Listening on port %d with AETitle: %s", cfg.port, cfg.aetitle);
    write_log(msg);

    while (1) {
        clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) continue;
        write_log("Modality SCU connected.");

        int bytesRead;
        while ((bytesRead = recv_pdu(clientSocket, (uint8_t*)buffer, BUFFER_SIZE)) > 0) {
            switch ((unsigned char)buffer[0]) {
                case 0x01: handle_association_rq(clientSocket, buffer, bytesRead); break;
                case 0x04: handle_pdata_tf(clientSocket, buffer, bytesRead); break;
                case 0x05: 
                    write_log("Received A-RELEASE-RQ. Disconnecting.");
                    uint8_t rel_rp[] = { 0x06, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 };
                    send(clientSocket, (char*)rel_rp, sizeof(rel_rp), 0);
                    closesocket(clientSocket); 
                    goto NEXT;
            }
        }
    NEXT:;
    }
    
    WSACleanup();
    if (log_fp) fclose(log_fp);
    return 0;
}