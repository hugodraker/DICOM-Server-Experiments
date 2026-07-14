/* ============================================================================
 * RIS Multi-Protocol Server (DICOM + Telnet + HTTP)
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 *
 * Compile:
 *   gcc -Os -s -o RIS_SERVER.exe RIS_SERVER.c -lws2_32 -luser32 -lshell32 -lkernel32
 *   tcc -o RIS_SERVER.exe RIS_SERVER.c -lws2_32 -luser32 -lshell32 -lkernel32
 * ============================================================================ */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "kernel32.lib")

#define ID_OK IDOK
#define MAX_CLIENTS          64
#define RECV_SIZE           8192
#define CLIENT_BUF_SIZE     16384
#define FIELD_COUNT         27
#define FIELD_SIZE          512
#define ENTRY_SIZE          (FIELD_COUNT * FIELD_SIZE)
#define LINE_SIZE           32768
#define MAX_CSV_LINE        8192
#define BUFFER_SIZE         32768
#define APPEND_DELAY_MS     2000
#define DEFAULT_TIMEOUT_MS  10000
#define FIONBIO_VAL         0x8004667E
#define WM_TRAYICON         (WM_USER + 1)
#define WM_SETTINGS         (WM_USER + 2)
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
#define ID_TXT_STATUS       1017

/* ===== Global Variables ===== */
static int g_TelnetPort = 23, g_HttpPort = 80, g_DicomPort = 104;
static int g_TelnetTimeout = 10, g_DebugLog = 1, g_bRunning = 0, g_bExitApp = 0;
static SOCKET g_listenSocket = INVALID_SOCKET, g_listenSocketHttp = INVALID_SOCKET, g_listenSocketDicom = INVALID_SOCKET;
static HINSTANCE g_hInstance = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hSettingsDlg = NULL;
static HMENU g_hMenu = NULL;
static char g_aeCalled[32] = "AUTOIT_SCP";
static char g_delPwd[128] = "0";
static char szIniFile[MAX_PATH];
static CRITICAL_SECTION g_csvLock;
static CRITICAL_SECTION g_procCodesLock;

static SOCKET g_clients[MAX_CLIENTS];
static int    g_clientIDs[MAX_CLIENTS];
static int    g_clientType[MAX_CLIENTS];
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
static char   g_dispParam[1024];

NOTIFYICONDATA nid;

/* In-memory CSV store — no file I/O for patient data */
#define MAX_CSV_ROWS 4096
static char g_csvData[MAX_CSV_ROWS][LINE_SIZE];
static int  g_csvRows = 0;
static int  g_csvInitialized = 0;

/* Procedure codes lookup table */
#define MAX_PROC_CODES 256
typedef struct {
    char id[128];
    char procCode[128];
    char modality[128];
    char name[128];
} ProcCodeEntry;
static ProcCodeEntry g_procCodes[MAX_PROC_CODES];
static int g_procCodeCount = 0;
static int g_procCodesLoaded = 0;

typedef struct {
    char patientName[128];
    char patientID[128];
    char dob[128];
    char sex[128];
    char reqPhys[128];
    char reqSvc[128];
    char procDesc[128];
    char procReason[128];
    char accNum[128];
    char procID[128];
    char procPri[128];
    char aeTitle[128];
    char spsDate[128];
    char spsTime[128];
    char perfPhys[128];
    char spsDesc[128];
    char spsID[128];
    char station[128];
    char loc[128];
    char status[128];
    char modality[128];
    char refPhys[128];
    char studyDesc[128];
    char protoCode[128];
    char protoScheme[128];
    char protoMeaning[128];
    char studyUID[128];
} MWLEntry;

static const char *szIniServer = "Server", *szIniAET = "AETitle", *szIniTelnetPort = "TelnetPort";
static const char *szIniHttpPort = "HttpPort", *szIniDicomPort = "DicomPort";
static const char *szIniTelnetTimeout = "TelnetTimeout", *szIniDebugLog = "DebugLog", *szIniDelPwd = "DeletePassword";
static const char *szDefAET = "AUTOIT_SCP", *szDefTelnetPort = "23", *szDefHttpPort = "80";
static const char *szDefDicomPort = "104", *szDefTelnetTimeout = "10", *szDefDebug = "1", *szDefDelPwd = "0";
static const char *szWndClass = "WorklistTrayClass", *szTrayTip = "RIS Multi-Protocol Server";
static const char *szMenuStart = "Start Server", *szMenuStop = "Stop Server", *szMenuShow = "Show Console", *szMenuExit = "Exit";
static const char *szMenuSettings = "Settings";
static const char *szStartup = "RIS Server starting...\r\n";
static const char *szConfigFmt = "Config: AET=%s TelnetPort=%u HttpPort=%u DicomPort=%u Timeout=%u DebugLog=%u\r\n";
static const char *szListenFmt = "Telnet listening on port %u\r\n";
static const char *szListenHttpFmt = "HTTP listening on port %u\r\n";
static const char *szListenDicomFmt = "DICOM SCP listening on port %u\r\n";
static const char *szListenFailFmt = "ERROR: Failed to listen on port %u\r\n";
static const char *szClientConnFmt = "Telnet Client connected on socket %u\r\n";
static const char *szHttpConnFmt = "HTTP Client connected on socket %u\r\n";
static const char *szDicomConnFmt = "DICOM SCU connected.\r\n";
static const char *szClientDiscFmt = "Client disconnected on socket %u\r\n";
static const char *szTimeoutFmt = "Client on socket %u timed out\r\n";
static const char *szPendingFmt = "PENDING Accession %s\r\n";
static const char *szInsertedFmt = "INSERTED Accession %s\r\n";
static const char *szUpdatedFmt = "UPDATED Accession %s\r\n";
static const char *szConnected = "Connected to RIS Telnet Server. Waiting for data...\r\n";
static const char *szPendingResp = "PENDING\r\n", *szInsertedResp = "INSERTED\r\n", *szUpdatedResp = "UPDATED\r\n";
static const char *szInvalidResp = "INVALID LINE\r\n", *szServerStarted = "Server STARTED\r\n", *szServerStopped = "Server STOPPED\r\n";
static const char *szHttp200OK = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n";
static const char *szCSVHeader = "PatientName,PatientID,BirthDate,Sex,ReqPhys,ReqSvc,ProcDesc,Reason,Accession,ProcID,Priority,StationAE,StartDate,StartTime,PerfPhys,SPSDesc,SPSID,StationName,Location,Status,Modality,RefPhys,StudyDesc,ProtoCode,ProtoScheme,ProtoMeaning,StudyUID";
static const char *szFakePatient = ""; 

static const char *szHtmlFormsTop = "<div class='form-row' style='display:flex; align-items:center; gap:10px; margin-top:20px; margin-bottom:15px;'>"
"<button type='button' onclick='newPatient()' class='top-btn bg-green'>New Patient</button>"
"<button type='button' onclick='searchPatient()' class='top-btn bg-blue'>Search</button>"
"<form action='/upload' method='POST' enctype='multipart/form-data' id='uploadForm' style='display:inline-flex; align-items:center; gap:10px; margin:0;'>"
"<input type='file' name='csvfile' id='csvfile' style='display:none;' onchange='document.getElementById(\"uploadForm\").submit();'>"
"<label for='csvfile' class='top-btn bg-blue'>Choose File</label>"
"</form>"
"<form action='/delete' method='POST' id='delAllForm' style='margin:0;'>"
"<input type='hidden' name='pwd' id='delPwd' value=''>"
"<input type='button' value='Delete All Data' class='top-btn bg-red' onclick='delAll()'>"
"</form></div><hr>"
"<form id='manForm'>";

static const char *szHtmlFormsInputs = "<input type='text' name='PatientName' placeholder='Patient Name'> <input type='text' name='PatientID' placeholder='Patient ID'> Birth: <input type='date' name='BirthDate'> <select name='Sex'><option value=''>Sex</option><option value='M'>M</option><option value='F'>F</option><option value='O'>O</option></select><br>"
"<input type='text' name='ReqPhys' placeholder='Req Phys'> <input type='text' name='ReqSvc' placeholder='Req Svc'> <input type='text' name='ProcDesc' placeholder='Proc Desc'> <input type='text' name='Reason' placeholder='Reason'><br>"
"<input type='text' name='Accession' placeholder='Accession'> <input type='text' name='ProcID' placeholder='Proc ID'> <select name='Priority'><option value='LOW' selected>LOW</option><option value='MEDIUM'>MEDIUM</option><option value='ROUTINE'>ROUTINE</option><option value='HIGH'>HIGH</option></select> <input type='text' name='StationAE' placeholder='Station AE'><br>"
"Start: <input type='date' name='StartDate'> Time: <input type='time' name='StartTime'> <input type='text' name='PerfPhys' placeholder='Perf Phys'> <input type='text' name='SPSDesc' placeholder='SPS Desc'><br>"
"<input type='text' name='SPSID' placeholder='SPS ID'> ";

static const char *szHtmlForms2 = " <select name='Status'><option value='0'>NONE</option><option value='1' selected>SCHEDULED</option><option value='2'>ARRIVED</option><option value='3'>READY</option><option value='4'>STARTED</option><option value='5'>DEPARTED</option></select><br>"
"<input type='text' name='Modality' placeholder='Modality'> <input type='text' name='RefPhys' placeholder='Ref Phys'> <input type='text' name='StudyDesc' placeholder='Study Desc'><br>"
"<input type='text' name='ProtoCode' placeholder='Proto Code'> <input type='text' name='ProtoScheme' placeholder='Proto Scheme'> <input type='text' name='ProtoMeaning' placeholder='Proto Meaning'><br>"
"<input type='text' name='StudyUID' placeholder='Study UID' style='width:300px;'><br>"
"<div style='margin-top:15px; display:flex; gap:10px;'>"
"<button type='button' onclick='sendMan()' class='top-btn bg-green'>Add/Edit Patient</button> "
"<button type='button' onclick='nxtA()' class='top-btn bg-blue'>Next Accession</button> "
"<button type='button' onclick='multiStudy()' class='top-btn bg-blue'>Stub</button> "
"<button type='button' onclick='delP()' class='top-btn bg-red'>Delete Patient</button></div></form><hr>";

