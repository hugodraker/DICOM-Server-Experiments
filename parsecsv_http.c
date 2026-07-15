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

// UTF-8 BOM: EF BB BF
// UTF-16 LE BOM: FF FE
// UTF-16 BE BOM: FE FF

typedef enum {
    ENCODING_UNKNOWN,
    ENCODING_UTF8,
    ENCODING_UTF8_BOM,
    ENCODING_UTF16_LE,
    ENCODING_UTF16_BE,
    ENCODING_ASCII
} Encoding;

// Global CSV data cache
static char* csv_data = NULL;
static long csv_data_len = 0;
static int name_col_idx = -1;
static int id_col_idx = 0;  // Always first column per requirements
static int birthdate_col_idx = -1;
static int sex_col_idx = -1;

// Detect file encoding from BOM
Encoding detect_encoding(const unsigned char* buf, long len) {
    if (len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
        return ENCODING_UTF8_BOM;
    }
    if (len >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) {
        return ENCODING_UTF16_LE;
    }
    if (len >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) {
        return ENCODING_UTF16_BE;
    }
    int has_high_bytes = 0;
    for (long i = 0; i < (len > 1024 ? 1024 : len); i++) {
        if (buf[i] > 127) {
            has_high_bytes = 1;
            break;
        }
    }
    if (has_high_bytes) {
        return ENCODING_UTF8;
    }
    return ENCODING_ASCII;
}

