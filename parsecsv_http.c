// Compile with GCC: gcc -O2 -s -o parsecsv_http.exe parsecsv_http.c -lws2_32 -luser32 -lkernel32
// Compile with TCC: tcc -o parsecsv_http.exe parsecsv_http.c -lws2_32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_LINE_LEN 8192
#define MAX_FIELD_LEN 512
#define SERVER_PORT 8080
#define BUFFER_SIZE 65536

typedef enum {
    ENCODING_UNKNOWN,
    ENCODING_UTF8,
    ENCODING_UTF8_BOM,
    ENCODING_UTF16_LE,
    ENCODING_UTF16_BE,
    ENCODING_ASCII
} Encoding;

static char* csv_data = NULL;
static long csv_data_len = 0;
static int name_col_idx = -1;
static int id_col_idx = 0;
static int birthdate_col_idx = -1;
static int sex_col_idx = -1;

void url_decode(const char* src, char* dest, int max_len) {
    int out = 0;
    for (int i = 0; src[i] && out < max_len - 1; i++) {
        if (src[i] == '+') {
            dest[out++] = ' ';
        } else if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = { src[i+1], src[i+2], 0 };
            dest[out++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            dest[out++] = src[i];
        }
    }
    dest[out] = '\0';
}

Encoding detect_encoding(const unsigned char* buf, long len) {
    if (len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) return ENCODING_UTF8_BOM;
    if (len >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) return ENCODING_UTF16_LE;
    if (len >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) return ENCODING_UTF16_BE;
    for (long i = 0; i < (len > 1024 ? 1024 : len); i++) {
        if (buf[i] > 127) return ENCODING_UTF8;
    }
    return ENCODING_ASCII;
}

int utf16le_to_utf8(const wchar_t* input, char* output, int max_output) {
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

int utf16be_to_utf8(const unsigned char* input, char* output, int max_output) {
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

void trim_whitespace(char* str) {
    char* start = str;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != str) memmove(str, start, strlen(start) + 1);
    int len = (int)strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\r' || str[len-1] == '\n')) {
        str[--len] = '\0';
    }
}

int str_iequals(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

int str_icontains(const char* haystack, const char* needle) {
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

int get_csv_field(const char* line, int target_col, char* out_buf, int max_len) {
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
    trim_whitespace(out_buf);
    return (current_col == target_col);
}

char* load_and_convert_csv(const char* filename, long* data_len) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);
    
    unsigned char* raw_buf = (unsigned char*)malloc(file_size + 4);
    if (!raw_buf) { fclose(fp); return NULL; }
    size_t bytes_read = fread(raw_buf, 1, file_size, fp);
    fclose(fp);
    if (bytes_read == 0) { free(raw_buf); return NULL; }
    
    raw_buf[bytes_read] = '\0';
    Encoding enc = detect_encoding(raw_buf, bytes_read);
    char* converted = (char*)malloc(bytes_read + 1);
    
    printf("[ENCODING DETECTION] ");
    switch (enc) {
        case ENCODING_UTF8_BOM:
            printf("UTF-8 with BOM\n");
            memcpy(converted, raw_buf, bytes_read + 1);
            *data_len = bytes_read - 3;
            memmove(converted, converted + 3, bytes_read - 2);
            break;
        case ENCODING_UTF8:
            printf("UTF-8\n");
            memcpy(converted, raw_buf, bytes_read + 1);
            *data_len = bytes_read;
            break;
        case ENCODING_UTF16_LE:
            printf("UTF-16 LE\n");
            *data_len = utf16le_to_utf8((wchar_t*)(raw_buf + 2), converted, bytes_read);
            break;
        case ENCODING_UTF16_BE:
            printf("UTF-16 BE\n");
            *data_len = utf16be_to_utf8(raw_buf + 2, converted, bytes_read);
            break;
        default:
            printf("ASCII / Unknown\n");
            memcpy(converted, raw_buf, bytes_read + 1);
            *data_len = bytes_read;
            break;
    }
    free(raw_buf);
    return converted;
}

long get_line_end(const char* data, long start, long len) {
    long pos = start;
    while (pos < len && data[pos] != '\r' && data[pos] != '\n') pos++;
    return pos;
}

void skip_newline(const char* data, long* pos, long len) {
    while (*pos < len && (data[*pos] == '\r' || data[*pos] == '\n')) (*pos)++;
}