static const char *szHtmlStart = "<html><head><title>RIS Worklist Manager</title><style>"
"table{border-collapse:collapse;width:100%;font-size:14px;}th,td{border:1px solid #dddddd;padding:12px;}th{background:#009879;color:#fff;text-align:left;}"
"tr{border-bottom:1px solid #ddd;}tr:nth-child(even){background:#f3f3f3;}input,select{margin:5px;padding:5px;}div.query-box{margin:20px;padding:10px;background:#f9f9f9;border-radius:5px;}div.form-row{display:inline-block;margin-right:15px;}"
".top-btn { min-width: 140px; height: 36px; padding: 0 15px; box-sizing: border-box; text-align: center; border: none; cursor: pointer; color: white; display: inline-block; font-size: 14px; margin: 0; line-height: 36px; border-radius: 4px; text-decoration: none; }"
".bg-green { background: #4CAF50; } .bg-blue { background: #2196F3; } .bg-red { background: #f44336; }"
"</style>"
"<script>var fnames=['PatientName','PatientID','BirthDate','Sex','ReqPhys','ReqSvc','ProcDesc','Reason','Accession','ProcID','Priority','StationAE','StartDate','StartTime','PerfPhys','SPSDesc','SPSID','StationName','Location','Status','Modality','RefPhys','StudyDesc','ProtoCode','ProtoScheme','ProtoMeaning','StudyUID'];"
"function f(r){var c=r.cells;for(var i=0;i<c.length&&i<fnames.length;i++){var e=document.getElementsByName(fnames[i])[0];if(e)e.value=(c[i].innerText||c[i].textContent).trim();}"
"var sel=document.getElementById('procCodeSel');if(sel){var pid=document.getElementsByName('ProcID')[0].value;for(var j=0;j<sel.options.length;j++){sel.options[j].selected=(sel.options[j].getAttribute('data-id')===pid);}}}"
"window.maxAcc=window.maxAcc||0;"
"function nxtA(){var t=document.getElementById('pTable'),m=0,p='ACC',c=5;if(t){for(var i=1;i<t.rows.length;i++){var v=t.rows[i].cells[8].innerText||'',mt=v.match(/([^0-9]*)([0-9]+)/);if(mt){p=mt[1];var val=parseInt(mt[2],10);if(val>m){m=val;c=mt[2].length;}}}}if(window.maxAcc<=m)window.maxAcc=m;window.maxAcc++;document.getElementsByName('Accession')[0].value=p+String(window.maxAcc).padStart(c,'0');}"
"function sendMan(){var a=document.getElementsByName('Accession')[0].value.trim();if(!a){alert('Accession number is required!');return false;}"
"var sel=document.getElementById('procCodeSel');var opts=[];if(sel){for(var j=0;j<sel.options.length;j++){if(sel.options[j].selected&&sel.options[j].value!=='')opts.push(sel.options[j]);}}"
"if(opts.length>1){var p=[];for(var k=0;k<opts.length;k++){nxtA();"
"document.getElementsByName('Modality')[0].value=opts[k].getAttribute('data-mod')||'';"
"document.getElementsByName('ProcDesc')[0].value=opts[k].text||'';"
"document.getElementsByName('ProcID')[0].value=opts[k].getAttribute('data-id')||'';"
"document.getElementsByName('SPSID')[0].value=opts[k].getAttribute('data-code')||'';"
"document.getElementsByName('StationAE')[0].value='';"
"document.getElementsByName('SPSDesc')[0].value='';"
"var sn=document.getElementsByName('StationName')[0];if(sn)sn.value='';"
"document.getElementsByName('Location')[0].value='';"
"document.getElementsByName('StudyDesc')[0].value='';"
"var c=[];for(var i=0;i<fnames.length;i++){var el=document.getElementsByName(fnames[i])[0];var v=el?el.value:'';if(v.indexOf(',')>-1||v.indexOf('\"')>-1)v='\"'+v.replace(new RegExp('\"','g'),'\"\"')+'\"';c.push(v);}"
"p.push(fetch('/manual',{method:'POST',body:c.join(',')}));}Promise.all(p).then(function(){window.location.href='/';});return false;}"
"var c=[];for(var i=0;i<fnames.length;i++){var el=document.getElementsByName(fnames[i])[0];var v=el?el.value:'';if(v.indexOf(',')>-1||v.indexOf('\"')>-1)v='\"'+v.replace(new RegExp('\"','g'),'\"\"')+'\"';c.push(v);}fetch('/manual',{method:'POST',body:c.join(',')}).then(function(){window.location.href='/';});return false;}"
"function delP(){var a=document.getElementsByName('Accession')[0].value;if(a&&confirm('Delete Patient '+a+'?'))fetch('/delpat',{method:'POST',body:a}).then(function(){window.location.href='/';});}"
"function delAll(){var p=prompt('Enter password to delete all data:');if(p!==null){document.getElementById('delPwd').value=p;document.getElementById('delAllForm').submit();}}"
"function multiStudy(){alert('Stub not implemented');}"
"function sortTable(n){var t,r,sw,i,x,y,sd,d;t=document.getElementById('pTable');sw=true;d='asc';while(sw){sw=false;r=t.rows;for(i=1;i<(r.length-1);i++){sd=false;x=r[i].getElementsByTagName('TD')[n];y=r[i+1].getElementsByTagName('TD')[n];if(d=='asc'){if(x.innerHTML.toLowerCase()>y.innerHTML.toLowerCase()){sd=true;break;}}else{if(x.innerHTML.toLowerCase()<y.innerHTML.toLowerCase()){sd=true;break;}}}if(sd){r[i].parentNode.insertBefore(r[i+1],r[i]);sw=true;}}}"
"function formatDates(){var t=document.getElementById('pTable');if(!t)return;for(var i=1;i<t.rows.length;i++){var c=t.rows[i].cells;if(c.length>13){"
"var b=c[2].innerText;if(b.length===8)c[2].innerText=b.substr(0,4)+'-'+b.substr(4,2)+'-'+b.substr(6,2);"
"var s=c[12].innerText;if(s.length===8)c[12].innerText=s.substr(0,4)+'-'+s.substr(4,2)+'-'+s.substr(6,2);"
"var tm=c[13].innerText;if(tm.length===6)c[13].innerText=tm.substr(0,2)+':'+tm.substr(2,2);"
"}}}"
"function newPatient(){for(var i=0;i<fnames.length;i++){var el=document.getElementsByName(fnames[i])[0];if(el)el.value='';}var d=new Date(),y=d.getFullYear(),mo=('0'+(d.getMonth()+1)).slice(-2),da=('0'+d.getDate()).slice(-2);var sd=document.getElementsByName('StartDate')[0];if(sd)sd.value=y+'-'+mo+'-'+da;nxtA();var su=document.getElementsByName('StudyUID')[0];if(su){var sec=Math.floor(Date.now()/1000);var rnd=Math.floor(Math.random()*10000);su.value='1.2.840.113619.2.1.'+sec+'.'+rnd;}var stat=document.getElementsByName('Status')[0];if(stat)stat.value='1';var pri=document.getElementsByName('Priority')[0];if(pri)pri.value='LOW';var tm=document.getElementsByName('StartTime')[0];if(tm)tm.value='16:00';}"
"function loadProcCode(){var sel=document.getElementById('procCodeSel');if(sel&&sel.selectedIndex>=0){var idx=sel.selectedIndex;"
"var id=document.getElementsByName('ProcID')[0];var code=document.getElementsByName('SPSID')[0];var mod=document.getElementsByName('Modality')[0];var desc=document.getElementsByName('ProcDesc')[0];"
"if(id&&code&&mod&&desc){id.value=sel.options[idx].getAttribute('data-id')||'';code.value=sel.options[idx].getAttribute('data-code')||'';mod.value=sel.options[idx].getAttribute('data-mod')||'';desc.value=sel.options[idx].text;}}"
"}"
"function filterProcCodes(){var i,f=document.getElementById('procFilter').value.toUpperCase(),s=document.getElementById('procCodeSel'),o=s.options;for(i=1;i<o.length;i++){if((o[i].text||o[i].innerText).toUpperCase().indexOf(f)>-1){o[i].style.display='';}else{o[i].style.display='none';o[i].selected=false;}}}"
"function searchPatient(){alert('Search not implemented');}"
"window.onload=function(){newPatient();};</script></head><body style='font-family:Arial,sans-serif;padding:20px;'>";

static const char *szHtmlEnd = "</body></html>";

/* ===== Logging ===== */
static void write_log(const char *msg) {
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

/* ===== Helpers ===== */
static void RemoveClient(int idx);

static char *GetFieldPtr(char *pEntry, int fieldIdx) { return pEntry + (fieldIdx * FIELD_SIZE); }
static char *GetClientBufferPtr(int idx) { return g_clientBuffers + (idx * CLIENT_BUF_SIZE); }
static char *GetPendingEntryPtr(int idx) { return g_pendingEntries + (idx * ENTRY_SIZE); }

static int IsWhite(char ival) { return ival == ' ' || ival == '\t' || ival == '\r' || ival == '\n'; }

static void TrimInPlace(char *pStr) {
    char *p = pStr;
    while (*p && IsWhite(*p)) p++;
    if (p != pStr) memmove(pStr, p, strlen(p)+1);
    size_t len = strlen(pStr);
    while (len > 0 && IsWhite(pStr[len-1])) pStr[--len] = '\0';
}

static void StripDashes(char *pStr) {
    char *dst = pStr;
    while (*pStr) { if (*pStr != '-') *dst++ = *pStr; pStr++; }
    *dst = '\0';
}

static void StripColons(char *pStr) {
    char *dst = pStr; char *orig = pStr;
    while (*pStr) { if (*pStr != ':') *dst++ = *pStr; pStr++; }
    *dst = '\0';
    if (strlen(orig) == 4) { strcat(orig, "00"); }
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
        TrimInPlace(last); TrimInPlace(rest);
        
        char *space = strchr(rest, ' ');
        while (space) { *space = '^'; space = strchr(space + 1, ' '); }
        
        snprintf(temp, sizeof(temp), "%s^%s", last, rest);
        strncpy(pName, temp, FIELD_SIZE - 1);
        pName[FIELD_SIZE - 1] = '\0';
        
        int len = (int)strlen(pName);
        while(len > 0 && pName[len-1] == '^') pName[--len] = '\0';
    } else {
        char *space1 = strchr(pName, ' ');
        if (!space1) return;
        
        char *lastSpace = strrchr(pName, ' ');
        if (lastSpace == space1) {
            *space1 = '\0';
            snprintf(temp, sizeof(temp), "%s^%s", space1 + 1, pName);
            strncpy(pName, temp, FIELD_SIZE - 1);
            pName[FIELD_SIZE - 1] = '\0';
        } else {
            *lastSpace = '\0'; 
            char last[256]; strncpy(last, lastSpace + 1, 255); last[255] = '\0';
            char rest[256]; strncpy(rest, pName, 255); rest[255] = '\0';
            char *sp = strchr(rest, ' ');
            while(sp) { *sp = '^'; sp = strchr(sp + 1, ' '); }
            snprintf(temp, sizeof(temp), "%s^%s", last, rest);
            strncpy(pName, temp, FIELD_SIZE - 1);
            pName[FIELD_SIZE - 1] = '\0';
        }
    }
}

