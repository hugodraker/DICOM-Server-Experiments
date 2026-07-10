/* WORKLIST_ADDCSV.c — RIS Multi-Protocol Server
 * Converted from MASM32 assembly to C.
 * Compile with TCC or GCC (MinGW) targeting Windows.
 *
 * This work is not fit for any function or purpose, comes with no warranty,
 * and is being released into the public domain.
 * # 32-bit (matching original MASM32 target)
 * gcc -m32 -o WORKLIST_ADDCSV.exe WORKLIST_ADDCSV.c -lws2_32 -luser32 -lshell32 -lkernel32 -lgdi32
 * 
 * # 64-bit (MinGW-w64)
 * gcc -m64 -o WORKLIST_ADDCSV.exe WORKLIST_ADDCSV.c -lws2_32 -luser32 -lshell32 -lkernel32 -lgdi32
 * 
 * # With warnings enabled during development
 * gcc -Wall -Wextra -o WORKLIST_ADDCSV.exe WORKLIST_ADDCSV.c -lws2_32 -luser32 -lshell32 -lkernel32 -lgdi32
 * 
 * TCC (Tiny C Compiler)
 * tcc -o WORKLIST_ADDCSV.exe WORKLIST_ADDCSV.c -lws2_32 -luser32 -lshell32 -lkernel32 -lgdi32
 * 
 * If TCC can't find the libraries automatically, specify the lib path:
 * 
 * tcc -o WORKLIST_ADDCSV.exe WORKLIST_ADDCSV.c -L"C:/tcc/lib" -lws2_32 -luser32 -lshell32 -kernel32 -lgdi32
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "kernel32.lib")

/* ---- Constants ---- */
#define MAX_CLIENTS          32
#define RECV_SIZE           2048
#define CLIENT_BUF_SIZE     8192
#define FIELD_COUNT         23
#define FIELD_SIZE          256
#define ENTRY_SIZE          (FIELD_COUNT * FIELD_SIZE)
#define LINE_SIZE           16384
#define APPEND_DELAY_MS     2000
#define DEFAULT_TIMEOUT_MS  10000
#define FIONBIO_VAL        0x8004667E
#define WSAEWOULDBLOCK_VAL  10035

#define WM_TRAYICON         (WM_USER + 1)
#define ID_TRAY_TOGGLE      1000
#define ID_TRAY_SHOW        1001
#define ID_TRAY_EXIT        1002

/* ---- Globals ---- */
static int g_TelnetPort       = 23;
static int g_HttpPort         = 80;
static int g_TelnetTimeout    = 10;
static int g_DebugLog         = 1;
static int g_bRunning         = 0;
static SOCKET g_listenSocket  = INVALID_SOCKET;
static SOCKET g_listenSocketHttp = INVALID_SOCKET;

static HINSTANCE g_hInstance  = NULL;
static HWND     g_hMainWnd    = NULL;
static HMENU    g_hMenu       = NULL;

static char g_aeCalled[17]    = "AUTOIT_SCP";

static char szIniFile[MAX_PATH];

/* filenames */
static const char *szCSVFile     = "patients.csv";
static const char *szCSVTempFile = "patients.tmp";
static const char *szProcCodesFile = "procedurecodes.csv";
static const char *szRoomsFile   = "rooms.csv";

/* ini keys */
static const char *szIniServer        = "Server";
static const char *szIniAET           = "AETitle";
static const char *szIniTelnetPort    = "TelnetPort";
static const char *szIniHttpPort      = "HttpPort";
static const char *szIniTelnetTimeout = "TelnetTimeout";
static const char *szIniDebugLog      = "DebugLog";

/* defaults */
static const char *szDefAET           = "AUTOIT_SCP";
static const char *szDefTelnetPort    = "23";
static const char *szDefHttpPort      = "80";
static const char *szDefTelnetTimeout = "10";
static const char *szDefDebug         = "1";

/* strings */
static const char *szWndClass  = "WorklistTrayClass";
static const char *szTrayTip   = "RIS Multi-Protocol Server";
static const char *szMenuStart = "Start Server";
static const char *szMenuStop  = "Stop Server";
static const char *szMenuShow  = "Show Console";
static const char *szMenuExit  = "Exit";

static const char *szStartup =
    "RIS Server starting...\r\n";
static const char *szConfigFmt =
    "Config: AET=%s TelnetPort=%u HttpPort=%u Timeout=%u DebugLog=%u\r\n";
static const char *szListenFmt =
    "Telnet listening on port %u\r\n";
static const char *szListenHttpFmt =
    "HTTP listening on port %u\r\n";
static const char *szListenFailFmt =
    "ERROR: Failed to listen on port %u\r\n";
static const char *szClientConnFmt =
    "Telnet Client connected on socket %u\r\n";
static const char *szHttpConnFmt =
    "HTTP Client connected on socket %u\r\n";
static const char *szClientDiscFmt =
    "Client disconnected on socket %u\r\n";
static const char *szTimeoutFmt =
    "Client on socket %u timed out\r\n";
static const char *szPendingFmt =
    "PENDING Accession %s\r\n";
static const char *szInsertedFmt =
    "INSERTED Accession %s\r\n";
static const char *szUpdatedFmt =
    "UPDATED Accession %s\r\n";

static const char *szConnected =
    "Connected to RIS Telnet Server. Waiting for data...\r\n";
static const char *szPendingResp  = "PENDING\r\n";
static const char *szInsertedResp = "INSERTED\r\n";
static const char *szUpdatedResp  = "UPDATED\r\n";
static const char *szInvalidResp  = "INVALID LINE\r\n";
static const char *szServerStarted = "Server STARTED\r\n";
static const char *szServerStopped = "Server STOPPED\r\n";