int initialize_csv_parser(void) {
    if (!csv_data) {
        csv_data = load_and_convert_csv("patients.csv", &csv_data_len);
        if (!csv_data || csv_data_len == 0) {
            fprintf(stderr, "[ERROR] Cannot load patients.csv\n");
            return -1;
        }
        long h_end = get_line_end(csv_data, 0, csv_data_len);
        char header[8192];
        long h_len = (h_end > 8191) ? 8191 : h_end;
        memcpy(header, csv_data, h_len);
        header[h_len] = '\0';
        trim_whitespace(header);
        
        printf("[HEADER]%s\n", header);
        
        int col_idx = 0;
        char field_buf[MAX_FIELD_LEN];
        while (get_csv_field(header, col_idx, field_buf, sizeof(field_buf))) {
            if (str_iequals(field_buf, "Name")) name_col_idx = col_idx;
            else if (str_iequals(field_buf, "Birthdate") || str_iequals(field_buf, "DOB")) birthdate_col_idx = col_idx;
            else if (str_iequals(field_buf, "Sex") || str_iequals(field_buf, "Gender")) sex_col_idx = col_idx;
            col_idx++;
        }
        
        printf("[Column Detection] Name=%d, ID=0, Birthdate=%d, Sex=%d\n", name_col_idx, birthdate_col_idx, sex_col_idx);
        if (name_col_idx < 0) return -1;
    }
    return 0;
}

