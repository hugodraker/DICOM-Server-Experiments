/* ============================================================================
 * RIS Multi-Protocol Server (DICOM + Telnet + HTTP)
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 *
 * Compile:
 *   gcc -O2 -s -o RIS_SERVER.exe RIS_SERVER.c -lws2_32 -luser32 -lshell32 -lkernel32 -lgdi32
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
#include <ctype.h>

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

#define MAX_FIELD_LEN       512

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

/* Patient directory filtering variables */
typedef enum {
    ENCODING_UNKNOWN,
    ENCODING_UTF8,
    ENCODING_UTF8_BOM,
    ENCODING_UTF16_LE,
    ENCODING_UTF16_BE,
    ENCODING_ASCII
} Encoding;

static char* patients_csv_data = NULL;
static long patients_csv_data_len = 0;
static int pts_name_col_idx = -1;
static int pts_id_col_idx = 0;
static int pts_birthdate_col_idx = -1;
static int pts_sex_col_idx = -1;

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
"<div style='display:flex; gap:20px; margin-bottom:15px;'>"
"<div>"
"<input type='text' id='patientFilter' oninput='filterPatients()' placeholder='Filter patients...' style='width: 300px; display: block; margin-bottom: 5px; padding: 5px;'>"
"<select id='patientSel' size='5' onchange='loadPatientDetails(this.value)' style='width: 300px; vertical-align:top;'>"
"<option value=''>Select Patient</option></select></div>"
"<div>"
"<input type='text' id='procFilter' onkeyup='filterProcCodes()' placeholder='Filter procedure codes...' style='width: 300px; display: block; margin-bottom: 5px; padding: 5px;'>"
"<select id='procCodeSel' multiple size='5' onchange=\"loadProcCode()\" style='width: 300px; vertical-align:top;'><option value=''>Select Procedure Code</option>";

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
"let ptDebounce=null;let ptAbort=null;"
"function filterPatients(){"
"    var f=document.getElementById('patientFilter').value.trim();"
"    if(ptDebounce)clearTimeout(ptDebounce);"
"    ptDebounce=setTimeout(async function(){"
"        if(ptAbort)ptAbort.abort();"
"        ptAbort=new AbortController();"
"        try{"
"            var res=await fetch('/api/patients?filter='+encodeURIComponent(f),{signal:ptAbort.signal});"
"            if(!res.ok)return;"
"            var names=await res.json();"
"            var sel=document.getElementById('patientSel');"
"            sel.options.length=0;"
"            var defaultOpt=document.createElement('option');"
"            defaultOpt.value='';defaultOpt.text='Select Patient';"
"            sel.appendChild(defaultOpt);"
"            names.forEach(function(n){"
"                var opt=document.createElement('option');"
"                opt.value=n;opt.text=n;"
"                sel.appendChild(opt);"
"            });"
"        }catch(e){}"
"    },250);"
"}"
"async function loadPatientDetails(name){"
"    if(!name)return;"
"    try{"
"        var res=await fetch('/api/patient/'+encodeURIComponent(name));"
"        if(!res.ok)return;"
"        var data=await res.json();"
"        if(!data.error){"
"            document.getElementsByName('PatientName')[0].value=data.name||'';"
"            document.getElementsByName('PatientID')[0].value=data.patient_id||'';"
"            document.getElementsByName('BirthDate')[0].value=data.birthdate||'';"
"            document.getElementsByName('Sex')[0].value=data.sex||'';"
"        }"
"    }catch(e){}"
"}"
"window.onload=function(){newPatient();filterPatients();};</script></head><body style='font-family:Arial,sans-serif;padding:20px;'>";

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
static void RemoveClient(int idx) {
    if (idx >= 0 && idx < MAX_CLIENTS) {
        if (g_clients[idx] != INVALID_SOCKET && g_clients[idx] != 0) {
            closesocket(g_clients[idx]);
        }
        g_clients[idx] = 0;
        g_clientIDs[idx] = 0;
        g_clientType[idx] = 0;
    }
}

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