static const char *szCSVHeader =
    "PatientName,PatientID,BirthDate,Sex,ReqPhys,ReqSvc,ProcDesc,Reason,"
    "Accession,ProcID,Priority,StationAE,StartDate,StartTime,PerfPhys,"
    "SPSDesc,SPSID,StationName,Location,Status,Modality,RefPhys,StudyDesc";
static const char *szFakePatient =
    "Jane Smith,P87654321,19920522,F,Dr. Brown,MRI,Brain MRI,Headache,"
    "ACC00002,PROC2,2,RIS,20260709,11:45,Dr. White,SPS2,SPSID2,Station2,"
    "Room2,1,MR,Dr. Black,Study 2";

static const char *szHttp200OK =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";

static NOTIFYICONDATA nid;

/* ---- Client arrays ---- */
static SOCKET g_clients[MAX_CLIENTS];
static int    g_clientIDs[MAX_CLIENTS];
static int    g_clientType[MAX_CLIENTS];     /* 0=Telnet, 1=HTTP */
static DWORD  g_clientLastTick[MAX_CLIENTS];
static DWORD  g_clientTimeout[MAX_CLIENTS];
static int    g_clientBufLen[MAX_CLIENTS];
static char   g_clientBuffers[MAX_CLIENTS * CLIENT_BUF_SIZE];
static int    g_pendingFlag[MAX_CLIENTS];
static DWORD  g_pendingSince[MAX_CLIENTS];
static char   g_pendingEntries[MAX_CLIENTS * ENTRY_SIZE];

static char   g_recvBuf[RECV_SIZE];
static char   g_fileLine[LINE_SIZE];
static char   g_lineOut[LINE_SIZE];
static char   g_iniBuf[256];
static char   g_dispParam[512];

/* ---- HTML blocks ---- */
static const char *szHtmlStart =
"<html><head><title>RIS Worklist Manager</title><style>"
"table{border-collapse:collapse;width:100%;font-size:14px;box-shadow:0 0 20px rgba(0,0,0,0.15);}"
"th,td{border:1px solid #dddddd;padding:12px 15px;}th{background:#009879;color:#ffffff;text-align:left;}"
"tr{border-bottom:1px solid #dddddd;}tr:nth-child(even){background-color:#f3f3f3;}"
"input[type='text'],input[type='date'],input[type='time']{margin-bottom:10px;padding:5px;width:180px;margin-right:10px;}"
"select{margin-bottom:10px;padding:5px;margin-right:10px;}</style>"
"<script>var fnames=['PatientName','PatientID','BirthDate','Sex','ReqPhys','ReqSvc','ProcDesc',"
"'Reason','Accession','ProcID','Priority','StationAE','StartDate','StartTime','PerfPhys','SPSDesc','SPSID','StationName',"
"'Location','Status','Modality','RefPhys','StudyDesc'];"
"function f(r){var c=r.cells;for(var i=0;i<c.length&&i<fnames.length;i++){var e=document.getElementsByName(fnames[i])[0];if(e)e.value=(c[i].innerText||c[i].textContent).trim();}}"
"function sendMan(){var c=[];for(var i=0;i<fnames.length;i++){var v=document.getElementsByName(fnames[i])[0].value||'';"
"if(v.indexOf(',')>-1||v.indexOf('\"')>-1)v='\"'+v.replace(new RegExp('\"','g'),'\"\"')+'\"';c.push(v);}"
"fetch('/manual',{method:'POST',body:c.join(',')}).then(function(){window.location.href='/';});return false;}"
"function sortTable(n){var t,r,sw,i,x,y,sd,d,sc=0;t=document.getElementById('pTable');sw=true;d='asc';"
"while(sw){sw=false;r=t.rows;for(i=1;i<(r.length-1);i++){sd=false;x=r[i].getElementsByTagName('TD')[n];"
"y=r[i+1].getElementsByTagName('TD')[n];if(d=='asc'){if(x.innerHTML.toLowerCase()>y.innerHTML.toLowerCase()){sd=true;break;}}"
"else{if(x.innerHTML.toLowerCase()<y.innerHTML.toLowerCase()){sd=true;break;}}}if(sd){r[i].parentNode.insertBefore(r[i+1],r[i]);sw=true;sc++;}"
"else{if(sc==0&&d=='asc'){d='desc';sw=true;}}}}"
"function formatDates(){var t=document.getElementById('pTable');if(!t)return;for(var i=1;i<t.rows.length;i++){"
"var c=t.rows[i].cells;if(c.length>12){var b=c[2].innerText;if(b.length===8&&b.indexOf('-')===-1)c[2].innerText=b.substr(0,4)+'-'+b.substr(4,2)+'-'+b.substr(6,2);"
"var s=c[12].innerText;if(s.length===8&&s.indexOf('-')===-1)c[12].innerText=s.substr(0,4)+'-'+s.substr(4,2)+'-'+s.substr(6,2);}}}"
"function nxtA(){var t=document.getElementById('pTable'),m=0,p='ACC',c=5;if(t){for(var i=1;i<t.rows.length;i++){"
"var v=t.rows[i].cells[8].innerText||'',mt=v.match(/([^0-9]*)([0-9]+)/);if(mt){p=mt[1];var val=parseInt(mt[2],10);"
"if(val>m){m=val;c=mt[2].length;}}}}m++;document.getElementsByName('Accession')[0].value=p+String(m).padStart(c,'0');}"
"window.onload=function(){var d=new Date(),y=d.getFullYear(),mo=('0'+(d.getMonth()+1)).slice(-2),da=('0'+d.getDate()).slice(-2);"
"var sd=document.getElementsByName('StartDate')[0];if(sd&&!sd.value)sd.value=y+'-'+mo+'-'+da;};"
"</script></head><body style='font-family: Arial, sans-serif; padding: 20px; color: #333;'>";