int get_patient_names(const char* filter, char* output, int max_output, int max_count) {
    if (initialize_csv_parser() != 0) {
        snprintf(output, max_output, "{\"error\": \"Failed to load CSV\"}");
        return -1;
    }
    
    output[0] = '\0';
    int count = 0;
    long data_start = get_line_end(csv_data, 0, csv_data_len);
    skip_newline(csv_data, &data_start, csv_data_len);
    
    snprintf(output, max_output, "[");
    long line_start = data_start;
    
    while (line_start < csv_data_len && (max_count == 0 || count < max_count)) {
        long line_end = get_line_end(csv_data, line_start, csv_data_len);
        if (line_end <= line_start) {
            line_start = line_end + 1;
            skip_newline(csv_data, &line_start, csv_data_len);
            continue;
        }
        
        long len = line_end - line_start;
        if (len > 8191) len = 8191;
        
        char line[8192];
        memcpy(line, &csv_data[line_start], len);
        line[len] = '\0';
        trim_whitespace(line);
        
        char name_buf[MAX_FIELD_LEN];
        
        // 1. Extract the name FIRST
        if (get_csv_field(line, name_col_idx, name_buf, sizeof(name_buf))) {
            
            // 2. Filter STRICTLY against the extracted name, not the whole raw CSV line
            if (filter && strlen(filter) > 0) {
                if (!str_icontains(name_buf, filter)) {
                    line_start = line_end;
                    skip_newline(csv_data, &line_start, csv_data_len);
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
        skip_newline(csv_data, &line_start, csv_data_len);
    }
    
    strncat(output, "]", max_output - strlen(output) - 1);
    return count;
}

int get_patient_details(const char* patient_name, char* output, int max_output) {
    if (initialize_csv_parser() != 0) return -1;
    
    long data_start = get_line_end(csv_data, 0, csv_data_len);
    skip_newline(csv_data, &data_start, csv_data_len);
    
    long line_start = data_start;
    while (line_start < csv_data_len) {
        long line_end = get_line_end(csv_data, line_start, csv_data_len);
        if (line_end <= line_start) {
            line_start = line_end + 1;
            skip_newline(csv_data, &line_start, csv_data_len);
            continue;
        }
        
        long len = line_end - line_start;
        if (len > 8191) len = 8191;
        
        char line[8192];
        memcpy(line, &csv_data[line_start], len);
        line[len] = '\0';
        trim_whitespace(line);
        
        char name_buf[MAX_FIELD_LEN];
        if (get_csv_field(line, name_col_idx, name_buf, sizeof(name_buf))) {
            if (str_iequals(name_buf, patient_name)) {
                char id_buf[MAX_FIELD_LEN] = {0}, birthdate_buf[MAX_FIELD_LEN] = {0}, sex_buf[MAX_FIELD_LEN] = {0};
                get_csv_field(line, id_col_idx, id_buf, sizeof(id_buf));
                if (birthdate_col_idx >= 0) get_csv_field(line, birthdate_col_idx, birthdate_buf, sizeof(birthdate_buf));
                if (sex_col_idx >= 0) get_csv_field(line, sex_col_idx, sex_buf, sizeof(sex_buf));
                
                snprintf(output, max_output, "{\"patient_id\":\"%s\",\"name\":\"%s\",\"birthdate\":\"%s\",\"sex\":\"%s\"}", id_buf, name_buf, birthdate_buf, sex_buf);
                return 0;
            }
        }
        line_start = line_end;
        skip_newline(csv_data, &line_start, csv_data_len);
    }
    snprintf(output, max_output, "{\"error\": \"Patient not found\"}");
    return -1;
}

int send_http_response(SOCKET client, const char* status, const char* content_type, const char* body, int body_len) {
    char response_header[1024];
    int header_len = snprintf(response_header, sizeof(response_header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, body_len);
    
    send(client, response_header, header_len, 0);
    send(client, body, body_len, 0);
    return 0;
}

void serve_html_page(SOCKET client) {
    const char* html_content = 
        "<!DOCTYPE html>"
        "<html lang='en'>"
        "<head>"
        "    <meta charset='UTF-8'>"
        "    <title>Patient Directory</title>"
        "    <style>"
        "        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }"
        "        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }"
        "        .search-box { margin-bottom: 20px; }"
        "        input[type='text'] { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }"
        "        #patientList { width: 100%; height: 200px; padding: 5px; border: 1px solid #ddd; border-radius: 4px; overflow-y: auto; font-size: 14px; margin-top: 10px; }"
        "        #patientList option { padding: 4px 8px; }"
        "        #patientList option:hover { background-color: #6d4aff; color: white; cursor: pointer; }"
        "        .fields { margin-top: 20px; padding: 15px; background: #f9f9f9; border-radius: 4px; }"
        "        .field-row { margin-bottom: 10px; }"
        "        label { display: inline-block; width: 100px; font-weight: bold; }"
        "        input.field-value { width: calc(100% - 105px); padding: 8px; border: 1px solid #ccc; border-radius: 4px; }"
        "        .status { margin-top: 5px; font-style: italic; color: #666; font-size: 12px; min-height: 16px; }"
        "    </style>"
        "</head>"
        "<body>"
        "    <div class='container'>"
        "        <div class='search-box'>"
        "            <input type='text' id='filterInput' placeholder='Type to filter patients...'>"
        "            <select id='patientList' size='10' onchange='loadPatientDetails(this.value)' onclick='loadPatientDetails(this.value)'></select>"
        "            <div id='searchStatus' class='status'></div>"
        "        </div>"
        "        <div class='fields'>"
        "            <div class='field-row'><label>Name:</label><input id='displayName' class='field-value' type='text'></div>"
        "            <div class='field-row'><label>Patient ID:</label><input id='displayId' class='field-value' type='text'></div>"
        "            <div class='field-row'><label>Birthdate:</label><input id='displayBirthdate' class='field-value' type='text'></div>"
        "            <div class='field-row'><label>Sex:</label><input id='displaySex' class='field-value' type='text'></div>"
        "        </div>"
        "    </div>"
"    <script>"
        "        let debounceTimer = null;"
        "        let searchAbortController = null;"
        "        const DEBOUNCE_DELAY = 250;"
        "        "
        "        async function filterPatients() {"
        "            const filter = document.getElementById('filterInput').value.trim();"
        "            const statusDiv = document.getElementById('searchStatus');"
        "            statusDiv.textContent = 'Searching...';"
        "            "
        "            if (debounceTimer) clearTimeout(debounceTimer);"
        "            "
        "            debounceTimer = setTimeout(async () => {"
        "                if (searchAbortController) {"
        "                    searchAbortController.abort();"
        "                }"
        "                searchAbortController = new AbortController();"
        "                "
        "                const url = '/api/patients?filter=' + encodeURIComponent(filter);"
        "                try {"
        "                    const response = await fetch(url, { signal: searchAbortController.signal });"
        "                    if (!response.ok) throw new Error('HTTP ' + response.status);"
        "                    "
        "                    const names = await response.json();"
        "                    populateListBox(names);"
        "                    "
        "                    statusDiv.textContent = names.length > 0 "
        "                        ? (filter ? names.length + ' patient(s) found' : names.length + ' patient(s) loaded') "
        "                        : 'No patients found';"
        "                } catch (e) {"
        "                    if (e.name === 'AbortError') return;"
        "                    console.error('Search error:', e);"
        "                    statusDiv.textContent = 'Error searching patients';"
        "                }"
        "            }, DEBOUNCE_DELAY);"
        "        }"
        "        "
        "        function populateListBox(names) {"
        "            const select = document.getElementById('patientList');"
        "            select.options.length = 0;"
        "            "
        "            if (!names || names.length === 0) {"
        "                const option = document.createElement('option');"
        "                option.value = ''; "
        "                option.textContent = 'No matches found'; "
        "                option.disabled = true;"
        "                select.appendChild(option);"
        "                return;"
        "            }"
        "            "
        "            names.forEach(name => {"
        "                const option = document.createElement('option');"
        "                option.value = name; "
        "                option.textContent = name;"
        "                select.appendChild(option);"
        "            });"
        "        }"
        "        "
        "        async function loadPatientDetails(patientName) {"
        "            if (!patientName) return;"
        "            const url = '/api/patient/' + encodeURIComponent(patientName);"
        "            try {"
        "                const response = await fetch(url);"
        "                if (!response.ok) throw new Error('HTTP ' + response.status);"
        "                const data = await response.json();"
        "                if (!data.error) {"
        "                    document.getElementById('displayName').value = data.name || '';"
        "                    document.getElementById('displayId').value = data.patient_id || '';"
        "                    document.getElementById('displayBirthdate').value = data.birthdate || '';"
        "                    document.getElementById('displaySex').value = data.sex || '';"
        "                }"
        "            } catch (e) {"
        "                console.error('Error loading details:', e);"
        "            }"
        "        }"
        "        "
        "        document.getElementById('filterInput').addEventListener('input', filterPatients);"
        "        window.onload = filterPatients;"
        "    </script>"
        "</body>"
        "</html>";
    
    send_http_response(client, "200 OK", "text/html", html_content, (int)strlen(html_content));
}

void get_query_param(const char* url, const char* param_name, char* value, int max_value) {
    value[0] = '\0';
    const char* start = strstr(url, param_name);
    if (!start) return;
    start += strlen(param_name);
    if (*start != '=') return;
    start++;
    
    char raw_val[256] = {0};
    int i = 0;
    while (*start && *start != '&' && *start != '#' && i < sizeof(raw_val) - 1) {
        raw_val[i++] = *start++;
    }
    raw_val[i] = '\0';
    url_decode(raw_val, value, max_value);
}

// Route HTTP request
void route_request(SOCKET client, const char* request_line) {
    char method[16] = {0};
    char path[512] = {0};
    char query[256] = {0};
    
    sscanf(request_line, "%15s %511s", method, path);
    
    char* qmark = strchr(path, '?');
    if (qmark) {
        *qmark = '\0';
        snprintf(query, sizeof(query), "%s", qmark + 1);
    }
    
    printf("[REQUEST] %s %s\n", method, path);
    
    // 1. HTML Page Route
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        serve_html_page(client);
    }
    // 2. PATIENT DETAILS ROUTE (Must be checked FIRST and require the trailing slash)
    else if (strncmp(path, "/api/patient/", 13) == 0) {
        char* raw_name = path + 13;
        char decoded_name[512] = {0};
        url_decode(raw_name, decoded_name, sizeof(decoded_name));
        
        char* json_response = (char*)malloc(BUFFER_SIZE);
        if (!json_response) return;
        
        int result = get_patient_details(decoded_name, json_response, BUFFER_SIZE);
        
        if (result == 0) {
            send_http_response(client, "200 OK", "application/json", json_response, (int)strlen(json_response));
        } else {
            send_http_response(client, "404 Not Found", "application/json", json_response, (int)strlen(json_response));
        }
        free(json_response);
    }
    // 3. PATIENT LIST / SEARCH ROUTE (Plural)
    else if (strncmp(path, "/api/patients", 13) == 0) {
        char filter[256] = {0};
        get_query_param(query, "filter", filter, sizeof(filter));
        
        char* json_response = (char*)malloc(BUFFER_SIZE);
        if (!json_response) return;
        
        int max_results = (strlen(filter) > 0) ? 50 : 10;
        int count = get_patient_names(filter, json_response, BUFFER_SIZE, max_results);
        
        if (count >= 0) {
            send_http_response(client, "200 OK", "application/json", json_response, (int)strlen(json_response));
        } else {
            send_http_response(client, "500 Internal Server Error", "application/json", 
                "{\"error\": \"Server error\"}", 22);
        }
        free(json_response);
    }
    // 4. Fallback 404
    else {
        send_http_response(client, "404 Not Found", "text/plain", "Not Found", 9);
    }
}

int run_server(void) {
    WSADATA wsaData;
    SOCKET server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    int addr_len = sizeof(client_addr);
    
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) { WSACleanup(); return 1; }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(server_fd); WSACleanup(); return 1;
    }
    
    if (listen(server_fd, 10) == SOCKET_ERROR) {
        closesocket(server_fd); WSACleanup(); return 1;
    }
    
    printf("\n========================================\n");
    printf("HTTP Server Started\n");
    printf("Open http://localhost:%d in your browser\n", SERVER_PORT);
    printf("Press Ctrl+C to stop\n");
    printf("========================================\n\n");
    
    char* request_buffer = (char*)malloc(BUFFER_SIZE);
    if (!request_buffer) { closesocket(server_fd); WSACleanup(); return 1; }
    
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd == INVALID_SOCKET) continue;
        
        int received = recv(client_fd, request_buffer, BUFFER_SIZE - 1, 0);
        if (received > 0) {
            request_buffer[received] = '\0';
            char request_line[512];
            sscanf(request_buffer, "%511[^\r\n]", request_line);
            route_request(client_fd, request_line);
        }
        
        shutdown(client_fd, SD_SEND);
        closesocket(client_fd);
    }
    
    free(request_buffer);
    closesocket(server_fd);
    WSACleanup();
    return 0;
}

int main(int argc, char* argv[]) {
    if (initialize_csv_parser() != 0) return 1;
    return run_server();
}