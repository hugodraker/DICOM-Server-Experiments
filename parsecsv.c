// Compile with GCC: gcc -O2 -s -o parsecsv.exe parsecsv.c -luser32 -lkernel32
// Compile with TCC: tcc -o parsecsv.exe parsecsv.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>

#define MAX_LINE_LEN 8192
#define MAX_FIELD_LEN 512

// Strips invisible UTF-8 BOM (Byte Order Mark) inserted by Excel/Windows editors
void strip_bom(char* str) {
    if (strlen(str) >= 3 &&
        (unsigned char)str[0] == 0xEF && 
        (unsigned char)str[1] == 0xBB && 
        (unsigned char)str[2] == 0xBF) {
        memmove(str, str + 3, strlen(str) - 2);
    }
}

// Safely trims leading/trailing whitespace and line endings in place
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
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\r' || str[len-1] == '\n')) {
        str[--len] = '\0';
    }
}

// Case-insensitive string equality check
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

// Case-insensitive substring search (zero allocation, stack-safe)
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

// Lightweight CSV Tokenizer: Extracts a specific column index from a line
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

int main(int argc, char* argv[]) {
    FILE* fp;
    char line[MAX_LINE_LEN];
    char header[MAX_LINE_LEN];
    char field_buf[MAX_FIELD_LEN];
    char input_buf[256];
    char* search_term;
    int name_col_idx = -1;
    int col_idx = 0;
    int matches_found = 0;

    // Handle command line argument OR prompt interactively if double-clicked
    if (argc >= 2) {
        search_term = argv[1];
    } else {
        printf("Enter search term: ");
        if (fgets(input_buf, sizeof(input_buf), stdin) == NULL) {
            return 1;
        }
        trim_whitespace(input_buf);
        search_term = input_buf;
    }

    if (strlen(search_term) == 0) {
        fprintf(stderr, "[ERROR] Search term cannot be empty.\n");
        return 1;
    }

    fp = fopen("patients.csv", "rb");
    if (!fp) {
        fprintf(stderr, "[ERROR] Cannot open 'patients.csv'. Ensure it is in the same folder.\n");
        return 1;
    }

    // 1. Read and clean header line
    if (fgets(header, sizeof(header), fp) == NULL) {
        fprintf(stderr, "[ERROR] patients.csv is empty.\n");
        fclose(fp);
        return 1;
    }
    strip_bom(header);
    trim_whitespace(header);

    // 2. Locate exact index of the "Name" column
    while (get_csv_field(header, col_idx, field_buf, sizeof(field_buf))) {
        if (str_iequals(field_buf, "Name")) {
            name_col_idx = col_idx;
            break;
        }
        col_idx++;
    }

    // 3. STAGE 1: Output all raw lines matching the command line string
    printf("\n=== STAGE 1: Matching Raw CSV Lines ===\n");
    while (fgets(line, sizeof(line), fp) != NULL) {
        trim_whitespace(line);
        if (line[0] == '\0') continue;
        
        if (str_icontains(line, search_term)) {
            printf("%s\n", line);
            matches_found++;
        }
    }

    if (matches_found == 0) {
        printf("No lines matched '%s'.\n", search_term);
        fclose(fp);
        return 0;
    }

    // 4. STAGE 2: Pass matching lines to parser and output ONLY the "Name" column
    printf("\n=== STAGE 2: Parsed 'Name' Column After First Output ===\n");
    if (name_col_idx >= 0) {// Compile with GCC: gcc -O2 -s -o parsecsv.exe parsecsv.c
// Compile with TCC: tcc -o parsecsv.exe parsecsv.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>

#define MAX_FIELD_LEN 512

// Case-insensitive string equality check
int str_iequals(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
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

// Safely trims leading and trailing whitespaces/quotes from strings
void trim_whitespace(char* str) {
    char* start = str;
    int len;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n' || *start == '"') {
        start++;
    }
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    len = (int)strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\r' || str[len-1] == '\n' || str[len-1] == '"')) {
        str[--len] = '\0';
    }
}

// Tokenizes a specific column out of a clean CSV line
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

int main(int argc, char* argv[]) {
    FILE* fp;
    char input_buf[256];
    char* search_term;
    char field_buf[MAX_FIELD_LEN];
    int name_col_idx = -1;
    int col_idx = 0;
    int matches_found = 0;

    // 1. Resolve Command Line Input
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

    // 2. Open File and Read Entire Payload Into Buffer
    fp = fopen("patients.csv", "rb");
    if (!fp) {
        fprintf(stderr, "[ERROR] Cannot open 'patients.csv'. Verify it is in this folder.\n");
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    char* raw_buf = (char*)malloc(file_size + 4);
    if (!raw_buf) {
        fprintf(stderr, "[ERROR] Memory allocation failed.\n");
        fclose(fp);
        return 1;
    }

    size_t bytes_read = fread(raw_buf, 1, file_size, fp);
    fclose(fp);
    raw_buf[bytes_read] = '\0';
    raw_buf[bytes_read + 1] = '\0';

    // 3. Flatten & Sanitize Buffers (Destroys UTF-16 Null-bytes and UTF-8 BOM markers)
    char* data = raw_buf;
    long data_len = (long)bytes_read;

    if (data_len >= 2 && (unsigned char)raw_buf[0] == 0xFF && (unsigned char)raw_buf[1] == 0xFE) {
        // Convert UTF-16 LE to clean standard ASCII on the fly
        long j = 0;
        for (long i = 2; i < data_len; i += 2) {
            raw_buf[j++] = (raw_buf[i + 1] == 0) ? raw_buf[i] : '?';
        }
        raw_buf[j] = '\0';
        data_len = j;
    } else if (data_len >= 3 && (unsigned char)raw_buf[0] == 0xEF && (unsigned char)raw_buf[1] == 0xBB && (unsigned char)raw_buf[2] == 0xBF) {
        // Skip UTF-8 BOM Header
        data = raw_buf + 3;
        data_len -= 3;
    }

    // 4. Extract Header Line Adaptively (\r, \n, or \r\n compatible)
    long pos = 0;
    while (pos < data_len && data[pos] != '\r' && data[pos] != '\n') pos++;
    long h_end = pos;
    
    if (pos < data_len && data[pos] == '\r') pos++;
    if (pos < data_len && data[pos] == '\n') pos++;
    long data_start_pos = pos;

    char header[8192];
    long h_len = (h_end > 8191) ? 8191 : h_end;
    memcpy(header, data, h_len);
    header[h_len] = '\0';
    trim_whitespace(header);

    // Identify Name Column Index
    while (get_csv_field(header, col_idx, field_buf, sizeof(field_buf))) {
        if (str_iequals(field_buf, "Name")) {
            name_col_idx = col_idx;
            break;
        }
        col_idx++;
    }

    // 5. STAGE 1: Scan and output all raw matching lines
    printf("\n=== STAGE 1: Matching Raw CSV Lines ===\n");
    pos = data_start_pos;
    while (pos < data_len) {
        long line_start = pos;
        while (pos < data_len && data[pos] != '\r' && data[pos] != '\n') pos++;
        long line_end = pos;
        
        if (pos < data_len && data[pos] == '\r') pos++;
        if (pos < data_len && data[pos] == '\n') pos++;
        
        long len = line_end - line_start;
        if (len <= 0) continue; 

        char line[8192];
        if (len > 8191) len = 8191;
        memcpy(line, &data[line_start], len);
        line[len] = '\0';
        trim_whitespace(line);
        if (strlen(line) == 0) continue;

        if (str_icontains(line, search_term)) {
            printf("%s\n", line);
            matches_found++;
        }
    }

    if (matches_found == 0) {
        printf("No lines matched '%s'.\n", search_term);
        free(raw_buf);
        return 0;
    }

    // 6. STAGE 2: Parse out just the "Name" column from those matches
    printf("\n=== STAGE 2: Parsed 'Name' Column After First Output ===\n");
    if (name_col_idx >= 0) {
        pos = data_start_pos;
        while (pos < data_len) {
            long line_start = pos;
            while (pos < data_len && data[pos] != '\r' && data[pos] != '\n') pos++;
            long line_end = pos;
            
            if (pos < data_len && data[pos] == '\r') pos++;
            if (pos < data_len && data[pos] == '\n') pos++;
            
            long len = line_end - line_start;
            if (len <= 0) continue;

            char line[8192];
            if (len > 8191) len = 8191;
            memcpy(line, &data[line_start], len);
            line[len] = '\0';
            trim_whitespace(line);
            if (strlen(line) == 0) continue;

            if (str_icontains(line, search_term)) {
                if (get_csv_field(line, name_col_idx, field_buf, sizeof(field_buf))) {
                    printf("%s\n", field_buf);
                }
            }
        }
    } else {
        printf("[WARNING] 'Name' column not found in header configuration.\n");
    }

    free(raw_buf);
    return 0;
}
        rewind(fp);
        fgets(header, sizeof(header), fp); // Skip header on second pass
        
        while (fgets(line, sizeof(line), fp) != NULL) {
            trim_whitespace(line);
            if (line[0] == '\0') continue;
            
            if (str_icontains(line, search_term)) {
                if (get_csv_field(line, name_col_idx, field_buf, sizeof(field_buf))) {
                    printf("%s\n", field_buf);
                }
            }
        }
    } else {
        printf("[WARNING] 'Name' column could not be located in header.\n");
    }

    printf("\n=== Complete (%d match(es) processed) ===\n", matches_found);
    fclose(fp);
    return 0;
}