static const char *szHtmlHeader = "<h1>RIS Worklist Manager</h1>";

static const char *szHtmlForms =
"<h3>Upload patients.csv</h3>"
"<form action='/upload' method='POST' enctype='multipart/form-data'>"
"<input type='file' name='csvfile'> <input type='submit' value='Upload CSV'></form>"
"<form action='/delete' method='POST'><input type='submit' value='Delete Data' style='color:red;'></form>"
"<hr><h3>Manual Input</h3>"
"<form id='manForm' onsubmit='return sendMan()'>"
"<input type='text' name='PatientName' placeholder='Patient Name'> "
"<input type='text' name='PatientID' placeholder='Patient ID'> "
"Birth Date: <input type='date' name='BirthDate'> "
"<select name='Sex'><option value=''>Sex...</option><option value='M'>M</option><option value='F'>F</option><option value='O'>O</option></select><br>"
"<input type='text' name='ReqPhys' placeholder='Req Phys'> "
"<input type='text' name='ReqSvc' placeholder='Req Svc'> "
"<input type='text' name='ProcDesc' placeholder='Proc Desc'> "
"<input type='text' name='Reason' placeholder='Reason'><br>"
"<input type='text' name='Accession' placeholder='Accession Number'> "
"<input type='text' name='ProcID' placeholder='Proc ID'> "
"<select name='Priority'><option value='0' selected>LOW</option><option value='1'>MEDIUM</option><option value='2'>ROUTINE</option><option value='3'>HIGH</option></select> "
"<input type='text' name='StationAE' placeholder='Station AE'><br>"
"Start Date: <input type='date' name='StartDate'> "
"Start Time: <input type='time' name='StartTime' value='16:00'> "
"<input type='text' name='PerfPhys' placeholder='Perf Phys'> "
"<input type='text' name='SPSDesc' placeholder='SPS Desc'><br>"
"<input type='text' name='SPSID' placeholder='SPS ID'> "
"<input type='text' name='StationName' placeholder='Station Name'> "
"<input type='text' name='Location' placeholder='Location'> "
"<select name='Status'><option value='0'>NONE</option><option value='1' selected>SCHEDULED</option><option value='2'>ARRIVED</option>"
"<option value='3'>READY</option><option value='4'>STARTED</option><option value='5'>DEPARTED</option></select><br>"
"<input type='text' name='Modality' placeholder='Modality'> "
"<input type='text' name='RefPhys' placeholder='Ref Phys'> "
"<input type='text' name='StudyDesc' placeholder='Study Desc'><br>"
"<input type='submit' id='btnAddUpdate' value='Add/Edit Patient'> "
"<button type='button' onclick='nxtA()'>Next Accession</button></form><hr><h3>Current Database</h3>";

static const char *szHtmlEnd = "</body></html>";

/* ---- Forward declarations ---- */
static void RemoveClient(int idx);
static int  IsDateRangeParam(const char *pParam);
static void ProcessHttpClient(int clientIdx, char *pBuf);
static void StripDashes(char *pStr);
static void SendText(SOCKET sock, const char *pText);

/* ---- Helper: field pointer ---- */
static char *GetFieldPtr(char *pEntry, int fieldIdx) {
    return pEntry + (fieldIdx * FIELD_SIZE);
}

static char *GetClientBufferPtr(int idx) {
    return g_clientBuffers + (idx * CLIENT_BUF_SIZE);
}

static char *GetPendingEntryPtr(int idx) {
    return g_pendingEntries + (idx * ENTRY_SIZE);
}

/* ---- ResolveIniPath ---- */
static void ResolveIniPath(void) {
    GetModuleFileName(NULL, szIniFile, MAX_PATH);
    size_t len = strlen(szIniFile);
    if (len > 4 && szIniFile[len-4] == '.') {
        szIniFile[len-3] = 'i';
        szIniFile[len-2] = 'n';
        szIniFile[len-1] = 'i';
    }
}

/* ---- SetAETitle ---- */
static void SetAETitle(const char *pSrc) {
    memset(g_aeCalled, ' ', 16);
    g_aeCalled[16] = '\0';
    size_t nLen = strlen(pSrc);
    if (nLen > 16) nLen = 16;
    memcpy(g_aeCalled, pSrc, nLen);
}

/* ---- LoadConfig ---- */
static void LoadConfig(void) {
    GetPrivateProfileString(szIniServer, szIniAET, szDefAET,
        g_iniBuf, 256, szIniFile);
    SetAETitle(g_iniBuf);

    g_TelnetPort = GetPrivateProfileInt(szIniServer, szIniTelnetPort, 23, szIniFile);
    g_HttpPort   = GetPrivateProfileInt(szIniServer, szIniHttpPort, 80, szIniFile);
    g_TelnetTimeout = GetPrivateProfileInt(szIniServer, szIniTelnetTimeout, 10, szIniFile);
    g_DebugLog   = GetPrivateProfileInt(szIniServer, szIniDebugLog, 1, szIniFile);

    WritePrivateProfileString(szIniServer, szIniAET, g_aeCalled, szIniFile);
    WritePrivateProfileString(szIniServer, szIniTelnetPort, szDefTelnetPort, szIniFile);
    WritePrivateProfileString(szIniServer, szIniHttpPort, szDefHttpPort, szIniFile);
    WritePrivateProfileString(szIniServer, szIniTelnetTimeout, szDefTelnetTimeout, szIniFile);
    WritePrivateProfileString(szIniServer, szIniDebugLog, szDefDebug, szIniFile);
}