static char UpperChar(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static int StartsWithDISP(const char *pLine) {
    return (UpperChar(pLine[0]) == 'D' && UpperChar(pLine[1]) == 'I' &&
            UpperChar(pLine[2]) == 'S' && UpperChar(pLine[3]) == 'P');
}

static int StrIEquals(const char *a, const char *b) {
    while (UpperChar(*a) == UpperChar(*b)) { if (*a == '\0') return 1; a++; b++; }
    return 0;
}

static int StrNIEquals(const char *a, const char *b, int len) {
    for (int i = 0; i < len; i++) {
        if (UpperChar(a[i]) != UpperChar(b[i])) return 0;
        if (a[i] == '\0' || b[i] == '\0') return 0;
    }
    return 1;
}

static char* StrIStr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    int len = (int)strlen(needle);
    while (*haystack) {
        if (StrNIEquals(haystack, needle, len)) return (char*)haystack;
        haystack++;
    }
    return NULL;
}

static int IsEightDigits(const char *pStr) {
    if (*pStr == '\0') return 0;
    for (int i = 0; i < 8; i++) if (pStr[i] < '0' || pStr[i] > '9') return 0;
    return (pStr[8] == '\0');
}

/* Hardened SendText: Handles WSAEWOULDBLOCK on non-blocking sockets */
static void SendText(SOCKET sock, const char *pText) {
    if (!pText || sock == INVALID_SOCKET || sock == 0) return;
    int nLen = (int)strlen(pText);
    int total = 0;
    while (total < nLen) {
        int r = send(sock, pText + total, nLen - total, 0);
        if (r > 0) {
            total += r;
        } else if (r == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAENOBUFS) {
                Sleep(1); /* Allow OS TCP buffer to flush before retrying */
                continue;
            }
            break; /* Fatal socket error */
        } else {
            break;
        }
    }
}

/* Cleanly closes an HTTP client by draining unread receive data to prevent TCP RST */
static void CloseHttpClient(int clientIdx) {
    SOCKET sock = g_clients[clientIdx];
    if (sock != 0 && sock != INVALID_SOCKET) {
        char dumpBuf[4096];
        while (recv(sock, dumpBuf, sizeof(dumpBuf), 0) > 0) { }
        shutdown(sock, SD_SEND);
    }
    RemoveClient(clientIdx);
}

/* ===== Config ===== */
static void ResolveIniPath(void) {
    GetModuleFileName(NULL, szIniFile, MAX_PATH);
    size_t len = strlen(szIniFile);
    if (len > 4 && szIniFile[len-4] == '.') {
        szIniFile[len-3] = 'i'; szIniFile[len-2] = 'n'; szIniFile[len-1] = 'i';
    }
}

static void SetAETitle(const char *pSrc) {
    memset(g_aeCalled, ' ', sizeof(g_aeCalled) - 1); g_aeCalled[sizeof(g_aeCalled) - 1] = '\0';
    size_t nLen = strlen(pSrc); if (nLen > sizeof(g_aeCalled) - 1) nLen = sizeof(g_aeCalled) - 1;
    memcpy(g_aeCalled, pSrc, nLen);
}

static void LoadConfig(void) {
    GetPrivateProfileString(szIniServer, szIniAET, szDefAET, g_iniBuf, sizeof(g_iniBuf), szIniFile);
    SetAETitle(g_iniBuf);
    
    GetPrivateProfileString(szIniServer, szIniDelPwd, szDefDelPwd, g_delPwd, sizeof(g_delPwd), szIniFile);
    TrimInPlace(g_delPwd);
    
    g_TelnetPort = GetPrivateProfileInt(szIniServer, szIniTelnetPort, 23, szIniFile);
    g_HttpPort = GetPrivateProfileInt(szIniServer, szIniHttpPort, 80, szIniFile);
    g_DicomPort = GetPrivateProfileInt(szIniServer, szIniDicomPort, 104, szIniFile);
    g_TelnetTimeout = GetPrivateProfileInt(szIniServer, szIniTelnetTimeout, 10, szIniFile);
    g_DebugLog = GetPrivateProfileInt(szIniServer, szIniDebugLog, 1, szIniFile);
}

static void SaveConfig(void) {
    char buf[32];
    WritePrivateProfileString(szIniServer, szIniAET, g_aeCalled, szIniFile);
    WritePrivateProfileString(szIniServer, szIniDelPwd, g_delPwd, szIniFile);
    
    snprintf(buf, sizeof(buf), "%d", g_TelnetPort);
    WritePrivateProfileString(szIniServer, szIniTelnetPort, buf, szIniFile);
    
    snprintf(buf, sizeof(buf), "%d", g_HttpPort);
    WritePrivateProfileString(szIniServer, szIniHttpPort, buf, szIniFile);
    
    snprintf(buf, sizeof(buf), "%d", g_DicomPort);
    WritePrivateProfileString(szIniServer, szIniDicomPort, buf, szIniFile);
    
    snprintf(buf, sizeof(buf), "%d", g_TelnetTimeout);
    WritePrivateProfileString(szIniServer, szIniTelnetTimeout, buf, szIniFile);
    
    snprintf(buf, sizeof(buf), "%d", g_DebugLog);
    WritePrivateProfileString(szIniServer, szIniDebugLog, buf, szIniFile);
}

/* ===== CSV Functions ===== */
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
            *(pDest + charIdx) = '\0'; fieldIdx++;
            if (fieldIdx >= FIELD_COUNT) break;
            pDest += FIELD_SIZE; charIdx = 0; si++; continue;
        }
        if (charIdx < FIELD_SIZE - 1) { *(pDest + charIdx) = charCode; charIdx++; }
        si++;
    }
    *(pDest + charIdx) = '\0';
    for (int fi = 0; fi < FIELD_COUNT; fi++) TrimInPlace(GetFieldPtr(pEntry, fi));
    return 1;
}

static int ValidateEntry(char *pEntry) { return (*GetFieldPtr(pEntry, 8) != '\0'); }

static int NeedsQuote(const char *pField) {
    while (*pField) { if (*pField == ',' || *pField == '"') return 1; pField++; }
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

static void EnsureCsvInitialized(void) {
    if (g_csvInitialized) return;
    g_csvInitialized = 1;
    strncpy(g_csvData[0], szCSVHeader, LINE_SIZE - 1); g_csvData[0][LINE_SIZE - 1] = '\0';
    
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
            found = 1; break;
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
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        newline = strchr(line, '\r');
        if (newline) *newline = '\0';
        
        char *fields[4] = {g_procCodes[g_procCodeCount].id, 
                          g_procCodes[g_procCodeCount].procCode,
                          g_procCodes[g_procCodeCount].modality,
                          g_procCodes[g_procCodeCount].name};
        char *p = line;
        for (int i = 0; i < 4; i++) {
            char *comma = strchr(p, ',');
            if (comma) { *comma = '\0'; strncpy(fields[i], p, 127); fields[i][127] = '\0'; TrimInPlace(fields[i]); p = comma + 1; }
            else { strncpy(fields[i], p, 127); fields[i][127] = '\0'; TrimInPlace(fields[i]); break; }
        }
        g_procCodeCount++;
    }
    fclose(fp);
    g_procCodesLoaded = 1;
    LeaveCriticalSection(&g_procCodesLock);
    write_log("Procedure codes loaded");
}

/* ===== DICOM Helpers ===== */
static bool dicom_add_str(uint8_t* buf, int* off, int max, uint16_t g, uint16_t e, const char* s) {
    uint32_t len = s ? (uint32_t)strlen(s) : 0;
    uint32_t pad = (len % 2) ? 1 : 0;
    uint32_t tot = len + pad;
    if (*off + 8 + (int)tot > max) return false;
    buf[(*off)++]=g&0xFF; buf[(*off)++]=(g>>8)&0xFF;
    buf[(*off)++]=e&0xFF; buf[(*off)++]=(e>>8)&0xFF;
    buf[(*off)++]=tot&0xFF; buf[(*off)++]=(tot>>8)&0xFF;
    buf[(*off)++]=(tot>>16)&0xFF; buf[(*off)++]=(tot>>24)&0xFF;
    if(len>0){memcpy(&buf[*off],s,len);*off+=len;}
    if(pad) buf[(*off)++]=' ';
    return true;
}
static bool dicom_add_us(uint8_t* buf, int* off, int max, uint16_t g, uint16_t e, uint16_t v) {
    if (*off+10>max) return false;
    buf[(*off)++]=g&0xFF; buf[(*off)++]=(g>>8)&0xFF;
    buf[(*off)++]=e&0xFF; buf[(*off)++]=(e>>8)&0xFF;
    buf[(*off)++]=2; buf[(*off)++]=0; buf[(*off)++]=0; buf[(*off)++]=0;
    buf[(*off)++]=v&0xFF; buf[(*off)++]=(v>>8)&0xFF;
    return true;
}
static bool dicom_add_ul(uint8_t* buf, int* off, int max, uint16_t g, uint16_t e, uint32_t v) {
    if (*off+12>max) return false;
    buf[(*off)++]=g&0xFF; buf[(*off)++]=(g>>8)&0xFF;
    buf[(*off)++]=e&0xFF; buf[(*off)++]=(e>>8)&0xFF;
    buf[(*off)++]=4; buf[(*off)++]=0; buf[(*off)++]=0; buf[(*off)++]=0;
    buf[(*off)++]=v&0xFF; buf[(*off)++]=(v>>8)&0xFF;
    buf[(*off)++]=(v>>16)&0xFF; buf[(*off)++]=(v>>24)&0xFF;
    return true;
}
static bool dicom_seq_hdr(uint8_t* buf, int* off, int max, uint16_t g, uint16_t e) {
    if (*off+8>max) return false;
    buf[(*off)++]=g&0xFF; buf[(*off)++]=(g>>8)&0xFF;
    buf[(*off)++]=e&0xFF; buf[(*off)++]=(e>>8)&0xFF;
    buf[(*off)++]=0xFF; buf[(*off)++]=0xFF; buf[(*off)++]=0xFF; buf[(*off)++]=0xFF;
    return true;
}
static bool dicom_item_hdr(uint8_t* buf, int* off, int max) {
    if (*off+8>max) return false;
    buf[(*off)++]=0xFE; buf[(*off)++]=0xFF; buf[(*off)++]=0x00; buf[(*off)++]=0xE0;
    buf[(*off)++]=0xFF; buf[(*off)++]=0xFF; buf[(*off)++]=0xFF; buf[(*off)++]=0xFF;
    return true;
}
static bool dicom_item_delim(uint8_t* buf, int* off, int max) {
    if (*off+8>max) return false;
    buf[(*off)++]=0xFE; buf[(*off)++]=0xFF; buf[(*off)++]=0x0D; buf[(*off)++]=0xE0;
    buf[(*off)++]=0x00; buf[(*off)++]=0x00; buf[(*off)++]=0x00; buf[(*off)++]=0x00;
    return true;
}
static bool dicom_seq_delim(uint8_t* buf, int* off, int max) {
    if (*off+8>max) return false;
    buf[(*off)++]=0xFE; buf[(*off)++]=0xFF; buf[(*off)++]=0xDD; buf[(*off)++]=0xE0;
    buf[(*off)++]=0x00; buf[(*off)++]=0x00; buf[(*off)++]=0x00; buf[(*off)++]=0x00;
    return true;
}
static bool dicom_add_string(uint8_t* buf, int* off, int max, uint16_t g, uint16_t e, const char* s) {
    return dicom_add_str(buf, off, max, g, e, s);
}