// Convert UTF-16 LE to UTF-8
int utf16le_to_utf8(const wchar_t* input, char* output, int max_output) {
    int out_pos = 0;
    int in_idx = 0;
    
    while (input[in_idx] != L'\0' && out_pos < max_output - 4) {
        wchar_t ch = input[in_idx++];
        
        if (ch < 0x80) {
            output[out_pos++] = (char)ch;
        } else if (ch < 0x800) {
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

// Convert UTF-16 BE to UTF-8
int utf16be_to_utf8(const unsigned char* input, char* output, int max_output) {
    int out_pos = 0;
    int in_idx = 0;
    
    while (in_idx + 1 < max_output * 2) {
        unsigned char hi = input[in_idx++];
        unsigned char lo = input[in_idx++];
        wchar_t ch = (wchar_t)((hi << 8) | lo);
        
        if (ch == 0) break;
        
        if (ch < 0x80) {
            output[out_pos++] = (char)ch;
        } else if (ch < 0x800) {
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

// Trim whitespace
void trim_whitespace(char* str) {
    char* start = str;
    int len;
    
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    len = (int)strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || 
           str[len-1] == '\r' || str[len-1] == '\n')) {
        str[--len] = '\0';
    }
}

// Case-insensitive string equality
int str_iequals(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

// Case-insensitive substring search
int str_icontains(const char* haystack, const char* needle) {
    int i, j;
    if (!*needle) return 1;
    
    for (i = 0; haystack[i] != '\0'; i++) {
        j = 0;
        while (needle[j] != '\0' && haystack[i + j] != '\0' && 
               tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (needle[j] == '\0') return 1;
    }
    return 0;
}

// Extract specific CSV column from a line
int get_csv_field(const char* line, int target_col, char* out_buf, int max_len) {
    int current_col = 0;
    int in_quotes = 0;
    int pos = 0;
    int out_pos = 0;
    
    while (line[pos] != '\0' && current_col <= target_col) {
        char c = line[pos];
        
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ',' && !in_quotes) {
            if (current_col == target_col) {
                break;
            }
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

// Load and convert file to normalized UTF-8
char* load_and_convert_csv(const char* filename, long* data_len) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return NULL;
    
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);
    
    unsigned char* raw_buf = (unsigned char*)malloc(file_size + 4);
    if (!raw_buf) {
        fclose(fp);
        return NULL;
    }
    
    size_t bytes_read = fread(raw_buf, 1, file_size, fp);
    fclose(fp);
    
    if (bytes_read == 0) {
        free(raw_buf);
        return NULL;
    }
    
    raw_buf[bytes_read] = '\0';
    raw_buf[bytes_read + 1] = '\0';
    
    Encoding enc = detect_encoding(raw_buf, bytes_read);
    char* converted = NULL;
    
    printf("[ENCODING DETECTION] ");
    
    switch (enc) {
        case ENCODING_UTF8_BOM:
            printf("UTF-8 with BOM\n");
            converted = (char*)malloc(bytes_read + 1);
            if (converted) {
                memcpy(converted, raw_buf, bytes_read + 1);
                *data_len = bytes_read - 3;
                memmove(converted, converted + 3, bytes_read - 2);
            }
            break;
            
        case ENCODING_UTF8:
            printf("UTF-8 (no BOM)\n");
            converted = (char*)malloc(bytes_read + 1);
            if (converted) {
                memcpy(converted, raw_buf, bytes_read + 1);
                *data_len = bytes_read;
            }
            break;
            
        case ENCODING_UTF16_LE:
            printf("UTF-16 Little Endian\n");
            converted = (char*)malloc(bytes_read + 1);
            if (converted) {
                int written = utf16le_to_utf8((wchar_t*)(raw_buf + 2), converted, bytes_read);
                *data_len = written;
                converted[written] = '\0';
            }
            break;
            
        case ENCODING_UTF16_BE:
            printf("UTF-16 Big Endian\n");
            converted = (char*)malloc(bytes_read + 1);
            if (converted) {
                int written = utf16be_to_utf8(raw_buf + 2, converted, bytes_read);
                *data_len = written;
                converted[written] = '\0';
            }
            break;
            
        default:
            printf("ASCII / Unknown\n");
            converted = (char*)malloc(bytes_read + 1);
            if (converted) {
                memcpy(converted, raw_buf, bytes_read + 1);
                *data_len = bytes_read;
            }
            break;
    }
    
    free(raw_buf);
    return converted;
}

// Find line boundaries
long get_line_end(const char* data, long start, long len) {
    long pos = start;
    while (pos < len && data[pos] != '\r' && data[pos] != '\n') {
        pos++;
    }
    return pos;
}

void skip_newline(const char* data, long* pos, long len) {
    while (*pos < len && (data[*pos] == '\r' || data[*pos] == '\n')) {
        (*pos)++;
    }
}

// =============================================================================
// CSV PARSING FUNCTION - DEDICATED C FUNCTION FOR PATIENT DATA
// =============================================================================

// Initialize CSV parser - reads header and detects column indices
int initialize_csv_parser(void) {
    if (!csv_data) {
        csv_data = load_and_convert_csv("patients.csv", &csv_data_len);
        if (!csv_data || csv_data_len == 0) {
            fprintf(stderr, "[ERROR] Cannot load patients.csv\n");
            return -1;
        }
        
        // Extract header line
        long h_end = get_line_end(csv_data, 0, csv_data_len);
        char header[8192];
        long h_len = (h_end > 8191) ? 8191 : h_end;
        memcpy(header, csv_data, h_len);
        header[h_len] = '\0';
        trim_whitespace(header);
        
        printf("[HEADER]%s\n", header);
        
        // Find column indices dynamically
        int col_idx = 0;
        char field_buf[MAX_FIELD_LEN];
        
        // Name column (case-insensitive search for "Name")
        while (get_csv_field(header, col_idx, field_buf, sizeof(field_buf))) {
            if (str_iequals(field_buf, "Name")) {
                name_col_idx = col_idx;
            } else if (str_iequals(field_buf, "birthdate") || 
                       str_iequals(field_buf, "Birthdate") ||
                       str_iequals(field_buf, "Birth Date") ||
                       str_iequals(field_buf, "DOB") ||
                       str_iequals(field_buf, "Date of Birth")) {
                birthdate_col_idx = col_idx;
            } else if (str_iequals(field_buf, "Sex") || 
                       str_iequals(field_buf, "sex") ||
                       str_iequals(field_buf, "Gender") ||
                       str_iequals(field_buf, "gender")) {
                sex_col_idx = col_idx;
            }
            col_idx++;
        }
        
        printf("[Column Detection] Name=%d, ID=0, Birthdate=%d, Sex=%d\n", 
               name_col_idx, birthdate_col_idx, sex_col_idx);
        
        if (name_col_idx < 0) {
            fprintf(stderr, "[ERROR] 'Name' column not found in CSV header\n");
            free(csv_data);
            csv_data = NULL;
            return -1;
        }
    }
    
    return 0;
}

// Get filtered patient names from CSV (dedicated C function)
// Returns JSON array of names via output buffer
int get_patient_names(const char* filter, char* output, int max_output, int max_count) {
    if (initialize_csv_parser() != 0) {
        strcpy_s(output, max_output, "{\"error\": \"Failed to load CSV\"}");
        return -1;
    }
    
    output[0] = '\0';
    int count = 0;
    long data_start = get_line_end(csv_data, 0, csv_data_len);
    skip_newline(csv_data, &data_start, csv_data_len);
    
    char temp_json[BUFFER_SIZE];
    strcpy_s(temp_json, sizeof(temp_json), "[");
    
    long line_start = data_start;
    while (line_start < csv_data_len && count < max_count) {
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
        
        // Apply filter if provided
        if (filter && strlen(filter) > 0) {
            if (!str_icontains(line, filter)) {
                line_start = line_end;
                skip_newline(csv_data, &line_start, csv_data_len);
                continue;
            }
        }
        
        // Extract name field
        char name_buf[MAX_FIELD_LEN];
        if (get_csv_field(line, name_col_idx, name_buf, sizeof(name_buf))) {
            if (count > 0) {
                strcat_s(temp_json, sizeof(temp_json), ",");
            }
            // Escape quotes in name for JSON
            char escaped_name[MAX_FIELD_LEN * 2];
            int ei = 0;
            for (int ni = 0; name_buf[ni] && ei < MAX_FIELD_LEN * 2 - 1; ni++) {
                if (name_buf[ni] == '"' || name_buf[ni] == '\\') {
                    escaped_name[ei++] = '\\';
                }
                escaped_name[ei++] = name_buf[ni];
            }
            escaped_name[ei] = '\0';
            
            sprintf_s(escaped_name, sizeof(escaped_name), "\"%s\"", escaped_name);
            strcat_s(temp_json, sizeof(temp_json), escaped_name);
            count++;
        }
        
        line_start = line_end;
        skip_newline(csv_data, &line_start, csv_data_len);
    }
    
    strcat_s(temp_json, sizeof(temp_json), "]");
    strcpy_s(output, max_output, temp_json);
    printf("[Patient Names] Returned %d names\n", count);
    return count;
}

// Get full patient details by name
int get_patient_details(const char* patient_name, char* output, int max_output) {
    if (initialize_csv_parser() != 0) {
        strcpy_s(output, max_output, "{\"error\": \"Failed to load CSV\"}");
        return -1;
    }
    
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
        
        // Extract name and check for match
        char name_buf[MAX_FIELD_LEN];
        if (get_csv_field(line, name_col_idx, name_buf, sizeof(name_buf))) {
            if (str_iequals(name_buf, patient_name)) {
                // Found matching patient - extract all fields
                char id_buf[MAX_FIELD_LEN];
                char birthdate_buf[MAX_FIELD_LEN];
                char sex_buf[MAX_FIELD_LEN];
                
                get_csv_field(line, id_col_idx, id_buf, sizeof(id_buf));
                
                if (birthdate_col_idx >= 0) {
                    get_csv_field(line, birthdate_col_idx, birthdate_buf, sizeof(birthdate_buf));
                } else {
                    strcpy_s(birthdate_buf, sizeof(birthdate_buf), "");
                }
                
                if (sex_col_idx >= 0) {
                    get_csv_field(line, sex_col_idx, sex_buf, sizeof(sex_buf));
                } else {
                    strcpy_s(sex_buf, sizeof(sex_buf), "");
                }
                
                // Build JSON response
                sprintf_s(output, max_output, 
                    "{\"patient_id\":\"%s\",\"name\":\"%s\",\"birthdate\":\"%s\",\"sex\":\"%s\"}",
                    id_buf, name_buf, birthdate_buf, sex_buf);
                
                printf("[Patient Details] Found: %s (ID: %s)\n", name_buf, id_buf);
                return 0;
            }
        }
        
        line_start = line_end;
        skip_newline(csv_data, &line_start, csv_data_len);
    }
    
    strcpy_s(output, max_output, "{\"error\": \"Patient not found\"}");
    return -1;
}

// =============================================================================
// HTTP SERVER FUNCTIONS
// =============================================================================

// Send HTTP response
int send_http_response(SOCKET client, const char* status, const char* content_type, 
                       const char* body, int body_len) {
    char response_header[BUFFER_SIZE];
    int header_len = sprintf_s(response_header, sizeof(response_header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, body_len);
    
    send(client, response_header, header_len, 0);
    send(client, body, body_len, 0);
    return 0;
}

// Serve HTML page with LISTBOX (not dropdown) and debounced search
void serve_html_page(SOCKET client) {
    const char* html_content = 
        "<!DOCTYPE html>"
        "<html lang='en'>"
        "<head>"
        "    <meta charset='UTF-8'>"
        "    <meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "    <title>Patient Search</title>"
        "    <style>"
        "        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }"
        "        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }"
        "        h1 { color: #6d4aff; text-align: center; }"
        "        .search-box { margin-bottom: 20px; }"
        "        input[type='text'] { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }"
        "        /* LISTBOX STYLE - NOT DROPDOWN */"
        "        #patientList {"
        "            width: 100%;"
        "            height: 150px;"
        "            padding: 5px;"
        "            border: 1px solid #ddd;"
        "            border-radius: 4px;"
        "            box-sizing: border-box;"
        "            overflow-y: scroll;"
        "            font-size: 14px;"
        "            margin-top: 10px;"
        "        }"
        "        #patientList option:hover {"
        "            background-color: #6d4aff;"
        "            color: white;"
        "        }"
        "        .fields { margin-top: 20px; padding: 15px; background: #f9f9f9; border-radius: 4px; }"
        "        .field-row { margin-bottom: 10px; }"
        "        label { display: inline-block; width: 100px; font-weight: bold; }"
        "        span.field-value { color: #333; }"
        "        .status { margin-top: 5px; font-style: italic; color: #666; font-size: 12px; min-height: 16px; }"
        "    </style>"
        "</head>"
        "<body>"
        "    <div class='container'>"
        "        <h1>Patient Search</h1>"
        "        <div class='search-box'>"
        "            <input type='text' id='filterInput' placeholder='Type to search patients (wait 1 sec)...'>"
        "            <!-- LISTBOX - multiple selection enabled -->"
        "            <select id='patientList' size='7' multiple onchange='loadPatientDetails(getSelectedPatients())'>"
        "                <option value=''>Start typing to search...</option>"
        "            </select>"
        "            <div id='searchStatus' class='status'></div>"
        "        </div>"
        "        <div class='fields'>"
        "            <div class='field-row'><label>Name:</label><span id='displayName' class='field-value'></span></div>"
        "            <div class='field-row'><label>Patient ID:</label><span id='displayId' class='field-value'></span></div>"
        "            <div class='field-row'><label>Birthdate:</label><span id='displayBirthdate' class='field-value'></span></div>"
        "            <div class='field-row'><label>Sex:</label><span id='displaySex' class='field-value'></span></div>"
        "        </div>"
        "    </div>"
        "    <script>"
        "        // Debounce timer - waits 1 second after typing stops"
        "        let debounceTimer = null;"
        "        const DEBOUNCE_DELAY = 1000; // 1 second delay"
        "        "
        "        // Debounced search function - calls C backend after delay"
        "        function filterPatients() {"
        "            const filter = document.getElementById('filterInput').value;"
        "            const statusDiv = document.getElementById('searchStatus');"
        "            statusDiv.textContent = 'Searching...';"
        "            "
        "            // Clear any existing timer"
        "            if (debounceTimer) {"
        "                clearTimeout(debounceTimer);"
        "            }"
        "            "
        "            // Set new timer - only execute after 1 second of no typing"
        "            debounceTimer = setTimeout(function() {"
        "                const url = '/api/patients?filter=' + encodeURIComponent(filter);"
        "                fetch(url)"
        "                    .then(response => response.json())"
        "                    .then(names => {"
        "                        populateListBox(names);"
        "                        if (names.length > 0) {"
        "                            statusDiv.textContent = names.length + ' patient(s) found (max 7)';"
        "                        } else {"
        "                            statusDiv.textContent = 'No patients found';"
        "                        }"
        "                    })"
        "                    .catch(e => {"
        "                        console.error('Error fetching patients:', e);"
        "                        statusDiv.textContent = 'Error searching patients';"
        "                    });"
        "            }, DEBOUNCE_DELAY);"
        "        }"
        "        "
        "        // Populate LISTBOX from C backend JSON response"
        "        function populateListBox(names) {"
        "            const select = document.getElementById('patientList');"
        "            select.innerHTML = '';"
        "            "
        "            if (names.length === 0) {"
        "                const option = document.createElement('option');"
        "                option.value = '';"
        "                option.textContent = 'No matches found';"
        "                select.appendChild(option);"
        "                return;"
        "            }"
        "            "
        "            // Listbox limited to maximum 7 results"
        "            names.slice(0, 7).forEach(function(name) {"
        "                const option = document.createElement('option');"
        "                option.value = name;"
        "                option.textContent = name;"
        "                select.appendChild(option);"
        "            });"
        "        }"
        "        "
        "        // Helper to get selected patient names from listbox"
        "        function getSelectedPatients() {"
        "            const select = document.getElementById('patientList');"
        "            const selected = [];"
        "            for (let i = 0; i < select.options.length; i++) {"
        "                if (select.options[i].selected) {"
        "                    selected.push(select.options[i].value);"
        "                }"
        "            }"
        "            return selected;"
        "        }"
        "        "
        "        // Load patient details for clicked patient"
        "        async function loadPatientDetails(selectedPatients) {"
        "            if (!selectedPatients || selectedPatients.length === 0) {"
        "                document.getElementById('displayName').textContent = '';"
        "                document.getElementById('displayId').textContent = '';"
        "                document.getElementById('displayBirthdate').textContent = '';"
        "                document.getElementById('displaySex').textContent = '';"
        "                return;"
        "            }"
        "            "
        "            // Load first selected patient"
        "            const patientName = selectedPatients[0];"
        "            if (!patientName) return;"
        "            "
        "            const url = '/api/patient/' + encodeURIComponent(patientName);"
        "            try {"
        "                const response = await fetch(url);"
        "                const data = await response.json();"
        "                if (data.error) {"
        "                    console.error('Error:', data.error);"
        "                } else {"
        "                    document.getElementById('displayName').textContent = data.name;"
        "                    document.getElementById('displayId').textContent = data.patient_id;"
        "                    document.getElementById('displayBirthdate').textContent = data.birthdate;"
        "                    document.getElementById('displaySex').textContent = data.sex;"
        "                }"
        "            } catch (e) {"
        "                console.error('Error loading patient details:', e);"
        "            }"
        "        }"
        "        "
        "        // Attach event listener - NO immediate action on typing"
        "        document.getElementById('filterInput').addEventListener('input', filterPatients);"
        "        "
        "        // Initial load - show placeholder"
        "        window.onload = function() {"
        "            populateListBox([]);"
        "            document.getElementById('searchStatus').textContent = 'Type to begin searching...';"
        "        };"
        "    </script>"
        "</body>"
        "</html>";
    
    send_http_response(client, "200 OK", "text/html", html_content, (int)strlen(html_content));
}

// Parse URL query parameters
void get_query_param(const char* url, const char* param_name, char* value, int max_value) {
    value[0] = '\0';
    
    const char* start = strstr(url, param_name);
    if (!start) return;
    
    start += strlen(param_name);
    if (*start != '=') return;
    start++;
    
    while (*start && *start != '&' && *start != '#') {
        if (*start == '+') {
            *value++ = ' ';
        } else if (*start == '%') {
            // Handle URL encoding (simplified)
            start++;
            if (*start && *(start+1)) {
                char hex[3] = {*start, *(start+1), 0};
                *value = (char)strtol(hex, NULL, 16);
                start += 2;
                value++;
            }
        } else {
            *value++ = *start;
        }
        start++;
    }
    *value = '\0';
}

// Route HTTP request
void route_request(SOCKET client, const char* request_line) {
    char method[16] = {0};
    char path[512] = {0};
    char query[256] = {0};
    
    sscanf(request_line, "%15s %511s", method, path);
    
    // Separate path and query string
    char* qmark = strchr(path, '?');
    if (qmark) {
        *qmark = '\0';
        strcpy_s(query, sizeof(query), qmark + 1);
    }
    
    printf("[REQUEST] %s %s\n", method, path);
    
    // Route based on path
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        // Serve HTML page
        serve_html_page(client);
    }
    else if (strncmp(path, "/api/patients", 13) == 0) {
        // Get patient names with optional filter
        char filter[256] = {0};
        get_query_param(path, "filter", filter, sizeof(filter));
        
        char json_response[BUFFER_SIZE];
        int count = get_patient_names(filter, json_response, sizeof(json_response), 7);
        
        if (count >= 0) {
            send_http_response(client, "200 OK", "application/json", json_response, (int)strlen(json_response));
        } else {
            send_http_response(client, "500 Internal Server Error", "application/json", 
                "{\"error\": \"Server error\"}", 22);
        }
    }
    else if (strncmp(path, "/api/patient/", 13) == 0) {
        // Get patient details by name
        char* encoded_name = path + 13;
        
        char json_response[BUFFER_SIZE];
        int result = get_patient_details(encoded_name, json_response, sizeof(json_response));
        
        if (result == 0) {
            send_http_response(client, "200 OK", "application/json", json_response, (int)strlen(json_response));
        } else {
            send_http_response(client, "404 Not Found", "application/json", json_response, (int)strlen(json_response));
        }
    }
    else {
        send_http_response(client, "404 Not Found", "text/plain", "Not Found", 9);
    }
}

// Main server loop
int run_server(void) {
    WSADATA wsaData;
    SOCKET server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    int addr_len = sizeof(client_addr);
    
    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "[ERROR] WSAStartup failed\n");
        return 1;
    }
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        fprintf(stderr, "[ERROR] Socket creation failed\n");
        WSACleanup();
        return 1;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    // Bind socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] Bind failed (port %d may be in use)\n", SERVER_PORT);
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    
    // Listen
    if (listen(server_fd, 10) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] Listen failed\n");
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    
    printf("\n========================================\n");
    printf("HTTP Server Started\n");
    printf("Open http://localhost:%d in your browser\n", SERVER_PORT);
    printf("Press Ctrl+C to stop\n");
    printf("========================================\n\n");
    
    // Accept connections
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd == INVALID_SOCKET) {
            fprintf(stderr, "[WARNING] Accept failed\n");
            continue;
        }
        
        // Receive request
        char request_buffer[BUFFER_SIZE];
        int received = recv(client_fd, request_buffer, sizeof(request_buffer) - 1, 0);
        
        if (received > 0) {
            request_buffer[received] = '\0';
            
            // Parse first line of request
            char request_line[512];
            sscanf(request_buffer, "%511[^\\r\\n]", request_line);
            
            // Route the request
            route_request(client_fd, request_line);
        }
        
        // Close client connection
        closesocket(client_fd);
    }
    
    // Cleanup (never reached in normal operation)
    closesocket(server_fd);
    WSAStartupCleanup();
    return 0;
}

int main(int argc, char* argv[]) {
    // Initialize CSV parser first
    if (initialize_csv_parser() != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize CSV parser\n");
        return 1;
    }
    
    // Run HTTP server
    return run_server();
}