/* ---- SendText ---- */
static void SendText(SOCKET sock, const char *pText) {
    int nLen = (int)strlen(pText);
    if (nLen > 0) send(sock, pText, nLen, 0);
}

/* ---- String helpers ---- */
static int IsWhite(char ival) {
    return ival == ' ' || ival == '\t' || ival == '\r' || ival == '\n';
}

static void TrimInPlace(char *pStr) {
    char *p = pStr;
    while (*p && IsWhite(*p)) p++;
    if (p != pStr) memmove(pStr, p, strlen(p)+1);
    size_t len = strlen(pStr);
    while (len > 0 && IsWhite(pStr[len-1])) pStr[--len] = '\0';
}

static void StripDashes(char *pStr) {
    char *dst = pStr;
    while (*pStr) {
        if (*pStr != '-') *dst++ = *pStr;
        pStr++;
    }
    *dst = '\0';
}

static char UpperChar(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static int StartsWithDISP(const char *pLine) {
    return (UpperChar(pLine[0]) == 'D' &&
            UpperChar(pLine[1]) == 'I' &&
            UpperChar(pLine[2]) == 'S' &&
            UpperChar(pLine[3]) == 'P');
}

static int StrIEquals(const char *a, const char *b) {
    while (UpperChar(*a) == UpperChar(*b)) {
        if (*a == '\0') return 1;
        a++; b++;
    }
    return 0;
}

static int IsEightDigits(const char *pStr) {
    if (*pStr == '\0') return 0;
    int i;
    for (i = 0; i < 8; i++) {
        if (pStr[i] < '0' || pStr[i] > '9') return 0;
    }
    if (pStr[8] != '\0') return 0;
    return 1;
}

/* ---- ParseCSVLineToEntry ---- */
static int ParseCSVLineToEntry(const char *pLine, char *pEntry) {
    memset(pEntry, 0, ENTRY_SIZE);
    int fieldIdx = 0;
    int charIdx  = 0;
    int quoteState = 0;
    char *pDest = pEntry;
    const char *si = pLine;

    while (*si && *si != '\r' && *si != '\n') {
        char charCode = *si;
        if (charCode == '~') charCode = '"';

        if (charCode == '"') {
            quoteState = !quoteState;
            si++;
            continue;
        }

        if (charCode == ',' && !quoteState) {
            *(pDest + charIdx) = '\0';
            fieldIdx++;
            if (fieldIdx >= FIELD_COUNT) break;
            pDest += FIELD_SIZE;
            charIdx = 0;
            si++;
            continue;
        }

        if (charIdx < FIELD_SIZE - 1) {
            *(pDest + charIdx) = charCode;
            charIdx++;
        }
        si++;
    }

    *(pDest + charIdx) = '\0';

    /* Trim all fields */
    int fi;
    for (fi = 0; fi < FIELD_COUNT; fi++)
        TrimInPlace(GetFieldPtr(pEntry, fi));

    return 1;
}

/* ---- ValidateEntry ---- */
static int ValidateEntry(char *pEntry) {
    char *acc = GetFieldPtr(pEntry, 8);
    if (*acc == '\0') return 0;
    return 1;
}

/* ---- NeedsQuote ---- */
static int NeedsQuote(const char *pField) {
    while (*pField) {
        if (*pField == ',' || *pField == '"') return 1;
        pField++;
    }
    return 0;
}

/* ---- BuildCSVLineFromEntry ---- */
static void BuildCSVLineFromEntry(char *pEntry, char *pOut) {
    int fieldIdx;
    char *pCur = pOut;

    for (fieldIdx = 0; fieldIdx < FIELD_COUNT; fieldIdx++) {
        if (fieldIdx > 0) *pCur++ = ',';
        char *pField = GetFieldPtr(pEntry, fieldIdx);
        int q = NeedsQuote(pField);
        if (q) *pCur++ = '"';
        for (const char *s = pField; *s; s++) {
            if (q && *s == '"') *pCur++ = '"';
            *pCur++ = *s;
        }
        if (q) *pCur++ = '"';
    }
    *pCur = '\0';
}

/* ---- CreateCSVIfMissing ---- */
static void CreateCSVIfMissing(void) {
    FILE *f = fopen(szCSVFile, "r");
    if (f) { fclose(f); return; }
    f = fopen(szCSVFile, "w");
    if (!f) return;
    fprintf(f, "%s\r\n", szCSVHeader);
    fprintf(f, "%s\r\n", szFakePatient);
    fclose(f);
}

/* ---- AccessionEquals ---- */
static int AccessionEquals(const char *pLine, const char *pAccession) {
    char tempEntry[ENTRY_SIZE];
    ParseCSVLineToEntry(pLine, tempEntry);
    char *pField = GetFieldPtr(tempEntry, 8);
    return strcmp(pField, pAccession) == 0;
}

/* ---- UpdateCsvByPatient ---- */
static int UpdateCsvByPatient(char *pEntry, const char *pLineOut) {
    int found = 0;
    CreateCSVIfMissing();
    char *pAcc = GetFieldPtr(pEntry, 8);

    FILE *hIn  = fopen(szCSVFile, "r");
    FILE *hOut = fopen(szCSVTempFile, "w");
    if (!hOut) {
        if (hIn) fclose(hIn);
        return 0;
    }

    if (!hIn || !fgets(g_fileLine, LINE_SIZE, hIn)) {
        fprintf(hOut, "%s\r\n", szCSVHeader);
        if (hIn) goto UCP_ReadLoop_;
        goto UCP_Insert;
    }
    fputs(g_fileLine, hOut);

UCP_ReadLoop_:
    while (fgets(g_fileLine, LINE_SIZE, hIn)) {
        if (!found && AccessionEquals(g_fileLine, pAcc)) {
            fprintf(hOut, "%s\r\n", pLineOut);
            found = 1;
        } else {
            fputs(g_fileLine, hOut);
        }
    }
    if (hIn) fclose(hIn);

UCP_Insert:
    if (!found)
        fprintf(hOut, "%s\r\n", pLineOut);
    fclose(hOut);
    remove(szCSVFile);
    rename(szCSVTempFile, szCSVFile);
    return found;
}

/* ---- ExtractDispParam ---- */
static void ExtractDispParam(const char *pLine, char *pOut) {
    const char *si = pLine + 4;
    while (*si == ' ') si++;
    char *di = pOut;
    while (*si && *si != '\r' && *si != '\n' && *si != ',')
        *di++ = *si++;
    *di = '\0';
    TrimInPlace(pOut);
}

/* ---- IsDateRangeParam ---- */
static int IsDateRangeParam(const char *pParam) {
    if (pParam[8] == ' ' && pParam[17] == '\0') return 1;
    return 0;
}

/* ---- LineMatchesDisp ---- */
static int LineMatchesDisp(const char *pLine, const char *pParam) {
    if (pParam[0] == '\0') return 1;

    char tempEntry[ENTRY_SIZE];
    ParseCSVLineToEntry(pLine, tempEntry);

    if (IsEightDigits(pParam)) {
        char *pField = GetFieldPtr(tempEntry, 1);
        return strcmp(pField, pParam) == 0 ? 1 : 0;
    }

    if (IsDateRangeParam(pParam)) {
        char *pField = GetFieldPtr(tempEntry, 12);
        if (strncmp(pField, pParam, 8) < 0) return 0;
        if (strncmp(pField, pParam + 9, 8) > 0) return 0;
        return 1;
    }

    char *pField = GetFieldPtr(tempEntry, 20);
    if (StrIEquals(pField, pParam)) return 1;
    return 0;
}

/* ---- ProcessDispCommand ---- */
static void ProcessDispCommand(int clientIdx, const char *pLine) {
    ExtractDispParam(pLine, g_dispParam);
    FILE *f = fopen(szCSVFile, "r");
    if (!f) return;
    fgets(g_fileLine, LINE_SIZE, f); /* skip header */
    while (fgets(g_fileLine, LINE_SIZE, f)) {
        if (LineMatchesDisp(g_fileLine, g_dispParam))
            SendText(g_clients[clientIdx], g_fileLine);
    }
    fclose(f);
}

/* ---- CommitPendingEntry ---- */
static void CommitPendingEntry(int clientIdx) {
    char *pEntry = GetPendingEntryPtr(clientIdx);
    BuildCSVLineFromEntry(pEntry, g_lineOut);
    int found = UpdateCsvByPatient(pEntry, g_lineOut);
    char *pAcc = GetFieldPtr(pEntry, 8);
    SOCKET sock = g_clients[clientIdx];

    if (found) {
        SendText(sock, szUpdatedResp);
        printf(szUpdatedFmt, pAcc);
    } else {
        SendText(sock, szInsertedResp);
        printf(szInsertedFmt, pAcc);
    }
    g_pendingFlag[clientIdx]  = 0;
    g_pendingSince[clientIdx] = 0;
}

/* ---- ProcessClientLine ---- */
static void ProcessClientLine(int clientIdx, char *pLine) {
    char entry[ENTRY_SIZE];
    TrimInPlace(pLine);
    if (*pLine == '\0') return;

    if (StartsWithDISP(pLine)) {
        ProcessDispCommand(clientIdx, pLine);
        return;
    }

    if (!ParseCSVLineToEntry(pLine, entry) || !ValidateEntry(entry)) {
        SendText(g_clients[clientIdx], szInvalidResp);
        return;
    }

    char *pPending = GetPendingEntryPtr(clientIdx);
    memcpy(pPending, entry, ENTRY_SIZE);
    g_pendingFlag[clientIdx] = 1;
    g_pendingSince[clientIdx] = GetTickCount();
    g_clientTimeout[clientIdx] += 1000;
    SendText(g_clients[clientIdx], szPendingResp);
    printf(szPendingFmt, GetFieldPtr(pPending, 8));
}

/* ---- ProcessHttpClient ---- */
static void ProcessHttpClient(int clientIdx, char *pBuf) {
    SOCKET sock = g_clients[clientIdx];
    char *headerEnd = strstr(pBuf, "\r\n\r\n");

    if (!headerEnd) return; /* wait for more data */

    char firstChar = pBuf[0];

    if (firstChar == 'G') {
        /* Serve UI */
        SendText(sock, szHttp200OK);
        SendText(sock, szHtmlStart);
        SendText(sock, szHtmlHeader);
        SendText(sock, szHtmlForms);

        /* ProcedureCodes integration */
        FILE *hf = fopen(szProcCodesFile, "r");
        if (hf) {
            SendText(sock, "<script>var pcsv=[");
            while (fgets(g_fileLine, LINE_SIZE, hf)) {
                TrimInPlace(g_fileLine);
                if (g_fileLine[0] == '\0') continue;
                SendText(sock, "`");
                SendText(sock, g_fileLine);
                SendText(sock, "`,");
            }
            SendText(sock, "];");
            SendText(sock, "var s=document.createElement('select');s.innerHTML='<option value=0>Select Code...</option>';");
            SendText(sock, "for(var i=1;i<pcsv.length;i++){var p=pcsv[i].split(',');if(p.length>=4)s.innerHTML+='<option value='+i+'>'+p[3]+'</option>';}");
            SendText(sock, "s.onchange=function(){if(this.value==0)return;var p=pcsv[this.value].split(',');");
            SendText(sock, "document.getElementsByName('ProcID')[0].value=p[0];document.getElementsByName('SPSID')[0].value=p[1];");
            SendText(sock, "document.getElementsByName('Modality')[0].value=p[2];document.getElementsByName('ProcDesc')[0].value=p[3];};");
            SendText(sock, "var inp=document.getElementsByName('ProcDesc')[0];inp.parentNode.insertBefore(s,inp.nextSibling);</script>");
            fclose(hf);
        }

        /* Rooms integration */
        hf = fopen(szRoomsFile, "r");
        if (hf) {
            SendText(sock, "<script>var rcsv=[");
            while (fgets(g_fileLine, LINE_SIZE, hf)) {
                TrimInPlace(g_fileLine);
                if (g_fileLine[0] == '\0') continue;
                SendText(sock, "`");
                SendText(sock, g_fileLine);
                SendText(sock, "`,");
            }
            SendText(sock, "];");
            SendText(sock, "var sr=document.createElement('select');sr.innerHTML='<option value=0>Select Room...</option>';");
            SendText(sock, "for(var i=1;i<rcsv.length;i++){var p=rcsv[i].split(',');if(p.length>=5)sr.innerHTML+='<option value='+i+'>'+p[1]+'</option>';}");
            SendText(sock, "sr.onchange=function(){if(this.value==0)return;var p=rcsv[this.value].split(',');");
            SendText(sock, "document.getElementsByName('Location')[0].value=p[0];document.getElementsByName('StationName')[0].value=p[4];");
            SendText(sock, "document.getElementsByName('Modality')[0].value=p[2];document.getElementsByName('StationAE')[0].value=p[3];};");
            SendText(sock, "var inpr=document.getElementsByName('StationName')[0];inpr.parentNode.insertBefore(sr,inpr.nextSibling);</script>");
            fclose(hf);
        }

        /* Main table */
        hf = fopen(szCSVFile, "r");
        if (hf) {
            int isHeader = 1;
            SendText(sock, "<table id='pTable'>");
            while (fgets(g_fileLine, LINE_SIZE, hf)) {
                char entry[ENTRY_SIZE];
                ParseCSVLineToEntry(g_fileLine, entry);

                if (isHeader)
                    SendText(sock, "<tr>");
                else
                    SendText(sock, "<tr onclick='f(this)' style='cursor:pointer;'>");

                int fi;
                for (fi = 0; fi < FIELD_COUNT; fi++) {
                    if (isHeader)
                        SendText(sock, "<th onclick='sortTable(this.cellIndex)' style='cursor:pointer;' title='Click to Sort'>");
                    else
                        SendText(sock, "<td>");

                    SendText(sock, GetFieldPtr(entry, fi));

                    if (isHeader)
                        SendText(sock, "</th>");
                    else
                        SendText(sock, "</td>");
                }
                SendText(sock, "</tr>");
                isHeader = 0;
            }
            SendText(sock, "</table><script>formatDates();</script>");
            fclose(hf);
        }

        SendText(sock, szHtmlEnd);
        RemoveClient(clientIdx);
        return;
    }

    if (firstChar == 'P') {
        /* POST handling */
        char *body = strstr(pBuf, "\r\n\r\n");
        if (!body) { RemoveClient(clientIdx); return; }
        body += 4;

        if (strstr(pBuf, "POST /delete")) {
            FILE *hf = fopen(szCSVFile, "w");
            if (hf) {
                fprintf(hf, "%s\r\n", szCSVHeader);
                fclose(hf);
            }
        } else if (strstr(pBuf, "POST /manual")) {
            if (!body[0]) { RemoveClient(clientIdx); return; }
            char entry[ENTRY_SIZE];
            memset(entry, 0, ENTRY_SIZE);
            ParseCSVLineToEntry(body, entry);
            StripDashes(GetFieldPtr(entry, 2));
            StripDashes(GetFieldPtr(entry, 12));
            if (ValidateEntry(entry)) {
                BuildCSVLineFromEntry(entry, g_lineOut);
                UpdateCsvByPatient(entry, g_lineOut);
            }
        } else if (strstr(pBuf, "POST /upload")) {
            char *csvStart = strstr(pBuf, szCSVHeader);
            if (csvStart) {
                char *si = csvStart;
                /* skip header line */
                while (*si && *si != '\n') si++;
                if (*si) si++;

                while (*si) {
                    if (*si == '-') break;
                    if (*si == '\r' || *si == '\n') { si++; continue; }

                    /* copy one line */
                    char *di = g_fileLine;
                    int ci = 0;
                    while (*si && *si != '\r' && *si != '\n' &&
                           ci < LINE_SIZE - 1) {
                        *di++ = *si++;
                        ci++;
                    }
                    *di = '\0';

                    /* skip remaining CR/LF */
                    while (*si == '\r' || *si == '\n') si++;

                    if (g_fileLine[0] == '\0') continue;
                    if (g_fileLine[0] == '-') break;

                    char entry[ENTRY_SIZE];
                    ParseCSVLineToEntry(g_fileLine, entry);
                    if (ValidateEntry(entry)) {
                        BuildCSVLineFromEntry(entry, g_lineOut);
                        UpdateCsvByPatient(entry, g_lineOut);
                    }
                }
            }
        }
        /* Redirect back */
        SendText(sock, szHttp200OK);
        SendText(sock, szHtmlStart);
        SendText(sock, "<meta http-equiv='refresh' content='0; url=/' /><h2>Action Processed. Returning to main screen...</h2>");
        SendText(sock, szHtmlEnd);
        RemoveClient(clientIdx);
        return;
    }

    RemoveClient(clientIdx);
}

/* ---- CheckPendingAndTimeout ---- */
static void CheckPendingAndTimeout(int clientIdx) {
    SOCKET sock = g_clients[clientIdx];
    if (sock == 0) return;

    DWORD nowTick = GetTickCount();

    if (g_pendingFlag[clientIdx] == 1) {
        DWORD since = g_pendingSince[clientIdx];
        if (nowTick - since >= APPEND_DELAY_MS) {
            CommitPendingEntry(clientIdx);
            g_clientLastTick[clientIdx] = GetTickCount();
        }
    }

    DWORD lastTick = g_clientLastTick[clientIdx];
    DWORD timeoutVal = g_clientTimeout[clientIdx];
    if (nowTick - lastTick >= timeoutVal) {
        printf(szTimeoutFmt, (unsigned)sock);
        RemoveClient(clientIdx);
    }
}

/* ---- AppendReceivedBytes ---- */
static void AppendReceivedBytes(int clientIdx, const char *pBytes, int cbBytes) {
    char *pBuf = GetClientBufferPtr(clientIdx);
    int curLen = g_clientBufLen[clientIdx];
    int isHttp = g_clientType[clientIdx];
    int idx = 0;

    while (idx < cbBytes) {
        char ch = pBytes[idx];

        if (isHttp) {
            if (curLen < CLIENT_BUF_SIZE - 1)
                pBuf[curLen++] = ch;
            idx++;
        } else {
            if (ch == '\r') { idx++; continue; }
            if (ch == '\n') {
                pBuf[curLen] = '\0';
                ProcessClientLine(clientIdx, pBuf);
                curLen = 0;
                memset(pBuf, 0, CLIENT_BUF_SIZE);
                idx++;
                continue;
            }
            if (curLen < CLIENT_BUF_SIZE - 1)
                pBuf[curLen++] = ch;
            idx++;
        }
    }

    g_clientBufLen[clientIdx] = curLen;

    if (isHttp) {
        pBuf[curLen] = '\0';
        ProcessHttpClient(clientIdx, pBuf);
    }
}

/* ---- PollClient ---- */
static void PollClient(int clientIdx) {
    SOCKET sock = g_clients[clientIdx];
    if (sock == 0 || sock == INVALID_SOCKET) return;

    int cb = recv(sock, g_recvBuf, RECV_SIZE, 0);
    if (cb > 0) {
        g_clientLastTick[clientIdx] = GetTickCount();
        AppendReceivedBytes(clientIdx, g_recvBuf, cb);
    } else if (cb == 0) {
        RemoveClient(clientIdx);
        return;
    } else {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK_VAL) {
            RemoveClient(clientIdx);
            return;
        }
    }

    if (g_clientType[clientIdx] != 1) {
        CheckPendingAndTimeout(clientIdx);
    }
}