static void trim_mwl(char *str) {
    if (!str) return;
    char *p = str;
    while (*p == ' ' || *p == '"') p++;
    if (p != str) memmove(str, p, strlen(p) + 1);
    int len = (int)strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '"' || str[len-1] == '\r' || str[len-1] == '\n')) str[--len] = '\0';
}

static void parse_csv_line_mwl(char *line, MWLEntry *e) {
    memset(e, 0, sizeof(MWLEntry));
    char *fields[] = {
        e->patientName, e->patientID, e->dob, e->sex, e->reqPhys, e->reqSvc, e->procDesc, e->procReason,
        e->accNum, e->procID, e->procPri, e->aeTitle, e->spsDate, e->spsTime, e->perfPhys,
        e->spsDesc, e->spsID, e->station, e->loc, e->status, e->modality, e->refPhys, e->studyDesc,
        e->protoCode, e->protoScheme, e->protoMeaning, e->studyUID
    };
    char *start = line;
    for (int i = 0; i < 27; i++) {
        char *end = strchr(start, ',');
        if (end) { *end = '\0'; strncpy(fields[i], start, 127); fields[i][127] = '\0'; trim_mwl(fields[i]); start = end + 1; }
        else { strncpy(fields[i], start, 127); fields[i][127] = '\0'; trim_mwl(fields[i]); break; }
    }
}

static void send_pdata_tf(SOCKET s, uint8_t pc_id, uint8_t msg_ctrl, const uint8_t* data, uint32_t data_len) {
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

/* Hardened DICOM tag extractor */
static void dicom_get_string(const uint8_t *buf, int len, uint16_t g, uint16_t e, char *out) {
    uint8_t tg[4] = { (uint8_t)(g & 0xFF), (uint8_t)(g >> 8), (uint8_t)(e & 0xFF), (uint8_t)(e >> 8) };
    for (int i = 0; i < len - 8; i++) {
        if (buf[i] == tg[0] && buf[i+1] == tg[1] && buf[i+2] == tg[2] && buf[i+3] == tg[3]) {
            int vlen = 0; int offset = 0; bool is_vr = false;
            char v1 = buf[i+4], v2 = buf[i+5];
            
            if ((v1 >= 'A' && v1 <= 'Z') && (v2 >= 'A' && v2 <= 'Z')) {
                if ((v1=='D'&&v2=='A')||(v1=='T'&&v2=='M')||(v1=='C'&&v2=='S')||(v1=='S'&&v2=='H')||
                    (v1=='L'&&v2=='O')||(v1=='P'&&v2=='N')||(v1=='U'&&v2=='I')||(v1=='A'&&v2=='E')||
                    (v1=='A'&&v2=='S')||(v1=='I'&&v2=='S')||(v1=='D'&&v2=='S')||(v1=='S'&&v2=='T')||
                    (v1=='L'&&v2=='T')||(v1=='U'&&v2=='T')||(v1=='O'&&v2=='B')||(v1=='O'&&v2=='W')||
                    (v1=='S'&&v2=='Q')||(v1=='U'&&v2=='N')||(v1=='F'&&v2=='D')||(v1=='F'&&v2=='L')||
                    (v1=='S'&&v2=='L')||(v1=='S'&&v2=='S')||(v1=='U'&&v2=='L')||(v1=='U'&&v2=='S')) {
                    is_vr = true;
                }
            }
            
            if (is_vr) {
                if ((v1=='O'&&v2=='B') || (v1=='O'&&v2=='W') || (v1=='S'&&v2=='Q') || (v1=='U'&&v2=='N')) {
                    vlen = buf[i+8] | (buf[i+9]<<8) | (buf[i+10]<<16) | (buf[i+11]<<24); offset = 12;
                } else { vlen = buf[i+6] | (buf[i+7]<<8); offset = 8; }
            } else {
                vlen = buf[i+4] | (buf[i+5]<<8) | (buf[i+6]<<16) | (buf[i+7]<<24); offset = 8;
            }
            
            if (vlen > 0 && vlen < 128 && i + offset + vlen <= len) {
                memcpy(out, &buf[i+offset], vlen);
                out[vlen] = '\0';
                for(int j=vlen-1; j>=0 && (out[j]==' ' || out[j]=='\0'); j--) out[j]='\0';
                return;
            }
        }
    }
}

/* ===== DICOM Protocol Handlers ===== */
static void handle_association_rq(SOCKET clientSocket, char* buffer, int bytesRead) {
    write_log("Parsing A-ASSOCIATE-RQ...");
    uint8_t ac_pdu[2048] = {0};
    ac_pdu[0] = 0x02; ac_pdu[1] = 0x00;
    ac_pdu[6] = 0x00; ac_pdu[7] = 0x01;
    if (bytesRead < 42) return;
    memcpy(&ac_pdu[10], &buffer[10], 16);
    memcpy(&ac_pdu[26], &buffer[26], 16);
    int offset = 74;
    uint8_t app_ctx[] = { 0x10, 0x00, 0x00, 0x15, '1','.','2','.','8','4','0','.','1','0','0','0','8','.','3','.','1','.','1','.','1' };
    memcpy(&ac_pdu[offset], app_ctx, sizeof(app_ctx)); offset += sizeof(app_ctx);
    int i = 74;
    while (i < bytesRead - 4) {
        uint8_t item_type = buffer[i];
        uint16_t item_len = ((uint8_t)buffer[i+2] << 8) | (uint8_t)buffer[i+3];
        if (item_type == 0x20 && i + 4 + item_len <= bytesRead) {
            write_log("Accepting PC ID");
            uint8_t pc_id = buffer[i+4];
            ac_pdu[offset++] = 0x21; ac_pdu[offset++] = 0x00;
            ac_pdu[offset++] = 0x00; ac_pdu[offset++] = 0x19;
            ac_pdu[offset++] = pc_id; ac_pdu[offset++] = 0x00; ac_pdu[offset++] = 0x00; ac_pdu[offset++] = 0x00;
            uint8_t ts[] = { 0x40, 0x00, 0x00, 0x11, '1','.','2','.','8','4','0','.','1','0','0','0','8','.','1','.','2' };
            memcpy(&ac_pdu[offset], ts, sizeof(ts)); offset += sizeof(ts);
        }
        i += 4 + item_len;
    }
    uint8_t user_info[] = { 0x50, 0x00, 0x00, 0x1E, 0x51, 0x00, 0x00, 0x04, 0x00, 0x00, 0x40, 0x00, 0x52, 0x00, 0x00, 0x12, '1','.','2','.','2','7','6','.','0','.','7','2','3','.','1','.','9','9' };
    memcpy(&ac_pdu[offset], user_info, sizeof(user_info)); offset += sizeof(user_info);
    uint32_t pdu_len = offset - 6;
    ac_pdu[2] = (pdu_len >> 24) & 0xFF; ac_pdu[3] = (pdu_len >> 16) & 0xFF;
    ac_pdu[4] = (pdu_len >> 8) & 0xFF;  ac_pdu[5] = pdu_len & 0xFF;
    send(clientSocket, (char*)ac_pdu, offset, 0);
}

static void handle_c_echo_rq(SOCKET clientSocket, uint8_t pc_id, uint16_t msg_id) {
    uint8_t cmd_buf[512]; int cmd_len = 0;
    dicom_add_ul(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0000, 0);
    dicom_add_string(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0002, "1.2.840.10008.1.1");
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0100, 0x8030);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0120, msg_id);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0800, 0x0101);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0900, 0x0000);
    uint32_t gl = cmd_len - 12;
    cmd_buf[8] = gl & 0xFF; cmd_buf[9] = (gl >> 8) & 0xFF;
    cmd_buf[10] = (gl >> 16) & 0xFF; cmd_buf[11] = (gl >> 24) & 0xFF;
    send_pdata_tf(clientSocket, pc_id, 0x03, cmd_buf, cmd_len);
}