static void handle_pdata_tf(SOCKET clientSocket, char* buffer, int bytesRead, uint8_t *g_findCmd, int *g_findCmdLen, uint8_t *g_findData, int *g_findDataLen, uint8_t *g_findPcId, uint16_t *g_findMsgId, uint16_t *g_findCommand) {
    if (bytesRead < 6) return;

    uint32_t pdu_len = ((uint32_t)(uint8_t)buffer[2] << 24) |
                       ((uint32_t)(uint8_t)buffer[3] << 16) |
                       ((uint32_t)(uint8_t)buffer[4] << 8)  |
                        (uint32_t)(uint8_t)buffer[5];

    int pos = 6;               
    int end = 6 + (int)pdu_len;
    if (end > bytesRead) end = bytesRead;

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
            if (*g_findCmdLen + data_len < 16384) {
                memcpy(g_findCmd + *g_findCmdLen, buffer + data_off, data_len);
                *g_findCmdLen += data_len;
            }
            *g_findPcId = pc_id;

            if (is_last_frag) {
                *g_findCommand = 0;
                *g_findMsgId   = 0;
                for (int i = 0; i <= *g_findCmdLen - 10; i++) {
                    if (g_findCmd[i] == 0x00 && g_findCmd[i + 1] == 0x00 &&
                        g_findCmd[i + 2] == 0x00 && g_findCmd[i + 3] == 0x01 &&
                        g_findCmd[i + 4] == 0x02 && g_findCmd[i + 5] == 0x00) {
                        *g_findCommand = (uint16_t)g_findCmd[i + 8] |
                                        ((uint16_t)g_findCmd[i + 9] << 8);
                    }
                    if (g_findCmd[i] == 0x00 && g_findCmd[i + 1] == 0x00 &&
                        g_findCmd[i + 2] == 0x20 && g_findCmd[i + 3] == 0x01 &&
                        g_findCmd[i + 4] == 0x02 && g_findCmd[i + 5] == 0x00) {
                        *g_findMsgId = (uint16_t)g_findCmd[i + 8] |
                                      ((uint16_t)g_findCmd[i + 9] << 8);
                    }
                }

                if (*g_findCommand == 0x0030) {
                    handle_c_echo_rq(clientSocket, *g_findPcId, *g_findMsgId);
                    *g_findCmdLen  = 0;
                    *g_findDataLen = 0;
                }
            }
        } else {
            if (*g_findDataLen + data_len < 16384) {
                memcpy(g_findData + *g_findDataLen, buffer + data_off, data_len);
                *g_findDataLen += data_len;
            }

            if (is_last_frag) {
                if (*g_findCommand == 0x0020) {
                    handle_c_find_rq(clientSocket, *g_findPcId, *g_findMsgId,
                                     g_findData, *g_findDataLen);
                }
                *g_findCmdLen  = 0;
                *g_findDataLen = 0;
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
    uint8_t findCmd[16384]; int findCmdLen = 0;
    uint8_t findData[16384]; int findDataLen = 0;
    uint8_t findPcId = 0; uint16_t findMsgId = 0, findCommand = 0;
    
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
            findCmdLen = 0; findDataLen = 0; /* Fix context leak between client disconnects */
            
            while ((bytesRead = recv_pdu(clientSocket, (uint8_t*)buffer, BUFFER_SIZE)) > 0) {
                switch ((unsigned char)buffer[0]) {
                    case 0x01: handle_association_rq(clientSocket, buffer, bytesRead); break;
                    case 0x04: handle_pdata_tf(clientSocket, buffer, bytesRead, findCmd, &findCmdLen, findData, &findDataLen, &findPcId, &findMsgId, &findCommand); break;
                    case 0x05: {
                        uint8_t rel_rp[] = { 0x06, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 };
                        send(clientSocket, (char*)rel_rp, sizeof(rel_rp), 0);
                        goto client_disconnect;
                    }
                    case 0x02: {
                        uint8_t abort_pdu[] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02 };
                        send(clientSocket, (char*)abort_pdu, sizeof(abort_pdu), 0);
                        goto client_disconnect;
                    }
                }
            }
client_disconnect:
            closesocket(clientSocket);
        }
    }
}

