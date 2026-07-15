// Compile with GCC: gcc -O2 -s -o parsecsv.exe parsecsv.c -luser32 -lkernel32
// Compile with TCC: tcc -o parsecsv.exe parsecsv.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>

#define MAX_LINE_LEN 8192
#define MAX_FIELD_LEN 512

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
    // Check if ASCII-like (no high bytes)
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

// Convert UTF-16 BE to UTF-8 (swap byte order first)
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

// Strip BOM markers (in-place)
void strip_bom(char* str, long* len) {
    if (*len >= 3 && (unsigned char)str[0] == 0xEF && 
        (unsigned char)str[1] == 0xBB && 
        (unsigned char)str[2] == 0xBF) {
        memmove(str, str + 3, *len - 2);
        *len -= 3;
    }
}

// Safely trim whitespace
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

// Find line boundaries (handles \r, \n, \r\n)
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

int main(int argc, char* argv[]) {
    char input_buf[256];
    char* search_term;
    char field_buf[MAX_FIELD_LEN];
    int name_col_idx = -1;
    int col_idx = 0;
    int matches_found = 0;
    long data_len = 0;
    
    // 1. Get search term
    if (argc >= 2) {
        search_term = argv[1];
    } else {
        printf("Enter search term: ");
        if (fgets(input_buf, sizeof(input_buf), stdin) == NULL) return 1;
        trim_whitespace(input_buf);
        search_term = input_buf;
    }
    
    if (strlen(search_term) == 0) {
        fprintf(stderr, "[ERROR] Search term cannot be empty.\n");
        return 1;
    }
    
    printf("Searching patients.csv for: '%s'\n\n", search_term);
    
    // 2. Load and convert file
    char* data = load_and_convert_csv("patients.csv", &data_len);
    if (!data) {
        fprintf(stderr, "[ERROR] Cannot open 'patients.csv'. Verify file exists.\n");
        return 1;
    }
    
    if (data_len == 0) {
        printf("[INFO] File is empty.\n");
        free(data);
        return 0;
    }
    
    // 3. Extract header line
    long pos = 0;
    long h_end = get_line_end(data, 0, data_len);
    long data_start = h_end;
    skip_newline(data, &data_start, data_len);
    
    char header[8192];
    long h_len = (h_end > 8191) ? 8191 : h_end;
    memcpy(header, data, h_len);
    header[h_len] = '\0';
    trim_whitespace(header);
    
    printf("[HEADER]%s\n", header);
    
    // 4. Find Name column index
    while (get_csv_field(header, col_idx, field_buf, sizeof(field_buf))) {
        if (str_iequals(field_buf, "Name")) {
            name_col_idx = col_idx;
            break;
        }
        col_idx++;
    }
    
    if (name_col_idx >= 0) {
        printf("[SUCCESS] Found 'Name' column at index %d\n\n", name_col_idx);
    } else {
        printf("[WARNING] 'Name' column not found! Available columns:\n");
        col_idx = 0;
        while (get_csv_field(header, col_idx, field_buf, sizeof(field_buf))) {
            printf("  [%d] %s\n", col_idx++, field_buf);
        }
        printf("\n");
    }
    
    // 5. STAGE 1: Output all matching raw lines
    printf("=== STAGE 1: Matching Raw CSV Lines ===\n");
    
    long line_start = data_start;
    while (line_start < data_len) {
        long line_end = get_line_end(data, line_start, data_len);
        
        if (line_end <= line_start) {
            line_start = line_end + 1;
            skip_newline(data, &line_start, data_len);
            continue;
        }
        
        long len = line_end - line_start;
        if (len > 8191) len = 8191;
        
        char line[8192];
        memcpy(line, &data[line_start], len);
        line[len] = '\0';
        trim_whitespace(line);
        
        if (strlen(line) > 0 && str_icontains(line, search_term)) {
            printf("%s\n", line);
            matches_found++;
        }
        
        line_start = line_end;
        skip_newline(data, &line_start, data_len);
    }
    
    if (matches_found == 0) {
        printf("No lines matched '%s'.\n\n", search_term);
        free(data);
        return 0;
    }
    
    // 6. STAGE 2: Parse Name column from matching lines
    printf("\n=== STAGE 2: Parsed 'Name' Column ===\n");
    
    if (name_col_idx >= 0) {
        line_start = data_start;
        while (line_start < data_len) {
            long line_end = get_line_end(data, line_start, data_len);
            
            if (line_end <= line_start) {
                line_start = line_end + 1;
                skip_newline(data, &line_start, data_len);
                continue;
            }
            
            long len = line_end - line_start;
            if (len > 8191) len = 8191;
            
            char line[8192];
            memcpy(line, &data[line_start], len);
            line[len] = '\0';
            trim_whitespace(line);
            
            if (strlen(line) > 0 && str_icontains(line, search_term)) {
                if (get_csv_field(line, name_col_idx, field_buf, sizeof(field_buf))) {
                    printf("%s\n", field_buf);
                }
            }
            
            line_start = line_end;
            skip_newline(data, &line_start, data_len);
        }
    } else {
        printf("[SKIPPED] No 'Name' column to extract.\n");
    }
    
    printf("\n=== Complete (%d match(es) processed) ===\n", matches_found);
    
    free(data);
    return 0;
}