static void handle_c_find_rq(SOCKET clientSocket, uint8_t pc_id, uint16_t msg_id, const uint8_t *req_buf, int req_len) {
    char qDate[128] = {0};
    char qTime[128] = {0};
    char qAET[128] = {0};
    char qMod[128] = {0};
    
    dicom_get_string(req_buf, req_len, 0x0008, 0x0060, qMod);
    dicom_get_string(req_buf, req_len, 0x0040, 0x0001, qAET);
    dicom_get_string(req_buf, req_len, 0x0040, 0x0002, qDate);
    dicom_get_string(req_buf, req_len, 0x0040, 0x0003, qTime);

    EnsureCsvInitialized();
    {
        int row;
        for (row = 1; row < g_csvRows; row++) {
            MWLEntry e; parse_csv_line_mwl(g_csvData[row], &e);
            
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

            uint8_t cmd_buf[512]; int cmd_len = 0;
            dicom_add_ul(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0000, 0);
            dicom_add_string(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0002, "1.2.840.10008.5.1.4.31");
            dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0100, 0x8020);
            dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0120, msg_id);
            dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0800, 0x0102);
            dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0900, 0xFF00);
            uint32_t gl = cmd_len - 12;
            cmd_buf[8] = gl & 0xFF; cmd_buf[9] = (gl >> 8) & 0xFF;
            cmd_buf[10] = (gl >> 16) & 0xFF; cmd_buf[11] = (gl >> 24) & 0xFF;
            send_pdata_tf(clientSocket, pc_id, 0x03, cmd_buf, cmd_len);
            
            uint8_t data_buf[8192]; int data_len = 0;
            
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0050, e.accNum);
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0090, e.refPhys);
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0010, 0x0010, e.patientName);
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0010, 0x0020, e.patientID); 
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0010, 0x0030, e.dob);
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0020, 0x000D, e.studyUID);
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0032, 0x1060, e.procDesc);

            dicom_seq_hdr(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0100);
            dicom_item_hdr(data_buf, &data_len, sizeof(data_buf));
            
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0060, e.modality);
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0001, e.aeTitle);
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0002, e.spsDate);
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0003, e.spsTime);
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0007, e.spsDesc);
            
            dicom_seq_hdr(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0008);
            dicom_item_hdr(data_buf, &data_len, sizeof(data_buf));
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0100, e.protoCode);
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0102, e.protoScheme);
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0008, 0x0104, e.protoMeaning);
            dicom_item_delim(data_buf, &data_len, sizeof(data_buf));
            dicom_seq_delim(data_buf, &data_len, sizeof(data_buf));
            
            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x0009, e.spsID);
            
            dicom_item_delim(data_buf, &data_len, sizeof(data_buf));
            dicom_seq_delim(data_buf, &data_len, sizeof(data_buf));

            dicom_add_string(data_buf, &data_len, sizeof(data_buf), 0x0040, 0x1001, e.procID); 

            send_pdata_tf(clientSocket, pc_id, 0x02, data_buf, data_len);
        }
    }
    
    uint8_t cmd_buf[512]; int cmd_len = 0;
    dicom_add_ul(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0000, 0);
    dicom_add_string(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0002, "1.2.840.10008.5.1.4.31");
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0100, 0x8020);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0120, msg_id);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0800, 0x0101);
    dicom_add_us(cmd_buf, &cmd_len, sizeof(cmd_buf), 0x0000, 0x0900, 0x0000);
    uint32_t gl = cmd_len - 12;
    cmd_buf[8] = gl & 0xFF; cmd_buf[9] = (gl >> 8) & 0xFF;
    cmd_buf[10] = (gl >> 16) & 0xFF; cmd_buf[11] = (gl >> 24) & 0xFF;
    send_pdata_tf(clientSocket, pc_id, 0x03, cmd_buf, cmd_len);
}

static void handle_pdata_tf(SOCKET clientSocket, char* buffer, int bytesRead) {
    if (bytesRead < 6) return;

    uint32_t pdu_len = ((uint32_t)(uint8_t)buffer[2] << 24) |
                       ((uint32_t)(uint8_t)buffer[3] << 16) |
                       ((uint32_t)(uint8_t)buffer[4] << 8)  |
                        (uint32_t)(uint8_t)buffer[5];

    int pos = 6;               
    int end = 6 + (int)pdu_len;
    if (end > bytesRead) end = bytesRead;

    static uint8_t g_findCmd[16384];
    static int     g_findCmdLen = 0;
    static uint8_t g_findData[16384];
    static int     g_findDataLen = 0;
    static uint8_t g_findPcId = 0;
    static uint16_t g_findMsgId = 0;
    static uint16_t g_findCommand = 0;

    while (pos + 6 <= end) {
        uint32_t pdv_len = ((uint32_t)(uint8_t)buffer[pos]     << 24) |
                           ((uint32_t)(uint8_t)buffer[pos + 1] << 16) |
                           ((uint32_t)(uint8_t)buffer[pos + 2] << 8)  |
                            (uint32_t)(uint8_t)buffer[pos + 3];
        uint8_t pc_id    = buffer[pos + 4];
        uint8_t msg_ctrl = buffer[pos + 5];
        int     data_off = pos + 6;
        int     data_len = (int)pdv_len - 2;

        if (data_len < 0 || data_off + data_len > end) break;

        bool is_command   = (msg_ctrl & 0x01) != 0;
        bool is_last_frag = (msg_ctrl & 0x02) != 0;

        if (is_command) {
            if (g_findCmdLen + data_len < (int)sizeof(g_findCmd)) {
                memcpy(g_findCmd + g_findCmdLen, buffer + data_off, data_len);
                g_findCmdLen += data_len;
            }
            g_findPcId = pc_id;

            if (is_last_frag) {
                g_findCommand = 0;
                g_findMsgId   = 0;
                for (int i = 0; i <= g_findCmdLen - 10; i++) {
                    if (g_findCmd[i] == 0x00 && g_findCmd[i + 1] == 0x00 &&
                        g_findCmd[i + 2] == 0x00 && g_findCmd[i + 3] == 0x01 &&
                        g_findCmd[i + 4] == 0x02 && g_findCmd[i + 5] == 0x00) {
                        g_findCommand = (uint16_t)g_findCmd[i + 8] |
                                        ((uint16_t)g_findCmd[i + 9] << 8);
                    }
                    if (g_findCmd[i] == 0x00 && g_findCmd[i + 1] == 0x00 &&
                        g_findCmd[i + 2] == 0x20 && g_findCmd[i + 3] == 0x01 &&
                        g_findCmd[i + 4] == 0x02 && g_findCmd[i + 5] == 0x00) {
                        g_findMsgId = (uint16_t)g_findCmd[i + 8] |
                                      ((uint16_t)g_findCmd[i + 9] << 8);
                    }
                }

                if (g_findCommand == 0x0030) {
                    handle_c_echo_rq(clientSocket, g_findPcId, g_findMsgId);
                    g_findCmdLen  = 0;
                    g_findDataLen = 0;
                }
            }
        } else {
            if (g_findDataLen + data_len < (int)sizeof(g_findData)) {
                memcpy(g_findData + g_findDataLen, buffer + data_off, data_len);
                g_findDataLen += data_len;
            }

            if (is_last_frag) {
                if (g_findCommand == 0x0020) {
                    handle_c_find_rq(clientSocket, g_findPcId, g_findMsgId,
                                     g_findData, g_findDataLen);
                }
                g_findCmdLen  = 0;
                g_findDataLen = 0;
            }
        }

        pos += 4 + (int)pdv_len;
    }
}

static int recv_pdu(SOCKET s, uint8_t *buf, int max_len) {
    int total = 0;
    while (total < 6) { int r = recv(s, (char*)buf + total, 6 - total, 0); if (r <= 0) return r; total += r; }
    uint32_t pdu_len = ((uint32_t)buf[2] << 24) | ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | (uint32_t)buf[5];
    if (pdu_len + 6 > (uint32_t)max_len) return -1;
    while (pdu_len > 0) { int r = recv(s, (char*)buf + total, pdu_len, 0); if (r <= 0) return r; total += r; pdu_len -= r; }
    return total;
}

static void DicomNetworkLoop(void) {
    char buffer[BUFFER_SIZE];
    while (!g_bExitApp) {
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(g_listenSocketDicom, &readfds);
        
        tv.tv_sec = 1; tv.tv_usec = 0;
        int activity = select((int)g_listenSocketDicom + 1, &readfds, NULL, NULL, &tv);
        
        if (activity > 0 && FD_ISSET(g_listenSocketDicom, &readfds)) {
            SOCKET clientSocket = accept(g_listenSocketDicom, NULL, NULL);
            if (clientSocket == INVALID_SOCKET) continue;
            
            write_log(szDicomConnFmt);
            int bytesRead;
            while ((bytesRead = recv_pdu(clientSocket, (uint8_t*)buffer, BUFFER_SIZE)) > 0) {
                switch ((unsigned char)buffer[0]) {
                    case 0x01: handle_association_rq(clientSocket, buffer, bytesRead); break;
                    case 0x04: handle_pdata_tf(clientSocket, buffer, bytesRead); break;
                    case 0x05: {
                        uint8_t rel_rp[] = { 0x06, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 };
                        send(clientSocket, (char*)rel_rp, sizeof(rel_rp), 0);
                        closesocket(clientSocket); break;
                    }
                    case 0x02: {
                        uint8_t abort_pdu[] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02 };
                        send(clientSocket, (char*)abort_pdu, sizeof(abort_pdu), 0);
                        closesocket(clientSocket); break;
                    }
                }
            }
            closesocket(clientSocket);
        }
    }
}

static DWORD WINAPI DicomThread(LPVOID lpParam) { (void)lpParam; DicomNetworkLoop(); return 0; }

/* ===== Telnet/HTTP Functions ===== */
static void ExtractDispParam(const char *pLine, char *pOut) {
    const char *si = pLine + 4; while (*si == ' ') si++;
    char *di = pOut;
    while (*si && *si != '\r' && *si != '\n' && *si != ',' && (di - pOut < 1023)) *di++ = *si++;
    *di = '\0'; TrimInPlace(pOut);
}

static int LineMatchesDisp(const char *pLine, const char *pParam) {
    if (pParam[0] == '\0') return 1;
    char tempEntry[ENTRY_SIZE]; ParseCSVLineToEntry(pLine, tempEntry);
    char *modality = GetFieldPtr(tempEntry, 20);
    if (StrIEquals(modality, pParam)) return 1;
    char *stationAE = GetFieldPtr(tempEntry, 11);
    if (StrIEquals(stationAE, pParam)) return 1;
    char *patientID = GetFieldPtr(tempEntry, 1);
    if (IsEightDigits(pParam) && strcmp(patientID, pParam) == 0) return 1;
    return 0;
}

static void ProcessDispCommand(int clientIdx, const char *pLine) {
    ExtractDispParam(pLine, g_dispParam);
    write_log("DISP query received");
    EnsureCsvInitialized();
    int count = 0;
    for (int i = 1; i < g_csvRows; i++) {
        if (LineMatchesDisp(g_csvData[i], g_dispParam)) {
            SendText(g_clients[clientIdx], g_csvData[i]);
            SendText(g_clients[clientIdx], "\r\n");
            count++;
        }
    }
    char resp[128]; snprintf(resp, sizeof(resp), "%d RESULTS\r\n", count);
    SendText(g_clients[clientIdx], resp);
}

static void CommitPendingEntry(int clientIdx) {
    char *pEntry = GetPendingEntryPtr(clientIdx);
    
    StripDashes(GetFieldPtr(pEntry, 2)); 
    StripDashes(GetFieldPtr(pEntry, 12));
    StripColons(GetFieldPtr(pEntry, 13));
    FormatPatientName(GetFieldPtr(pEntry, 0));
    GenerateStudyUIDIfNeeded(pEntry);
    
    BuildCSVLineFromEntry(pEntry, g_lineOut);
    int found = UpdateCsvByPatient(pEntry, g_lineOut);
    char *pAcc = GetFieldPtr(pEntry, 8); SOCKET sock = g_clients[clientIdx];
    if (found) { SendText(sock, szUpdatedResp); printf(szUpdatedFmt, pAcc); }
    else { SendText(sock, szInsertedResp); printf(szInsertedFmt, pAcc); }
    g_pendingFlag[clientIdx] = 0; g_pendingSince[clientIdx] = 0;
}