static DWORD WINAPI DicomThread(LPVOID lpParam) { (void)lpParam; DicomNetworkLoop(); return 0; }

/* ===== Telnet Functions ===== */
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

/* ===== Patients Directory Filter Functions ===== */
static void url_decode(const char* src, char* dest, int max_len) {
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

static Encoding detect_encoding(const unsigned char* buf, long len) {
    if (len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) return ENCODING_UTF8_BOM;
    if (len >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) return ENCODING_UTF16_LE;
    if (len >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) return ENCODING_UTF16_BE;
    for (long i = 0; i < (len > 1024 ? 1024 : len); i++) {
        if (buf[i] > 127) return ENCODING_UTF8;
    }
    return ENCODING_ASCII;
}

static int utf16le_to_utf8(const wchar_t* input, char* output, int max_output) {
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

static int utf16be_to_utf8(const unsigned char* input, char* output, int max_output) {
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

static int str_icontains(const char* haystack, const char* needle) {
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

static int get_csv_field(const char* line, int target_col, char* out_buf, int max_len) {
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

static char* load_and_convert_csv(const char* filename, long* data_len) {
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
    
    switch (enc) {
        case ENCODING_UTF8_BOM:
            memcpy(converted, raw_buf, bytes_read + 1);
            *data_len = bytes_read - 3;
            memmove(converted, converted + 3, bytes_read - 2);
            break;
        case ENCODING_UTF8:
            memcpy(converted, raw_buf, bytes_read + 1);
            *data_len = bytes_read;
            break;
        case ENCODING_UTF16_LE:
            *data_len = utf16le_to_utf8((wchar_t*)(raw_buf + 2), converted, bytes_read);
            break;
        case ENCODING_UTF16_BE:
            *data_len = utf16be_to_utf8(raw_buf + 2, converted, bytes_read);
            break;
        default:
            memcpy(converted, raw_buf, bytes_read + 1);
            *data_len = bytes_read;
            break;
    }
    free(raw_buf);
    return converted;
}

static long get_line_end(const char* data, long start, long len) {
    long pos = start;
    while (pos < len && data[pos] != '\r' && data[pos] != '\n') pos++;
    return pos;
}

static void skip_newline(const char* data, long* pos, long len) {
    while (*pos < len && (data[*pos] == '\r' || data[*pos] == '\n')) (*pos)++;
}

static int init_patients_csv(void) {
    if (!patients_csv_data) {
        patients_csv_data = load_and_convert_csv("patients.csv", &patients_csv_data_len);
        if (!patients_csv_data || patients_csv_data_len == 0) {
            return -1;
        }
        long h_end = get_line_end(patients_csv_data, 0, patients_csv_data_len);
        char header[8192];
        long h_len = (h_end > 8191) ? 8191 : h_end;
        memcpy(header, patients_csv_data, h_len);
        header[h_len] = '\0';
        TrimInPlace(header);
        
        int col_idx = 0;
        char field_buf[MAX_FIELD_LEN];
        while (get_csv_field(header, col_idx, field_buf, sizeof(field_buf))) {
            if (StrIEquals(field_buf, "Name")) pts_name_col_idx = col_idx;
            else if (StrIEquals(field_buf, "Birthdate") || StrIEquals(field_buf, "DOB")) pts_birthdate_col_idx = col_idx;
            else if (StrIEquals(field_buf, "Sex") || StrIEquals(field_buf, "Gender")) pts_sex_col_idx = col_idx;
            col_idx++;
        }
        if (pts_name_col_idx < 0) return -1;
    }
    return 0;
}

static int get_patient_names(const char* filter, char* output, int max_output, int max_count) {
    if (init_patients_csv() != 0) {
        snprintf(output, max_output, "[]");
        return -1;
    }
    
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

static int get_patient_details(const char* patient_name, char* output, int max_output) {
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
                get_csv_field(line, pts_id_col_idx, id_buf, sizeof(id_buf));
                if (pts_birthdate_col_idx >= 0) get_csv_field(line, pts_birthdate_col_idx, birthdate_buf, sizeof(birthdate_buf));
                if (pts_sex_col_idx >= 0) get_csv_field(line, pts_sex_col_idx, sex_buf, sizeof(sex_buf));
                
                snprintf(output, max_output, "{\"patient_id\":\"%s\",\"name\":\"%s\",\"birthdate\":\"%s\",\"sex\":\"%s\"}", id_buf, name_buf, birthdate_buf, sex_buf);
                return 0;
            }
        }
        line_start = line_end;
        skip_newline(patients_csv_data, &line_start, patients_csv_data_len);
    }
    snprintf(output, max_output, "{\"error\": \"Patient not found\"}");
    return -1;
}

static void get_query_param(const char* url, const char* param_name, char* value, int max_value) {
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

/* HTTP Request Processor */
static void ProcessHttpClient(int clientIdx, char *pBuf) {
    SOCKET sock = g_clients[clientIdx];
    char *headerEnd = strstr(pBuf, "\r\n\r\n"); 
    if (!headerEnd) return;
    
    char method[16] = {0}, path[512] = {0}, query[256] = {0};
    sscanf(pBuf, "%15s %511s", method, path);
    char* qmark = strchr(path, '?');
    if (qmark) {
        *qmark = '\0';
        snprintf(query, sizeof(query), "%s", qmark + 1);
    }

    if (strncmp(path, "/api/patient/", 13) == 0) {
        char* raw_name = path + 13;
        char decoded_name[512] = {0};
        url_decode(raw_name, decoded_name, sizeof(decoded_name));
        
        char json_response[BUFFER_SIZE];
        int result = get_patient_details(decoded_name, json_response, sizeof(json_response));
        
        char hdr[256];
        if (result == 0) {
            snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\n\r\n");
        } else {
            snprintf(hdr, sizeof(hdr), "HTTP/1.1 404 Not Found\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\n\r\n");
        }
        SendText(sock, hdr);
        SendText(sock, json_response);
        CloseHttpClient(clientIdx);
        return;
    }
    else if (strncmp(path, "/api/patients", 13) == 0) {
        char filter[256] = {0};
        get_query_param(query, "filter", filter, sizeof(filter));
        
        char json_response[BUFFER_SIZE];
        int max_results = (strlen(filter) > 0) ? 50 : 10;
        int count = get_patient_names(filter, json_response, sizeof(json_response), max_results);
        
        if (count >= 0) {
            char hdr[256];
            snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\n\r\n");
            SendText(sock, hdr);
            SendText(sock, json_response);
        } else {
            SendText(sock, "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\": \"Server error\"}");
        }
        CloseHttpClient(clientIdx);
        return;
    }

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

        SendText(sock, szHtmlFormsTop);

        if (g_procCodesLoaded) {
            for (int i = 0; i < g_procCodeCount; i++) {
                char opt[1024];
                snprintf(opt, sizeof(opt), "<option value='%s' data-id='%s' data-code='%s' data-mod='%s'>%s</option>",
                        g_procCodes[i].id, g_procCodes[i].id, g_procCodes[i].procCode, g_procCodes[i].modality, g_procCodes[i].name);
                SendText(sock, opt);
            }
        }
        SendText(sock, "</select></div></div><form id='manForm'>");
        SendText(sock, szHtmlFormsInputs);
        SendText(sock, szHtmlForms2);
        SendText(sock, szHtmlEnd);
    }
    
    CloseHttpClient(clientIdx);
}

static void AcceptClient(SOCKET serverSocket, int type) {
    SOCKET newSock = accept(serverSocket, NULL, NULL);
    if (newSock == INVALID_SOCKET) return;
    
    u_long mode = 1;
    ioctlsocket(newSock, FIONBIO, &mode);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] == 0) {
            g_clients[i] = newSock;
            g_clientType[i] = type;
            g_clientLastTick[i] = GetTickCount();
            g_clientTimeout[i] = (type == 1) ? (g_TelnetTimeout * 1000) : DEFAULT_TIMEOUT_MS;
            g_clientBufLen[i] = 0;
            g_pendingFlag[i] = 0;
            if (type == 1) SendText(newSock, szConnected);
            return;
        }
    }
    closesocket(newSock);
}