/* ---- InitArrays ---- */
static void InitArrays(void) {
    memset(g_clients, 0, sizeof(g_clients));
    memset(g_clientIDs, 0, sizeof(g_clientIDs));
    memset(g_clientType, 0, sizeof(g_clientType));
    memset(g_clientLastTick, 0, sizeof(g_clientLastTick));
    memset(g_clientTimeout, 0, sizeof(g_clientTimeout));
    memset(g_clientBufLen, 0, sizeof(g_clientBufLen));
    memset(g_pendingFlag, 0, sizeof(g_pendingFlag));
    memset(g_pendingSince, 0, sizeof(g_pendingSince));
}

/* ---- SetNonBlocking ---- */
static void SetNonBlocking(SOCKET sock) {
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
}

/* ---- StartTelnetServer ---- */
static int StartTelnetServer(void) {
    struct sockaddr_in sin;
    g_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocket == INVALID_SOCKET) {
        printf(szListenFailFmt, g_TelnetPort);
        return 0;
    }
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((u_short)g_TelnetPort);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_listenSocket, (struct sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) {
        printf(szListenFailFmt, g_TelnetPort);
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
        return 0;
    }
    if (listen(g_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        printf(szListenFailFmt, g_TelnetPort);
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
        return 0;
    }
    SetNonBlocking(g_listenSocket);
    printf(szListenFmt, g_TelnetPort);
    return 1;
}