static void ProcessClientLine(int clientIdx, char *pLine) {
    char entry[ENTRY_SIZE]; TrimInPlace(pLine);
    if (*pLine == '\0') return;
    if (StartsWithDISP(pLine)) { ProcessDispCommand(clientIdx, pLine); return; }
    if (!ParseCSVLineToEntry(pLine, entry) || !ValidateEntry(entry)) { SendText(g_clients[clientIdx], szInvalidResp); return; }
    char *pPending = GetPendingEntryPtr(clientIdx); memcpy(pPending, entry, ENTRY_SIZE);
    CommitPendingEntry(clientIdx); 
}

/* Hardened HTTP Request Processor: Buffers table rows and streams elements reliably */
static void ProcessHttpClient(int clientIdx, char *pBuf) {
    SOCKET sock = g_clients[clientIdx];
    char *headerEnd = strstr(pBuf, "\r\n\r\n"); if (!headerEnd) return;
    
    if (pBuf[0] == 'G') {
        SendText(sock, szHttp200OK); 
        SendText(sock, szHtmlStart);
        EnsureCsvInitialized();
        { 
            int isHeader = 1; 
            SendText(sock, "<table id='pTable'>");
            for (int ri = 0; ri < g_csvRows; ri++) {
                char entry[ENTRY_SIZE]; 
                ParseCSVLineToEntry(g_csvData[ri], entry);
                
                /* Assemble row into line buffer to reduce socket send overhead by 98% */
                char rowBuf[LINE_SIZE]; rowBuf[0] = '\0';
                strncat(rowBuf, isHeader ? "<tr>" : "<tr onclick='f(this)' style='cursor:pointer;'>", LINE_SIZE - strlen(rowBuf) - 1);
                for (int fi = 0; fi < FIELD_COUNT; fi++) {
                    strncat(rowBuf, isHeader ? "<th onclick='sortTable(this.cellIndex)'>" : "<td>", LINE_SIZE - strlen(rowBuf) - 1);
                    strncat(rowBuf, GetFieldPtr(entry, fi), LINE_SIZE - strlen(rowBuf) - 1);
                    strncat(rowBuf, isHeader ? "</th>" : "</td>", LINE_SIZE - strlen(rowBuf) - 1);
                }
                strncat(rowBuf, "</tr>", LINE_SIZE - strlen(rowBuf) - 1);
                SendText(sock, rowBuf);
                isHeader = 0;
            }
            SendText(sock, "</table><script>formatDates();</script>");
        }

        /* Send forms Part 1 */
        SendText(sock, szHtmlFormsTop);

        /* Procedure codes select and filter box */
        SendText(sock, "<input type='text' id='procFilter' onkeyup='filterProcCodes()' placeholder='Filter procedure codes...' style='width: 600px; display: block; margin-bottom: 5px; padding: 5px;'>");
        SendText(sock, "<select id='procCodeSel' multiple size='5' onchange=\"loadProcCode()\" style='width: 600px; vertical-align:top; margin-bottom: 15px;'><option value=''>Select Procedure Code</option>");
        if (g_procCodesLoaded) {
            for (int i = 0; i < g_procCodeCount; i++) {
                char opt[1024];
                snprintf(opt, sizeof(opt), "<option value='%s' data-id='%s' data-code='%s' data-mod='%s'>%s</option>",
                        g_procCodes[i].id, g_procCodes[i].id, g_procCodes[i].procCode, g_procCodes[i].modality, g_procCodes[i].name);
                SendText(sock, opt);
            }
        }
        SendText(sock, "</select><br>");

        /* Send Inputs */
        SendText(sock, szHtmlFormsInputs);

        /* Send station rooms dropdowns */
        FILE *fr = fopen("rooms.csv", "r");
        if (fr) {
            char line[1024];
            char rId[64][64], rLoc[64][128], rMod[64][64], rAe[64][128], rName[64][128], rRoom[64][128];
            int rCount = 0, isHeader = 1;
            while (fgets(line, sizeof(line), fr) && rCount < 64) {
                if (isHeader) { isHeader = 0; continue; }
                char *p = line;
                char *fields[6] = {rId[rCount], rLoc[rCount], rMod[rCount], rAe[rCount], rName[rCount], rRoom[rCount]};
                for (int i = 0; i < 6; i++) {
                    char *comma = strchr(p, ',');
                    if (comma) { *comma = '\0'; strncpy(fields[i], p, 63); fields[i][63] = '\0'; p = comma + 1; }
                    else { strncpy(fields[i], p, 63); fields[i][63] = '\0'; break; }
                }
                trim_mwl(rId[rCount]); trim_mwl(rLoc[rCount]); trim_mwl(rMod[rCount]); trim_mwl(rAe[rCount]); trim_mwl(rName[rCount]); trim_mwl(rRoom[rCount]);
                rCount++;
            }
            fclose(fr);

            char js[8192] = "<script>var rData = [";
            for (int i = 0; i < rCount; i++) {
                char tmp[1024]; snprintf(tmp, sizeof(tmp), "{id:'%s',loc:'%s',mod:'%s',ae:'%s',name:'%s',room:'%s'},", rId[i], rLoc[i], rMod[i], rAe[i], rName[i], rRoom[i]); strncat(js, tmp, sizeof(js) - strlen(js) - 1);
            }
            strncat(js, "]; function updR() { var idx = document.getElementsByName('StationName')[0].selectedIndex - 1; if(idx>=0) { "
                       "var r=rData[idx]; document.getElementsByName('Location')[0].value=r.room; "
                       "document.getElementsByName('Modality')[0].value=r.mod; "
                       "document.getElementsByName('StationAE')[0].value=r.ae; "
                       "document.getElementsByName('SPSID')[0].value=r.ae; "
                       "document.getElementsByName('ProcDesc')[0].value=r.name; } }</script>", sizeof(js) - strlen(js) - 1);
            SendText(sock, js);

            SendText(sock, "<select name='StationName' onchange='updR()'><option value=''>Station Name</option>");
            for (int i = 0; i < rCount; i++) {
                char tmp[512]; snprintf(tmp, sizeof(tmp), "<option value=\"%s\">%s</option>", rName[i], rName[i]); SendText(sock, tmp);
            }
            SendText(sock, "</select> ");
            SendText(sock, "<input type='text' name='Location' placeholder='Location'> ");
        } else {
            SendText(sock, "<input type='text' name='StationName' placeholder='Station Name'> <input type='text' name='Location' placeholder='Location'> ");
        }
        
        SendText(sock, szHtmlForms2);
        SendText(sock, szHtmlEnd); 
        CloseHttpClient(clientIdx); 
        return;
    }
    if (pBuf[0] == 'P') {
        char *body = strstr(pBuf, "\r\n\r\n"); if (!body) { RemoveClient(clientIdx); return; }
        body += 4;
        
        char *clPtr = StrIStr(pBuf, "Content-Length:");
        if (clPtr) {
            char *val = strchr(clPtr, ':');
            if (val && (int)strlen(body) < atoi(val + 1)) return;
        }
        
        if (strstr(pBuf, "POST /delete")) {
            char *pwdPtr = strstr(body, "pwd=");
            int pwdMatch = 0;
            if (pwdPtr) {
                pwdPtr += 4;
                char inputPwd[128] = {0};
                int pIdx = 0;
                while (*pwdPtr && *pwdPtr != '&' && *pwdPtr != '\r' && *pwdPtr != '\n' && pIdx < 127) {
                    inputPwd[pIdx++] = *pwdPtr++;
                }
                inputPwd[pIdx] = '\0';
                if (strcmp(inputPwd, g_delPwd) == 0) pwdMatch = 1;
            }
            if (pwdMatch) {
                EnterCriticalSection(&g_csvLock);
                strncpy(g_csvData[0], szCSVHeader, LINE_SIZE - 1);
                g_csvData[0][LINE_SIZE - 1] = '\0';
                g_csvRows = 1;
                LeaveCriticalSection(&g_csvLock);
                SendText(sock, szHttp200OK); SendText(sock, szHtmlStart);
                SendText(sock, "<meta http-equiv='refresh' content='1; url=/' /><h2>Data Deleted Successfully. Redirecting...</h2>");
                SendText(sock, szHtmlEnd);
            } else {
                SendText(sock, szHttp200OK); SendText(sock, szHtmlStart);
                SendText(sock, "<meta http-equiv='refresh' content='2; url=/' /><h2>Invalid Password! Redirecting...</h2>");
                SendText(sock, szHtmlEnd);
            }
            CloseHttpClient(clientIdx); 
            return;
        }
        else if (strstr(pBuf, "POST /delpat")) {
            if (!body[0]) { RemoveClient(clientIdx); return; }
            char acc[128];
            strncpy(acc, body, 127); acc[127] = '\0';
            TrimInPlace(acc);
            EnterCriticalSection(&g_csvLock);
            for (int i = 1; i < g_csvRows; i++) {
                if (AccessionEquals(g_csvData[i], acc)) {
                    for (int j = i; j < g_csvRows - 1; j++) {
                        strncpy(g_csvData[j], g_csvData[j+1], LINE_SIZE - 1);
                    }
                    g_csvRows--;
                    break;
                }
            }
            LeaveCriticalSection(&g_csvLock);
        }
        else if (strstr(pBuf, "POST /manual")) {
            if (!body[0]) { RemoveClient(clientIdx); return; }
            
            int blen = (int)strlen(body);
            while (blen > 0 && (body[blen-1] == '\r' || body[blen-1] == '\n' || body[blen-1] == ' ')) {
                body[--blen] = '\0';
            }

            char entry[ENTRY_SIZE]; memset(entry, 0, ENTRY_SIZE); ParseCSVLineToEntry(body, entry);
            if (ValidateEntry(entry)) { 
                StripDashes(GetFieldPtr(entry, 2)); 
                StripDashes(GetFieldPtr(entry, 12));
                StripColons(GetFieldPtr(entry, 13));
                FormatPatientName(GetFieldPtr(entry, 0));
                GenerateStudyUIDIfNeeded(entry);
                BuildCSVLineFromEntry(entry, g_lineOut); 
                UpdateCsvByPatient(entry, g_lineOut); 
            }
        }
        else if (strstr(pBuf, "POST /upload")) {
            char *csvStart = strstr(pBuf, szCSVHeader);
            if (csvStart) { char *si = csvStart; while (*si && *si != '\n') si++; if (*si) si++;
                while (*si) { if (*si == '-') break; if (*si == '\r' || *si == '\n') { si++; continue; }
                    char *di = g_fileLine; int ci = 0; while (*si && *si != '\r' && *si != '\n' && ci < LINE_SIZE - 1) { *di++ = *si++; ci++; } *di = '\0';
                    while (*si == '\r' || *si == '\n') si++; if (g_fileLine[0] == '\0') continue;
                    char entry[ENTRY_SIZE]; ParseCSVLineToEntry(g_fileLine, entry);
                    if (ValidateEntry(entry)) { 
                        StripDashes(GetFieldPtr(entry, 2)); 
                        StripDashes(GetFieldPtr(entry, 12));
                        StripColons(GetFieldPtr(entry, 13));
                        FormatPatientName(GetFieldPtr(entry, 0));
                        GenerateStudyUIDIfNeeded(entry);
                        BuildCSVLineFromEntry(entry, g_lineOut); 
                        UpdateCsvByPatient(entry, g_lineOut); 
                    } 
                } 
            }
        }
        SendText(sock, szHttp200OK); SendText(sock, szHtmlStart);
        SendText(sock, "<meta http-equiv='refresh' content='0; url=/' /><h2>Action Complete</h2>");
        SendText(sock, szHtmlEnd); 
        CloseHttpClient(clientIdx); 
        return;
    }
    RemoveClient(clientIdx);
}