static DWORD WINAPI NetworkThread(LPVOID lpParam) {
    (void)lpParam;
    while (!g_bExitApp) {
        fd_set readfds;
        FD_ZERO(&readfds);
        SOCKET maxSocket = 0;
        
        if (g_listenSocket != INVALID_SOCKET) { FD_SET(g_listenSocket, &readfds); maxSocket = max(maxSocket, g_listenSocket); }
        if (g_listenSocketHttp != INVALID_SOCKET) { FD_SET(g_listenSocketHttp, &readfds); maxSocket = max(maxSocket, g_listenSocketHttp); }
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i] != 0 && g_clients[i] != INVALID_SOCKET) {
                FD_SET(g_clients[i], &readfds);
                maxSocket = max(maxSocket, g_clients[i]);
            }
        }
        
        struct timeval tv = {1, 0};
        if (select((int)maxSocket + 1, &readfds, NULL, NULL, &tv) > 0) {
            if (g_listenSocket != INVALID_SOCKET && FD_ISSET(g_listenSocket, &readfds)) AcceptClient(g_listenSocket, 1);
            if (g_listenSocketHttp != INVALID_SOCKET && FD_ISSET(g_listenSocketHttp, &readfds)) AcceptClient(g_listenSocketHttp, 2);
            
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (g_clients[i] != 0 && FD_ISSET(g_clients[i], &readfds)) {
                    int r = recv(g_clients[i], g_recvBuf, RECV_SIZE - 1, 0);
                    if (r > 0) {
                        g_clientLastTick[i] = GetTickCount();
                        if (g_clientType[i] == 2) {
                            g_recvBuf[r] = '\0';
                            ProcessHttpClient(i, g_recvBuf);
                        } else {
                            for (int j = 0; j < r; j++) {
                                if (g_recvBuf[j] == '\n' || g_recvBuf[j] == '\r') {
                                    if (g_clientBufLen[i] > 0) {
                                        char *pBuf = GetClientBufferPtr(i);
                                        pBuf[g_clientBufLen[i]] = '\0';
                                        ProcessClientLine(i, pBuf);
                                        g_clientBufLen[i] = 0;
                                    }
                                } else {
                                    if (g_clientBufLen[i] < CLIENT_BUF_SIZE - 1) {
                                        GetClientBufferPtr(i)[g_clientBufLen[i]++] = g_recvBuf[j];
                                    }
                                }
                            }
                        }
                    } else if (r <= 0) {
                        RemoveClient(i);
                    }
                }
            }
        }
    }
    return 0;
}

static void InitializeServer() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    InitializeCriticalSection(&g_csvLock);
    InitializeCriticalSection(&g_procCodesLock);
    LoadConfig();
    EnsureCsvInitialized();
    LoadProcedureCodes();
    
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    
    g_listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_port = htons(g_TelnetPort);
    bind(g_listenSocket, (struct sockaddr*)&server, sizeof(server));
    listen(g_listenSocket, SOMAXCONN);
    
    g_listenSocketHttp = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_port = htons(g_HttpPort);
    bind(g_listenSocketHttp, (struct sockaddr*)&server, sizeof(server));
    listen(g_listenSocketHttp, SOMAXCONN);

    g_listenSocketDicom = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_port = htons(g_DicomPort);
    bind(g_listenSocketDicom, (struct sockaddr*)&server, sizeof(server));
    listen(g_listenSocketDicom, SOMAXCONN);
    
    CreateThread(NULL, 0, NetworkThread, NULL, 0, NULL);
    CreateThread(NULL, 0, DicomThread, NULL, 0, NULL);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    InitializeServer();
    printf("Server running. Press Enter to stop.\n");
    getchar();
    g_bExitApp = 1;
    WSACleanup();
    return 0;
}