/* ---- StartHttpServer ---- */
static int StartHttpServer(void) {
    struct sockaddr_in sin;
    g_listenSocketHttp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocketHttp == INVALID_SOCKET) {
        printf(szListenFailFmt, g_HttpPort);
        return 0;
    }
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((u_short)g_HttpPort);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_listenSocketHttp, (struct sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) {
        printf(szListenFailFmt, g_HttpPort);
        closesocket(g_listenSocketHttp);
        g_listenSocketHttp = INVALID_SOCKET;
        return 0;
    }
    if (listen(g_listenSocketHttp, SOMAXCONN) == SOCKET_ERROR) {
        printf(szListenFailFmt, g_HttpPort);
        closesocket(g_listenSocketHttp);
        g_listenSocketHttp = INVALID_SOCKET;
        return 0;
    }
    SetNonBlocking(g_listenSocketHttp);
    printf(szListenHttpFmt, g_HttpPort);
    return 1;
}

/* ---- FindFreeClientSlot ---- */
static int FindFreeClientSlot(void) {
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] == 0) return i;
    return -1;
}

/* ---- AddClient ---- */
static void AddClient(SOCKET sock, int clientType) {
    int idx = FindFreeClientSlot();
    if (idx == -1) { closesocket(sock); return; }

    SetNonBlocking(sock);
    g_clients[idx] = sock;
    g_clientIDs[idx] = (int)sock;
    g_clientType[idx] = clientType;
    g_clientBufLen[idx] = 0;
    memset(GetClientBufferPtr(idx), 0, CLIENT_BUF_SIZE);
    g_clientLastTick[idx] = GetTickCount();
    g_clientTimeout[idx] = DEFAULT_TIMEOUT_MS;
    g_pendingFlag[idx] = 0;
    g_pendingSince[idx] = 0;

    if (clientType == 1) {
        printf(szHttpConnFmt, (unsigned)sock);
    } else {
        printf(szClientConnFmt, (unsigned)sock);
        SendText(sock, szConnected);
    }
}