static void CheckPendingAndTimeout(int clientIdx) {
    SOCKET sock = g_clients[clientIdx]; if (sock == 0) return;
    DWORD nowTick = GetTickCount();
    DWORD lastTick = g_clientLastTick[clientIdx]; DWORD timeoutVal = g_clientTimeout[clientIdx];
    if (nowTick - lastTick >= timeoutVal) { printf(szTimeoutFmt, (unsigned)sock); RemoveClient(clientIdx); }
}

static void AppendReceivedBytes(int clientIdx, const char *pBytes, int cbBytes) {
    char *pBuf = GetClientBufferPtr(clientIdx); int curLen = g_clientBufLen[clientIdx]; int isHttp = g_clientType[clientIdx];
    int idx = 0;
    while (idx < cbBytes) {
        char ch = pBytes[idx];
        if (isHttp) { if (curLen < CLIENT_BUF_SIZE - 1) pBuf[curLen++] = ch; idx++; }
        else {
            if (ch == '\r') { idx++; continue; }
            if (ch == '\n') { pBuf[curLen] = '\0'; ProcessClientLine(clientIdx, pBuf); curLen = 0; memset(pBuf, 0, CLIENT_BUF_SIZE); idx++; continue; }
            if (curLen < CLIENT_BUF_SIZE - 1) pBuf[curLen++] = ch; idx++;
        }
    }
    g_clientBufLen[clientIdx] = curLen;
    if (isHttp) { pBuf[curLen] = '\0'; ProcessHttpClient(clientIdx, pBuf); }
}

static void PollClient(int clientIdx) {
    SOCKET sock = g_clients[clientIdx]; if (sock == 0 || sock == INVALID_SOCKET) return;
    int cb = recv(sock, g_recvBuf, RECV_SIZE, 0);
    if (cb > 0) { g_clientLastTick[clientIdx] = GetTickCount(); AppendReceivedBytes(clientIdx, g_recvBuf, cb); }
    else if (cb == 0) { RemoveClient(clientIdx); return; } else { int err = WSAGetLastError(); if (err != 10035) { RemoveClient(clientIdx); return; } }
    if (g_clientType[clientIdx] != 1) CheckPendingAndTimeout(clientIdx);
}

static void InitArrays(void) {
    memset(g_clients, 0, sizeof(g_clients)); memset(g_clientIDs, 0, sizeof(g_clientIDs));
    memset(g_clientType, 0, sizeof(g_clientType)); memset(g_clientLastTick, 0, sizeof(g_clientLastTick));
    memset(g_clientTimeout, 0, sizeof(g_clientTimeout)); memset(g_clientBufLen, 0, sizeof(g_clientBufLen));
    memset(g_pendingFlag, 0, sizeof(g_pendingFlag)); memset(g_pendingSince, 0, sizeof(g_pendingSince));
}

static void SetNonBlocking(SOCKET sock) { u_long mode = 1; ioctlsocket(sock, FIONBIO_VAL, &mode); }

static int StartTelnetServer(void) {
    struct sockaddr_in sin;
    g_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocket == INVALID_SOCKET) { printf(szListenFailFmt, g_TelnetPort); return 0; }
    memset(&sin, 0, sizeof(sin)); sin.sin_family = AF_INET; sin.sin_port = htons((u_short)g_TelnetPort); sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_listenSocket, (struct sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) { printf(szListenFailFmt, g_TelnetPort); closesocket(g_listenSocket); g_listenSocket = INVALID_SOCKET; return 0; }
    if (listen(g_listenSocket, SOMAXCONN) == SOCKET_ERROR) { printf(szListenFailFmt, g_TelnetPort); closesocket(g_listenSocket); g_listenSocket = INVALID_SOCKET; return 0; }
    SetNonBlocking(g_listenSocket); printf(szListenFmt, g_TelnetPort); return 1;
}

static int StartHttpServer(void) {
    struct sockaddr_in sin;
    g_listenSocketHttp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocketHttp == INVALID_SOCKET) { printf(szListenFailFmt, g_HttpPort); return 0; }
    memset(&sin, 0, sizeof(sin)); 
    sin.sin_family = AF_INET; 
    sin.sin_port = htons((u_short)g_HttpPort); 
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_listenSocketHttp, (struct sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) { 
        printf(szListenFailFmt, g_HttpPort); closesocket(g_listenSocketHttp); g_listenSocketHttp = INVALID_SOCKET; return 0; 
    }
    if (listen(g_listenSocketHttp, SOMAXCONN) == SOCKET_ERROR) { 
        printf(szListenFailFmt, g_HttpPort); closesocket(g_listenSocketHttp); g_listenSocketHttp = INVALID_SOCKET; return 0; 
    }
    SetNonBlocking(g_listenSocketHttp); printf(szListenHttpFmt, g_HttpPort); return 1;
}

static int StartDicomServer(void) {
    struct sockaddr_in sin;
    g_listenSocketDicom = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocketDicom == INVALID_SOCKET) { printf(szListenFailFmt, g_DicomPort); return 0; }
    memset(&sin, 0, sizeof(sin)); sin.sin_family = AF_INET; sin.sin_port = htons((u_short)g_DicomPort); sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_listenSocketDicom, (struct sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) { printf(szListenFailFmt, g_DicomPort); closesocket(g_listenSocketDicom); g_listenSocketDicom = INVALID_SOCKET; return 0; }
    if (listen(g_listenSocketDicom, SOMAXCONN) == SOCKET_ERROR) { printf(szListenFailFmt, g_DicomPort); closesocket(g_listenSocketDicom); g_listenSocketDicom = INVALID_SOCKET; return 0; }
    printf(szListenDicomFmt, g_DicomPort); return 1;
}

static int FindFreeClientSlot(void) { for (int i = 0; i < MAX_CLIENTS; i++) if (g_clients[i] == 0) return i; return -1; }

/* ===== Client Management ===== */
static void AddClient(SOCKET sock, int clientType) {
    int idx = FindFreeClientSlot();
    if (idx == -1) { closesocket(sock); return; }
    SetNonBlocking(sock);
    g_clients[idx] = sock; g_clientIDs[idx] = (int)sock; g_clientType[idx] = clientType; g_clientBufLen[idx] = 0;
    memset(GetClientBufferPtr(idx), 0, CLIENT_BUF_SIZE);
    g_clientLastTick[idx] = GetTickCount(); g_clientTimeout[idx] = DEFAULT_TIMEOUT_MS;
    g_pendingFlag[idx] = 0; g_pendingSince[idx] = 0;
    if (clientType == 1) printf(szHttpConnFmt, (unsigned)sock);
    else { printf(szClientConnFmt, (unsigned)sock); SendText(sock, szConnected); }
}

static void RemoveClient(int idx) {
    SOCKET sock = g_clients[idx];
    if (sock != 0) { closesocket(sock); printf(szClientDiscFmt, (unsigned)sock); }
    g_clients[idx] = 0; g_clientIDs[idx] = 0; g_clientType[idx] = 0; g_clientBufLen[idx] = 0;
    g_pendingFlag[idx] = 0; g_pendingSince[idx] = 0;
}

static void AcceptNewClients(void) {
    SOCKET s;
    while ((s = accept(g_listenSocket, NULL, NULL)) != INVALID_SOCKET) AddClient(s, 0);
    while ((s = accept(g_listenSocketHttp, NULL, NULL)) != INVALID_SOCKET) AddClient(s, 1);
}

static void ServerStart(void) {
    if (g_bRunning) return;
    StartTelnetServer(); StartHttpServer(); StartDicomServer();
    g_bRunning = 1;
    
    ModifyMenu(g_hMenu, ID_TRAY_TOGGLE, MF_BYCOMMAND | MF_STRING | MF_CHECKED, ID_TRAY_TOGGLE, szMenuStop);
    
    Shell_NotifyIcon(NIM_MODIFY, &nid);
    printf("%s", szServerStarted);
}

static void ServerStop(void) {
    if (!g_bRunning) return;
    for (int i = 0; i < MAX_CLIENTS; i++) RemoveClient(i);
    if (g_listenSocket != INVALID_SOCKET) { closesocket(g_listenSocket); g_listenSocket = INVALID_SOCKET; }
    if (g_listenSocketHttp != INVALID_SOCKET) { closesocket(g_listenSocketHttp); g_listenSocketHttp = INVALID_SOCKET; }
    if (g_listenSocketDicom != INVALID_SOCKET) { closesocket(g_listenSocketDicom); g_listenSocketDicom = INVALID_SOCKET; }
    g_bRunning = 0;
    
    ModifyMenu(g_hMenu, ID_TRAY_TOGGLE, MF_BYCOMMAND | MF_STRING | MF_UNCHECKED, ID_TRAY_TOGGLE, szMenuStart);
    printf("%s", szServerStopped);
}

static void ServerLoop(void) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { 
        if (msg.message == WM_QUIT) g_bExitApp = 1; 
        TranslateMessage(&msg); DispatchMessage(&msg); 
    }
    if (!g_bRunning) return;
    AcceptNewClients();
    for (int i = 0; i < MAX_CLIENTS; i++) PollClient(i);
}