/* ---- RemoveClient ---- */
static void RemoveClient(int idx) {
    SOCKET sock = g_clients[idx];
    if (sock != 0) {
        closesocket(sock);
        printf(szClientDiscFmt, (unsigned)sock);
    }
    g_clients[idx] = 0;
    g_clientIDs[idx] = 0;
    g_clientType[idx] = 0;
    g_clientBufLen[idx] = 0;
    g_pendingFlag[idx] = 0;
    g_pendingSince[idx] = 0;
}

/* ---- AcceptNewClients ---- */
static void AcceptNewClients(void) {
    SOCKET s;
    while ((s = accept(g_listenSocket, NULL, NULL)) != INVALID_SOCKET)
        AddClient(s, 0);
    while ((s = accept(g_listenSocketHttp, NULL, NULL)) != INVALID_SOCKET)
        AddClient(s, 1);
}

/* ---- ServerStart ---- */
static void ServerStart(void) {
    if (g_bRunning) return;
    StartTelnetServer();
    StartHttpServer();
    g_bRunning = 1;
    printf(szServerStarted);
    ModifyMenu(g_hMenu, ID_TRAY_TOGGLE, MF_BYCOMMAND | MF_STRING,
               ID_TRAY_TOGGLE, szMenuStop);
}

/* ---- ServerStop ---- */
static void ServerStop(void) {
    if (!g_bRunning) return;
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
        RemoveClient(i);
    if (g_listenSocket != INVALID_SOCKET) {
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
    }
    if (g_listenSocketHttp != INVALID_SOCKET) {
        closesocket(g_listenSocketHttp);
        g_listenSocketHttp = INVALID_SOCKET;
    }
    g_bRunning = 0;
    printf(szServerStopped);
    ModifyMenu(g_hMenu, ID_TRAY_TOGGLE, MF_BYCOMMAND | MF_STRING,
               ID_TRAY_TOGGLE, szMenuStart);
}