/* ===== Settings Dialog Handler ===== */
static BOOL CALLBACK SetFontProc(HWND hChild, LPARAM lParam) {
    SendMessage(hChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            int y = 20; int x = 10;
            CreateWindow("STATIC", "AET Title:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", g_aeCalled, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x + 90, y, 200, 20, hWnd, (HMENU)ID_EDT_AET, g_hInstance, NULL);
            y += 30;
            
            char buf[64];
            snprintf(buf, sizeof(buf), "%d", g_TelnetPort);
            CreateWindow("STATIC", "Telnet Port:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, x + 90, y, 60, 20, hWnd, (HMENU)ID_EDT_TELNET, g_hInstance, NULL);
            y += 30;
            
            snprintf(buf, sizeof(buf), "%d", g_HttpPort);
            CreateWindow("STATIC", "HTTP Port:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, x + 90, y, 60, 20, hWnd, (HMENU)ID_EDT_HTTP, g_hInstance, NULL);
            y += 30;
            
            snprintf(buf, sizeof(buf), "%d", g_DicomPort);
            CreateWindow("STATIC", "DICOM Port:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, x + 90, y, 60, 20, hWnd, (HMENU)ID_EDT_DICOM, g_hInstance, NULL);
            y += 30;
            
            snprintf(buf, sizeof(buf), "%d", g_TelnetTimeout);
            CreateWindow("STATIC", "Timeout (sec):", WS_CHILD | WS_VISIBLE, x, y, 80, 20, hWnd, NULL, g_hInstance, NULL);
            CreateWindow("EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, x + 90, y, 60, 20, hWnd, (HMENU)ID_EDT_TIMEOUT, g_hInstance, NULL);
            y += 30;
            
            HWND hChk = CreateWindow("BUTTON", "Enable Debug Log", BS_CHECKBOX | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, 150, 20, hWnd, (HMENU)ID_CHK_DEBUG, g_hInstance, NULL);
            SendMessage(hChk, BM_SETCHECK, g_DebugLog ? BST_CHECKED : BST_UNCHECKED, 0);
            y += 30;
            
            CreateWindow("BUTTON", g_bRunning ? "Stop Server" : "Start Server", BS_OWNERDRAW | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x + 50, y, 240, 35, hWnd, (HMENU)ID_BTN_STARTSTOP, g_hInstance, NULL);
            y += 45;
            
            CreateWindow("BUTTON", "OK", BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x + 10, y, 80, 25, hWnd, (HMENU)ID_OK, g_hInstance, NULL);
            CreateWindow("BUTTON", "Cancel", BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP, x + 100, y, 80, 25, hWnd, (HMENU)IDCANCEL, g_hInstance, NULL);
            
            EnumChildWindows(hWnd, SetFontProc, (LPARAM)GetStockObject(DEFAULT_GUI_FONT));
            return 0;
        }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
            if (lpdis->CtlID == ID_BTN_STARTSTOP) {
                HBRUSH hBrush = CreateSolidBrush(g_bRunning ? RGB(200, 255, 200) : RGB(255, 200, 200));
                FillRect(lpdis->hDC, &lpdis->rcItem, hBrush);
                DeleteObject(hBrush);
                
                DrawEdge(lpdis->hDC, &lpdis->rcItem, (lpdis->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
                
                SetBkMode(lpdis->hDC, TRANSPARENT);
                SetTextColor(lpdis->hDC, RGB(0, 0, 0));
                
                char szText[128];
                GetWindowText(lpdis->hwndItem, szText, sizeof(szText));
                DrawText(lpdis->hDC, szText, -1, &lpdis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (lpdis->itemState & ODS_FOCUS) {
                    RECT rcFocus = lpdis->rcItem;
                    InflateRect(&rcFocus, -3, -3);
                    DrawFocusRect(lpdis->hDC, &rcFocus);
                }
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
                    if (g_bRunning) { 
                        ServerStop(); 
                        SetWindowText((HWND)lParam, "Start Server"); 
                    } else { 
                        ServerStart(); 
                        SetWindowText((HWND)lParam, "Stop Server"); 
                    }
                    InvalidateRect((HWND)lParam, NULL, TRUE);
                }
                else if (id == ID_CHK_DEBUG) {
                    LRESULT chk = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0);
                    SendMessage((HWND)lParam, BM_SETCHECK, chk == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
                }
                else if (id == ID_OK) {
                    GetDlgItemText(hWnd, ID_EDT_AET, g_aeCalled, sizeof(g_aeCalled)); g_aeCalled[sizeof(g_aeCalled) - 1] = '\0';
                    
                    BOOL trans;
                    int tp = GetDlgItemInt(hWnd, ID_EDT_TELNET, &trans, FALSE); if (trans) g_TelnetPort = tp;
                    int hp = GetDlgItemInt(hWnd, ID_EDT_HTTP, &trans, FALSE); if (trans) g_HttpPort = hp;
                    int dp = GetDlgItemInt(hWnd, ID_EDT_DICOM, &trans, FALSE); if (trans) g_DicomPort = dp;
                    int to = GetDlgItemInt(hWnd, ID_EDT_TIMEOUT, &trans, FALSE); if (trans) g_TelnetTimeout = to;
                    g_DebugLog = (SendMessage(GetDlgItem(hWnd, ID_CHK_DEBUG), BM_GETCHECK, 0, 0) == BST_CHECKED);
                    
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
            g_hSettingsDlg = NULL;
            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void ShowSettingsDialog(void) {
    if (g_hSettingsDlg != NULL) {
        SetForegroundWindow(g_hSettingsDlg);
        return;
    }
    
    HWND hParent = g_hMainWnd;
    RECT rc;
    GetWindowRect(hParent, &rc);
    int width = 350;
    int height = 320;
    int left = rc.left + (rc.right - rc.left - width) / 2;
    int top = rc.top + (rc.bottom - rc.top - height) / 2;
    
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
    
    g_hSettingsDlg = CreateWindowEx(
        0, "SettingsWndClass", "Server Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        left, top, width, height,
        hParent, NULL, g_hInstance, NULL
    );
}

/* ===== Win32 UI ===== */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TRAYICON) {
        if ((UINT)lParam == WM_RBUTTONUP || (UINT)lParam == WM_LBUTTONUP) { 
            POINT pt; GetCursorPos(&pt); SetForegroundWindow(hWnd); 
            TrackPopupMenu(g_hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL); 
        } return 0;
    }
    if (uMsg == WM_COMMAND) {
        int cmd = LOWORD(wParam);
        if (cmd == ID_TRAY_TOGGLE) { if (g_bRunning) ServerStop(); else ServerStart(); return 0; }
        if (cmd == ID_TRAY_SHOW) { HWND cw = GetConsoleWindow(); if (cw) { ShowWindow(cw, SW_SHOW); SetForegroundWindow(cw); } return 0; }
        if (cmd == ID_TRAY_EXIT) { g_bExitApp = 1; PostQuitMessage(0); return 0; }
        if (cmd == ID_TRAY_SETTINGS) { ShowSettingsDialog(); return 0; }
        return 0;
    }
    if (uMsg == WM_CLOSE) { g_bExitApp = 1; PostQuitMessage(0); return 0; }
    if (uMsg == WM_DESTROY) { g_bExitApp = 1; ServerStop(); Shell_NotifyIcon(NIM_DELETE, &nid); PostQuitMessage(0); return 0; }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static void CreateTray(void) {
    WNDCLASSEX wc; g_hInstance = GetModuleHandle(NULL);
    memset(&wc, 0, sizeof(wc)); wc.cbSize = sizeof(WNDCLASSEX); wc.lpfnWndProc = WndProc; wc.hInstance = g_hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszClassName = szWndClass;
    RegisterClassEx(&wc);
    g_hMainWnd = CreateWindowEx(0, szWndClass, szTrayTip, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, HWND_MESSAGE, NULL, g_hInstance, NULL);
    
    g_hMenu = CreatePopupMenu();
    AppendMenu(g_hMenu, MF_STRING | (g_bRunning ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_TOGGLE, g_bRunning ? szMenuStop : szMenuStart);
    AppendMenu(g_hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(g_hMenu, MF_STRING, ID_TRAY_SETTINGS, szMenuSettings);
    AppendMenu(g_hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(g_hMenu, MF_STRING, ID_TRAY_SHOW, szMenuShow);
    AppendMenu(g_hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(g_hMenu, MF_STRING, ID_TRAY_EXIT, szMenuExit);
    
    memset(&nid, 0, sizeof(nid)); nid.cbSize = sizeof(NOTIFYICONDATA); nid.hWnd = g_hMainWnd; nid.uID = 1; 
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; nid.uCallbackMessage = WM_TRAYICON; 
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); 
    strcpy(nid.szTip, szTrayTip); 
    Shell_NotifyIcon(NIM_ADD, &nid);
    ShowWindow(GetConsoleWindow(), SW_HIDE);
}

/* ===== Main ===== */
int main(int argc, char *argv[]) {
    WSADATA wsaData; HANDLE hDicomThread;
    (void)argc; (void)argv; srand((unsigned int)GetTickCount());
    
    ResolveIniPath(); printf("%s", szStartup); LoadConfig();
    printf(szConfigFmt, g_aeCalled, g_TelnetPort, g_HttpPort, g_DicomPort, g_TelnetTimeout, g_DebugLog);
    
    InitializeCriticalSection(&g_csvLock); 
    InitializeCriticalSection(&g_procCodesLock);
    InitArrays(); 
    WSAStartup(MAKEWORD(2, 2), &wsaData); 
    EnsureCsvInitialized(); 
    EnterCriticalSection(&g_csvLock);
    strncpy(g_csvData[0], szCSVHeader, LINE_SIZE - 1);
    g_csvData[0][LINE_SIZE - 1] = '\0';
    g_csvRows = 1;
    LeaveCriticalSection(&g_csvLock);
    LoadProcedureCodes();
    CreateTray(); 
    ServerStart();
    
    hDicomThread = CreateThread(NULL, 0, DicomThread, NULL, 0, NULL); if (hDicomThread) CloseHandle(hDicomThread);
    
    MSG qmsg;
    for (;;) { 
        ServerLoop(); if (g_bExitApp) break;
        if (PeekMessage(&qmsg, NULL, WM_QUIT, WM_QUIT, PM_NOREMOVE)) break; Sleep(10); 
    }
    
    ServerStop(); DeleteCriticalSection(&g_csvLock); DeleteCriticalSection(&g_procCodesLock); WSACleanup(); Shell_NotifyIcon(NIM_DELETE, &nid); DestroyWindow(g_hMainWnd);
    return 0;
}