/* ---- ServerLoop ---- */
static void ServerLoop(void) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (!g_bRunning) return;
    AcceptNewClients();
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
        PollClient(i);
}

/* ---- WndProc ---- */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TRAYICON) {
        if ((UINT)lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hWnd);
            TrackPopupMenu(g_hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
        }
        return 0;
    }

    if (uMsg == WM_COMMAND) {
        int cmd = LOWORD(wParam);
        if (cmd == ID_TRAY_TOGGLE) {
            if (g_bRunning) ServerStop(); else ServerStart();
            return 0;
        }
        if (cmd == ID_TRAY_SHOW) {
            HWND cw = GetConsoleWindow();
            if (cw) {
                ShowWindow(cw, SW_RESTORE);
                SetForegroundWindow(cw);
            }
            return 0;
        }
        if (cmd == ID_TRAY_EXIT) {
            ServerStop();
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
            return 0;
        }
        return 0;
    }

    if (uMsg == WM_DESTROY) {
        ServerStop();
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

/* ---- CreateTray ---- */
static void CreateTray(void) {
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

    g_hMainWnd = CreateWindowEx(0, szWndClass, szTrayTip, 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, g_hInstance, NULL);

    g_hMenu = CreatePopupMenu();
    AppendMenu(g_hMenu, MF_STRING, ID_TRAY_TOGGLE,
               g_bRunning ? szMenuStop : szMenuStart);
    AppendMenu(g_hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(g_hMenu, MF_STRING, ID_TRAY_SHOW, szMenuShow);
    AppendMenu(g_hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(g_hMenu, MF_STRING, ID_TRAY_EXIT, szMenuExit);

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

/* ---- Main ---- */
int main(int argc, char *argv[]) {
    WSADATA wsaData;

    ResolveIniPath();
    printf(szStartup);
    LoadConfig();
    printf(szConfigFmt, g_aeCalled, g_TelnetPort, g_HttpPort,
           g_TelnetTimeout, g_DebugLog);
    InitArrays();
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    CreateCSVIfMissing();
    CreateTray();
    ServerStart();

    MSG qmsg;
    for (;;) {
        ServerLoop();
        if (PeekMessage(&qmsg, NULL, WM_QUIT, WM_QUIT, PM_NOREMOVE))
            break;
        Sleep(10);
    }

    WSACleanup();
    return 0;
}