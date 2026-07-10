; THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
; AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.

;.c:\masm32\bin\ml /c /coff /Cp WORKLIST_ADDCSV.asm
;.c:\masm32\bin\link /subsystem:console WORKLIST_ADDCSV.obj
.586
.model flat, stdcall
option casemap:none
include \masm32\include\windows.inc
include \masm32\include\kernel32.inc
include \masm32\include\user32.inc
include \masm32\include\shell32.inc
include \masm32\include\ws2_32.inc
include \masm32\include\msvcrt.inc
include \masm32\macros\macros.asm    ; Required for CStr macro

includelib \masm32\lib\kernel32.lib
includelib \masm32\lib\user32.lib
includelib \masm32\lib\shell32.lib
includelib \masm32\lib\ws2_32.lib
includelib \masm32\lib\msvcrt.lib

CStr MACRO text:VARARG
    LOCAL lbl
    .const
    lbl db text, 0
    .code
    EXITM <OFFSET lbl>
ENDM

; Forward Prototyping for forward-referenced procedures
RemoveClient PROTO :DWORD
IsDateRangeParam PROTO :DWORD
ProcessHttpClient PROTO :DWORD, :DWORD
StripDashes PROTO :DWORD

MAX_CLIENTS        equ 32  ; Increased to handle HTTP + Telnet concurrently
RECV_SIZE          equ 2048
CLIENT_BUF_SIZE    equ 8192 ; Increased for HTTP requests
FIELD_COUNT        equ 23
FIELD_SIZE         equ 256
ENTRY_SIZE         equ FIELD_COUNT * FIELD_SIZE
LINE_SIZE          equ 16384
APPEND_DELAY_MS    equ 2000
DEFAULT_TIMEOUT_MS equ 10000
FIONBIO            equ 8004667Eh
WSAEWOULDBLOCK     equ 10035

WM_TRAYICON        equ WM_USER + 1
ID_TRAY_TOGGLE     equ 1000
ID_TRAY_SHOW       equ 1001
ID_TRAY_EXIT       equ 1002

.data
g_TelnetPort        dd 23
g_HttpPort          dd 80
g_TelnetTimeout     dd 10
g_DebugLog          dd 1
g_bRunning          dd 0
g_listenSocket      dd INVALID_SOCKET
g_listenSocketHttp  dd INVALID_SOCKET

g_hInstance         dd 0
g_hMainWnd          dd 0
g_hMenu             dd 0

g_aeCalled          db "AUTOIT_SCP      ", 0

szIniServer        db "Server",0
szIniLists         db "Lists",0
szIniAET           db "AETitle",0
szIniTelnetPort    db "TelnetPort",0
szIniHttpPort      db "HttpPort",0
szIniTelnetTimeout db "TelnetTimeout",0
szIniDebugLog      db "DebugLog",0

szIniModalities    db "Modalities",0
szIniAETitles      db "AETitles",0
szIniRefPhys       db "ReferringPhysicians",0
szIniProcedures    db "Procedures",0
szIniProcCodes     db "ProcedureCodes",0

szDefAET           db "AUTOIT_SCP",0
szDefMods          db "CR;DX;CT;MR;US;OT",0
szDefAETs          db "AET1;AET2",0
szDefRefPhys       db "Dr. Smith;Dr. Brown",0
szDefProcedures    db "Chest X-Ray;CT Head",0
szDefProcCodes     db "PCODE01;PCODE02",0
szDefDebug         db "1",0
szDefTelnetPort    db "23",0
szDefHttpPort      db "80",0
szDefTelnetTimeout db "10",0

szWndClass         db "WorklistTrayClass",0
szTrayTip          db "RIS Multi-Protocol Server",0
szMenuStart        db "Start Server",0
szMenuStop         db "Stop Server",0
szMenuShow         db "Show Console",0
szMenuExit         db "Exit",0

szStartup          db "RIS Server starting...",13,10,0
szConfigFmt        db "Config: AET=%s TelnetPort=%u HttpPort=%u Timeout=%u DebugLog=%u",13,10,0
szListenFmt        db "Telnet listening on port %u",13,10,0
szListenHttpFmt    db "HTTP listening on port %u",13,10,0
szListenFailFmt    db "ERROR: Failed to listen on port %u",13,10,0
szClientConnFmt    db "Telnet Client connected on socket %u",13,10,0
szHttpConnFmt      db "HTTP Client connected on socket %u",13,10,0
szClientDiscFmt    db "Client disconnected on socket %u",13,10,0
szTimeoutFmt       db "Client on socket %u timed out",13,10,0
szPendingFmt       db "PENDING Accession %s",13,10,0
szInsertedFmt      db "INSERTED Accession %s",13,10,0
szUpdatedFmt       db "UPDATED Accession %s",13,10,0
szConnected        db "Connected to RIS Telnet Server. Waiting for data...",13,10,0
szPendingResp      db "PENDING",13,10,0
szInsertedResp     db "INSERTED",13,10,0
szUpdatedResp      db "UPDATED",13,10,0
szInvalidResp      db "INVALID LINE",13,10,0
szServerStarted    db "Server STARTED",13,10,0
szServerStopped    db "Server STOPPED",13,10,0

szCSVFile          db "patients.csv",0
szProcCodesFile    db "procedurecodes.csv",0
szRoomsFile        db "rooms.csv",0
szCSVTempFile      db "patients.tmp",0
szModeRead         db "r",0
szModeWrite        db "w",0
szFmtLine          db "%s",13,10,0

szCSVHeader        db "PatientName,PatientID,BirthDate,Sex,ReqPhys,ReqSvc,ProcDesc,Reason,Accession,ProcID,Priority,StationAE,StartDate,StartTime,PerfPhys,SPSDesc,SPSID,StationName,Location,Status,Modality,RefPhys,StudyDesc",0
szFakePatient      db "Jane Smith,P87654321,19920522,F,Dr. Brown,MRI,Brain MRI,Headache,ACC00002,PROC2,2,RIS,20260709,11:45,Dr. White,SPS2,SPSID2,Station2,Room2,1,MR,Dr. Black,Study 2",0

; HTTP Responses
szHttp200OK        db "HTTP/1.1 200 OK",13,10,"Content-Type: text/html",13,10,"Connection: close",13,10,13,10,0

; HTML split to prevent MASM A2041 errors, Includes Table Sorting, Date Formatting, Auto-Date, & Accession Incrementor JS
szHtmlStart        db "<html><head><title>RIS Worklist Manager</title><style>"
                   db "table{border-collapse:collapse;width:100%;font-size:14px;box-shadow:0 0 20px rgba(0,0,0,0.15);}"
                   db "th,td{border:1px solid #dddddd;padding:12px 15px;}th{background:#009879;color:#ffffff;text-align:left;}"
                   db "tr{border-bottom:1px solid #dddddd;}tr:nth-child(even){background-color:#f3f3f3;}"
                   db "input[type='text'],input[type='date'],input[type='time']{margin-bottom:10px;padding:5px;width:180px;margin-right:10px;}"
                   db "select{margin-bottom:10px;padding:5px;margin-right:10px;}</style>"
                   db "<script>var fnames=['PatientName','PatientID','BirthDate','Sex','ReqPhys','ReqSvc','ProcDesc',"
                   db "'Reason','Accession','ProcID','Priority','StationAE','StartDate','StartTime','PerfPhys','SPSDesc','SPSID','StationName',"
                   db "'Location','Status','Modality','RefPhys','StudyDesc'];"
                   db "function f(r){var c=r.cells;for(var i=0;i<c.length&&i<fnames.length;i++){var e=document.getElementsByName(fnames[i])[0];if(e)e.value=(c[i].innerText||c[i].textContent).trim();}}"
                   db "function sendMan(){var c=[];for(var i=0;i<fnames.length;i++){var v=document.getElementsByName(fnames[i])[0].value||'';"
                   db "if(v.indexOf(',')>-1||v.indexOf('", 34, "')>-1)v='", 34, "'+v.replace(new RegExp('", 34, "','g'),'", 34, 34, "')+'", 34, "';c.push(v);}"
                   db "fetch('/manual',{method:'POST',body:c.join(',')}).then(function(){window.location.href='/';});return false;}"
                   db "function sortTable(n){var t,r,sw,i,x,y,sd,d,sc=0;t=document.getElementById('pTable');sw=true;d='asc';"
                   db "while(sw){sw=false;r=t.rows;for(i=1;i<(r.length-1);i++){sd=false;x=r[i].getElementsByTagName('TD')[n];"
                   db "y=r[i+1].getElementsByTagName('TD')[n];if(d=='asc'){if(x.innerHTML.toLowerCase()>y.innerHTML.toLowerCase()){sd=true;break;}}"
                   db "else{if(x.innerHTML.toLowerCase()<y.innerHTML.toLowerCase()){sd=true;break;}}}if(sd){r[i].parentNode.insertBefore(r[i+1],r[i]);sw=true;sc++;}"
                   db "else{if(sc==0&&d=='asc'){d='desc';sw=true;}}}}"
                   db "function formatDates(){var t=document.getElementById('pTable');if(!t)return;for(var i=1;i<t.rows.length;i++){"
                   db "var c=t.rows[i].cells;if(c.length>12){var b=c[2].innerText;if(b.length===8&&b.indexOf('-')===-1)c[2].innerText=b.substr(0,4)+'-'+b.substr(4,2)+'-'+b.substr(6,2);"
                   db "var s=c[12].innerText;if(s.length===8&&s.indexOf('-')===-1)c[12].innerText=s.substr(0,4)+'-'+s.substr(4,2)+'-'+s.substr(6,2);}}}"
                   db "function nxtA(){var t=document.getElementById('pTable'),m=0,p='ACC',c=5;if(t){for(var i=1;i<t.rows.length;i++){"
                   db "var v=t.rows[i].cells[8].innerText||'',mt=v.match(/([^0-9]*)([0-9]+)/);if(mt){p=mt[1];var val=parseInt(mt[2],10);"
                   db "if(val>m){m=val;c=mt[2].length;}}}}m++;document.getElementsByName('Accession')[0].value=p+String(m).padStart(c,'0');}"
                   db "window.onload=function(){var d=new Date(),y=d.getFullYear(),mo=('0'+(d.getMonth()+1)).slice(-2),da=('0'+d.getDate()).slice(-2);"
                   db "var sd=document.getElementsByName('StartDate')[0];if(sd&&!sd.value)sd.value=y+'-'+mo+'-'+da;};"
                   db "</script></head><body style='font-family: Arial, sans-serif; padding: 20px; color: #333;'>",0

szHtmlHeader       db "<h1>RIS Worklist Manager</h1>",0

szHtmlForms        db "<h3>Upload patients.csv</h3>"
                   db "<form action='/upload' method='POST' enctype='multipart/form-data'>"
                   db "<input type='file' name='csvfile'> <input type='submit' value='Upload CSV'></form>"
                   db "<form action='/delete' method='POST'><input type='submit' value='Delete Data' style='color:red;'></form>"
                   db "<hr><h3>Manual Input</h3>"
                   db "<form id='manForm' onsubmit='return sendMan()'>"
                   db "<input type='text' name='PatientName' placeholder='Patient Name'> "
                   db "<input type='text' name='PatientID' placeholder='Patient ID'> "
                   db "Birth Date: <input type='date' name='BirthDate'> "
                   db "<select name='Sex'><option value=''>Sex...</option><option value='M'>M</option><option value='F'>F</option><option value='O'>O</option></select><br>"
                   db "<input type='text' name='ReqPhys' placeholder='Req Phys'> "
                   db "<input type='text' name='ReqSvc' placeholder='Req Svc'> "
                   db "<input type='text' name='ProcDesc' placeholder='Proc Desc'> "
                   db "<input type='text' name='Reason' placeholder='Reason'><br>"
                   db "<input type='text' name='Accession' placeholder='Accession Number'> "
                   db "<input type='text' name='ProcID' placeholder='Proc ID'> "
                   db "<select name='Priority'><option value='0' selected>LOW</option><option value='1'>MEDIUM</option><option value='2'>ROUTINE</option><option value='3'>HIGH</option></select> "
                   db "<input type='text' name='StationAE' placeholder='Station AE'><br>"
                   db "Start Date: <input type='date' name='StartDate'> "
                   db "Start Time: <input type='time' name='StartTime' value='16:00'> "
                   db "<input type='text' name='PerfPhys' placeholder='Perf Phys'> "
                   db "<input type='text' name='SPSDesc' placeholder='SPS Desc'><br>"
                   db "<input type='text' name='SPSID' placeholder='SPS ID'> "
                   db "<input type='text' name='StationName' placeholder='Station Name'> "
                   db "<input type='text' name='Location' placeholder='Location'> "
                   db "<select name='Status'><option value='0'>NONE</option><option value='1' selected>SCHEDULED</option><option value='2'>ARRIVED</option>"
                   db "<option value='3'>READY</option><option value='4'>STARTED</option><option value='5'>DEPARTED</option></select><br>"
                   db "<input type='text' name='Modality' placeholder='Modality'> "
                   db "<input type='text' name='RefPhys' placeholder='Ref Phys'> "
                   db "<input type='text' name='StudyDesc' placeholder='Study Desc'><br>"
                   db "<input type='submit' id='btnAddUpdate' value='Add/Edit Patient'> "
                   db "<button type='button' onclick='nxtA()'>Next Accession</button></form><hr><h3>Current Database</h3>",0
                   
szHtmlEnd          db "</body></html>",0

.data?
wsaData            WSADATA <>
nid                NOTIFYICONDATA <>
szIniFile          db 260 dup(?)   ; Dynamic INI file path buffer
g_clients          dd MAX_CLIENTS dup(?)
g_clientIDs        dd MAX_CLIENTS dup(?)
g_clientType       dd MAX_CLIENTS dup(?)  ; 0 = Telnet, 1 = HTTP
g_clientLastTick   dd MAX_CLIENTS dup(?)
g_clientTimeout    dd MAX_CLIENTS dup(?)
g_clientBufLen     dd MAX_CLIENTS dup(?)
g_clientBuffers    db MAX_CLIENTS * CLIENT_BUF_SIZE dup(?)
g_pendingFlag      dd MAX_CLIENTS dup(?)
g_pendingSince     dd MAX_CLIENTS dup(?)
g_pendingEntries   db MAX_CLIENTS * ENTRY_SIZE dup(?)
g_recvBuf          db RECV_SIZE dup(?)
g_fileLine         db LINE_SIZE dup(?)
g_lineOut          db LINE_SIZE dup(?)
g_tmpEntry         db ENTRY_SIZE dup(?)
g_dispParam        db 512 dup(?)
g_iniBuf           db 256 dup(?)

.code

; Gets the .exe path and replaces .exe with .ini
ResolveIniPath PROC
    LOCAL idx:DWORD
    invoke GetModuleFileName, NULL, OFFSET szIniFile, 260
    invoke crt_strlen, OFFSET szIniFile
    mov idx, eax
    mov edx, OFFSET szIniFile
    add edx, idx
    sub edx, 4  ; step back to the dot before 'exe'
    mov eax, dword ptr [edx]
    cmp al, '.'
    jne RIP_Done
    mov byte ptr [edx+1], 'i'
    mov byte ptr [edx+2], 'n'
    mov byte ptr [edx+3], 'i'
RIP_Done:
    ret
ResolveIniPath ENDP

SetAETitle PROC pSrc:DWORD
    LOCAL nLen:DWORD
    push edi
    mov edi, OFFSET g_aeCalled
    mov al, ' '
    mov ecx, 16
    rep stosb
    mov BYTE PTR [edi], 0
    pop edi
    invoke crt_strlen, pSrc
    mov nLen, eax
    cmp nLen, 16
    jbe SAT_OK
    mov nLen, 16
SAT_OK:
    invoke crt_memcpy, OFFSET g_aeCalled, pSrc, nLen
    ret
SetAETitle ENDP

LoadConfig PROC
    invoke GetPrivateProfileString, OFFSET szIniServer, OFFSET szIniAET, OFFSET szDefAET, OFFSET g_iniBuf, 256, OFFSET szIniFile
    invoke SetAETitle, OFFSET g_iniBuf
    invoke GetPrivateProfileInt, OFFSET szIniServer, OFFSET szIniTelnetPort, 23, OFFSET szIniFile
    mov g_TelnetPort, eax
    invoke GetPrivateProfileInt, OFFSET szIniServer, OFFSET szIniHttpPort, 80, OFFSET szIniFile
    mov g_HttpPort, eax
    invoke GetPrivateProfileInt, OFFSET szIniServer, OFFSET szIniTelnetTimeout, 10, OFFSET szIniFile
    mov g_TelnetTimeout, eax
    invoke GetPrivateProfileInt, OFFSET szIniServer, OFFSET szIniDebugLog, 1, OFFSET szIniFile
    mov g_DebugLog, eax
    invoke WritePrivateProfileString, OFFSET szIniServer, OFFSET szIniAET, OFFSET g_aeCalled, OFFSET szIniFile
    invoke WritePrivateProfileString, OFFSET szIniServer, OFFSET szIniTelnetPort, OFFSET szDefTelnetPort, OFFSET szIniFile
    invoke WritePrivateProfileString, OFFSET szIniServer, OFFSET szIniHttpPort, OFFSET szDefHttpPort, OFFSET szIniFile
    invoke WritePrivateProfileString, OFFSET szIniServer, OFFSET szIniTelnetTimeout, OFFSET szDefTelnetTimeout, OFFSET szIniFile
    invoke WritePrivateProfileString, OFFSET szIniServer, OFFSET szIniDebugLog, OFFSET szDefDebug, OFFSET szIniFile
    ret
LoadConfig ENDP

; --- Boilerplate Array Accessors ---
GetClientSockPtr PROC idx:DWORD
    mov eax, idx
    shl eax, 2
    add eax, OFFSET g_clients
    ret
GetClientSockPtr ENDP

GetClientTypePtr PROC idx:DWORD
    mov eax, idx
    shl eax, 2
    add eax, OFFSET g_clientType
    ret
GetClientTypePtr ENDP

GetClientIDPtr PROC idx:DWORD
    mov eax, idx
    shl eax, 2
    add eax, OFFSET g_clientIDs
    ret
GetClientIDPtr ENDP

GetClientLastPtr PROC idx:DWORD
    mov eax, idx
    shl eax, 2
    add eax, OFFSET g_clientLastTick
    ret
GetClientLastPtr ENDP

GetClientTimeoutPtr PROC idx:DWORD
    mov eax, idx
    shl eax, 2
    add eax, OFFSET g_clientTimeout
    ret
GetClientTimeoutPtr ENDP

GetClientBufLenPtr PROC idx:DWORD
    mov eax, idx
    shl eax, 2
    add eax, OFFSET g_clientBufLen
    ret
GetClientBufLenPtr ENDP

GetClientBufferPtr PROC idx:DWORD
    mov eax, idx
    mov ecx, CLIENT_BUF_SIZE
    mul ecx
    add eax, OFFSET g_clientBuffers
    ret
GetClientBufferPtr ENDP

GetPendingFlagPtr PROC idx:DWORD
    mov eax, idx
    shl eax, 2
    add eax, OFFSET g_pendingFlag
    ret
GetPendingFlagPtr ENDP

GetPendingSincePtr PROC idx:DWORD
    mov eax, idx
    shl eax, 2
    add eax, OFFSET g_pendingSince
    ret
GetPendingSincePtr ENDP

GetPendingEntryPtr PROC idx:DWORD
    mov eax, idx
    mov ecx, ENTRY_SIZE
    mul ecx
    add eax, OFFSET g_pendingEntries
    ret
GetPendingEntryPtr ENDP

GetFieldPtr PROC pEntry:DWORD, fieldIdx:DWORD
    mov eax, fieldIdx
    mov ecx, FIELD_SIZE
    mul ecx
    add eax, pEntry
    ret
GetFieldPtr ENDP
; -----------------------------------

SendText PROC sock:DWORD, pText:DWORD
    LOCAL nLen:DWORD
    invoke crt_strlen, pText
    mov nLen, eax
    cmp eax, 0
    je ST_Done
    invoke send, sock, pText, nLen, 0
ST_Done:
    ret
SendText ENDP

; String Manipulation Helpers
IsWhite PROC ival:DWORD
    mov eax, ival
    cmp al, 20h
    je IW_Yes
    cmp al, 09h
    je IW_Yes
    cmp al, 0Dh
    je IW_Yes
    cmp al, 0Ah
    je IW_Yes
    xor eax, eax
    ret
IW_Yes:
    mov eax, 1
    ret
IsWhite ENDP

TrimInPlace PROC uses esi edi pStr:DWORD
    LOCAL nLen:DWORD
    LOCAL pEnd:DWORD
    mov esi, pStr
TIP_Lead:
    movzx eax, BYTE PTR [esi]
    cmp eax, 0
    je TIP_Done
    invoke IsWhite, eax
    cmp eax, 1
    jne TIP_ShiftStart
    inc esi
    jmp TIP_Lead
TIP_ShiftStart:
    mov edi, pStr
    cmp esi, edi
    je TIP_Trail
TIP_Shift:
    mov al, BYTE PTR [esi]
    mov BYTE PTR [edi], al
    inc esi
    inc edi
    cmp al, 0
    jne TIP_Shift
TIP_Trail:
    invoke crt_strlen, pStr
    mov nLen, eax
    cmp eax, 0
    je TIP_Done
    mov eax, pStr
    add eax, nLen
    dec eax
    mov pEnd, eax
TIP_TrailLoop:
    mov eax, pEnd
    cmp eax, pStr
    jb TIP_Done
    movzx ecx, BYTE PTR [eax]
    invoke IsWhite, ecx
    cmp eax, 1
    jne TIP_Done
    mov eax, pEnd
    mov BYTE PTR [eax], 0
    dec pEnd
    jmp TIP_TrailLoop
TIP_Done:
    ret
TrimInPlace ENDP

StripDashes PROC uses esi edi pStr:DWORD
    mov esi, pStr
    mov edi, pStr
SD_Loop:
    mov al, byte ptr [esi]
    cmp al, 0
    je SD_Done
    cmp al, '-'
    je SD_Skip
    mov byte ptr [edi], al
    inc edi
SD_Skip:
    inc esi
    jmp SD_Loop
SD_Done:
    mov byte ptr [edi], 0
    ret
StripDashes ENDP

UpperChar PROC ival:DWORD
    mov eax, ival
    cmp al, 'a'
    jb UC_Done
    cmp al, 'z'
    ja UC_Done
    sub al, 20h
UC_Done:
    ret
UpperChar ENDP

StartsWithDISP PROC pLine:DWORD
    mov edx, pLine
    movzx eax, BYTE PTR [edx+0]
    invoke UpperChar, eax
    cmp al, 'D'
    jne SWD_No
    movzx eax, BYTE PTR [edx+1]
    invoke UpperChar, eax
    cmp al, 'I'
    jne SWD_No
    movzx eax, BYTE PTR [edx+2]
    invoke UpperChar, eax
    cmp al, 'S'
    jne SWD_No
    movzx eax, BYTE PTR [edx+3]
    invoke UpperChar, eax
    cmp al, 'P'
    jne SWD_No
    mov eax, 1
    ret
SWD_No:
    xor eax, eax
    ret
StartsWithDISP ENDP

StrIEquals PROC uses esi edi pA:DWORD, pB:DWORD
    LOCAL ca:DWORD
    LOCAL cb:DWORD
    mov esi, pA
    mov edi, pB
SIE_Loop:
    movzx eax, BYTE PTR [esi]
    invoke UpperChar, eax
    mov ca, eax
    movzx eax, BYTE PTR [edi]
    invoke UpperChar, eax
    mov cb, eax
    mov eax, ca
    cmp al, BYTE PTR cb
    jne SIE_No
    cmp al, 0
    je SIE_Yes
    inc esi
    inc edi
    jmp SIE_Loop
SIE_Yes:
    mov eax, 1
    ret
SIE_No:
    xor eax, eax
    ret
StrIEquals ENDP

IsEightDigits PROC uses esi pStr:DWORD
    LOCAL idx:DWORD
    mov esi, pStr
    mov idx, 0
    movzx eax, BYTE PTR [esi]
    cmp eax, 0
    je IED_EmptyAllowed
IED_Loop:
    cmp idx, 8
    jge IED_EndCheck
    mov al, BYTE PTR [esi]
    cmp al, '0'
    jb IED_No
    cmp al, '9'
    ja IED_No
    inc esi
    inc idx
    jmp IED_Loop
IED_EndCheck:
    cmp BYTE PTR [esi], 0
    jne IED_No
    mov eax, 1
    ret
IED_EmptyAllowed:
    xor eax, eax
    ret
IED_No:
    xor eax, eax
    ret
IsEightDigits ENDP

ParseCSVLineToEntry PROC uses esi edi pLine:DWORD, pEntry:DWORD
    LOCAL fieldIdx:DWORD
    LOCAL charIdx:DWORD
    LOCAL quoteState:DWORD
    LOCAL pDest:DWORD
    LOCAL charCode:DWORD
    invoke RtlZeroMemory, pEntry, ENTRY_SIZE
    mov esi, pLine
    mov fieldIdx, 0
    mov charIdx, 0
    mov quoteState, 0
    mov eax, pEntry
    mov pDest, eax
PCL_Loop:
    movzx eax, BYTE PTR [esi]
    mov charCode, eax
    cmp eax, 0
    je PCL_End
    cmp eax, 0Dh
    je PCL_End
    cmp eax, 0Ah
    je PCL_End
    cmp al, '~'
    jne PCL_NotTilde
    mov charCode, '"'
PCL_NotTilde:
    mov eax, charCode
    cmp al, '"'
    jne PCL_CheckComma
    cmp quoteState, 0
    je PCL_QOn
    mov quoteState, 0
    inc esi
    jmp PCL_Loop
PCL_QOn:
    mov quoteState, 1
    inc esi
    jmp PCL_Loop
PCL_CheckComma:
    mov eax, charCode
    cmp al, ','
    jne PCL_Store
    cmp quoteState, 0
    jne PCL_Store
    mov edi, pDest
    add edi, charIdx
    mov BYTE PTR [edi], 0
    mov eax, fieldIdx
    inc eax
    mov fieldIdx, eax
    cmp fieldIdx, FIELD_COUNT
    jge PCL_End
    mov eax, pDest
    add eax, FIELD_SIZE
    mov pDest, eax
    mov charIdx, 0
    inc esi
    jmp PCL_Loop
PCL_Store:
    cmp charIdx, FIELD_SIZE - 1
    jge PCL_NextChar
    mov edi, pDest
    add edi, charIdx
    mov eax, charCode
    mov BYTE PTR [edi], al
    mov eax, charIdx
    inc eax
    mov charIdx, eax
PCL_NextChar:
    inc esi
    jmp PCL_Loop
PCL_End:
    mov edi, pDest
    add edi, charIdx
    mov BYTE PTR [edi], 0
    mov fieldIdx, 0
PCL_Trim:
    cmp fieldIdx, FIELD_COUNT
    jge PCL_Done
    invoke GetFieldPtr, pEntry, fieldIdx
    invoke TrimInPlace, eax
    mov eax, fieldIdx
    inc eax
    mov fieldIdx, eax
    jmp PCL_Trim
PCL_Done:
    mov eax, 1
    ret
ParseCSVLineToEntry ENDP

ValidateEntry PROC pEntry:DWORD
    ; Strictly enforce ONLY Accession (Field 8) to prevent silent rejections
    invoke GetFieldPtr, pEntry, 8
    cmp BYTE PTR [eax], 0
    je VE_No
    mov eax, 1
    ret
VE_No:
    xor eax, eax
    ret
ValidateEntry ENDP

NeedsQuote PROC uses esi pField:DWORD
    mov esi, pField
NQ_Loop:
    mov al, BYTE PTR [esi]
    cmp al, 0
    je NQ_No
    cmp al, ','
    je NQ_Yes
    cmp al, '"'
    je NQ_Yes
    inc esi
    jmp NQ_Loop
NQ_Yes:
    mov eax, 1
    ret
NQ_No:
    xor eax, eax
    ret
NeedsQuote ENDP

AppendChar PROC pOutPtr:DWORD, charVal:DWORD
    mov edx, pOutPtr
    mov eax, DWORD PTR [edx]
    mov ecx, charVal
    mov BYTE PTR [eax], cl
    inc eax
    mov DWORD PTR [edx], eax
    ret
AppendChar ENDP

BuildCSVLineFromEntry PROC uses esi pEntry:DWORD, pOut:DWORD
    LOCAL fieldIdx:DWORD
    LOCAL pField:DWORD
    LOCAL pCur:DWORD
    LOCAL quoteNeeded:DWORD
    mov eax, pOut
    mov pCur, eax
    mov fieldIdx, 0
BCL_FieldLoop:
    cmp fieldIdx, FIELD_COUNT
    jge BCL_End
    cmp fieldIdx, 0
    je BCL_NoComma
    mov eax, ','
    invoke AppendChar, ADDR pCur, eax
BCL_NoComma:
    invoke GetFieldPtr, pEntry, fieldIdx
    mov pField, eax
    invoke NeedsQuote, pField
    mov quoteNeeded, eax
    cmp quoteNeeded, 1
    jne BCL_Copy
    mov eax, '"'
    invoke AppendChar, ADDR pCur, eax
BCL_Copy:
    mov esi, pField
BCL_CopyLoop:
    mov al, BYTE PTR [esi]
    cmp al, 0
    je BCL_FieldDone
    cmp quoteNeeded, 1
    jne BCL_Normal
    cmp al, '"'
    jne BCL_Normal
    mov eax, '"'
    invoke AppendChar, ADDR pCur, eax
    mov eax, '"'
    invoke AppendChar, ADDR pCur, eax
    inc esi
    jmp BCL_CopyLoop
BCL_Normal:
    movzx eax, BYTE PTR [esi]
    invoke AppendChar, ADDR pCur, eax
    inc esi
    jmp BCL_CopyLoop
BCL_FieldDone:
    cmp quoteNeeded, 1
    jne BCL_Next
    mov eax, '"'
    invoke AppendChar, ADDR pCur, eax
BCL_Next:
    mov eax, fieldIdx
    inc eax
    mov fieldIdx, eax
    jmp BCL_FieldLoop
BCL_End:
    mov edx, pCur
    mov BYTE PTR [edx], 0
    ret
BuildCSVLineFromEntry ENDP

CreateCSVIfMissing PROC
    LOCAL hFile:DWORD
    invoke crt_fopen, OFFSET szCSVFile, OFFSET szModeRead
    mov hFile, eax
    cmp eax, 0
    je CCIM_Create
    invoke crt_fclose, hFile
    ret
CCIM_Create:
    invoke crt_fopen, OFFSET szCSVFile, OFFSET szModeWrite
    mov hFile, eax
    cmp eax, 0
    je CCIM_Done
    invoke crt_fprintf, hFile, OFFSET szFmtLine, OFFSET szCSVHeader
    invoke crt_fprintf, hFile, OFFSET szFmtLine, OFFSET szFakePatient
    invoke crt_fclose, hFile
CCIM_Done:
    ret
CreateCSVIfMissing ENDP

AccessionEquals PROC uses esi edi pLine:DWORD, pAccession:DWORD
    LOCAL tempEntry[ENTRY_SIZE]:BYTE
    LOCAL pField:DWORD
    invoke ParseCSVLineToEntry, pLine, ADDR tempEntry
    invoke GetFieldPtr, ADDR tempEntry, 8
    mov pField, eax
    invoke crt_strcmp, eax, pAccession
    cmp eax, 0
    je AE_Yes
    xor eax, eax
    ret
AE_Yes:
    mov eax, 1
    ret
AccessionEquals ENDP

UpdateCsvByPatient PROC pEntry:DWORD, pLineOut:DWORD
    LOCAL hIn:DWORD
    LOCAL hOut:DWORD
    LOCAL found:DWORD
    LOCAL pAccession:DWORD
    mov found, 0
    invoke CreateCSVIfMissing
    invoke GetFieldPtr, pEntry, 8
    mov pAccession, eax
    invoke crt_fopen, OFFSET szCSVFile, OFFSET szModeRead
    mov hIn, eax
    invoke crt_fopen, OFFSET szCSVTempFile, OFFSET szModeWrite
    mov hOut, eax
    cmp hOut, 0
    je UCP_Fail
    cmp hIn, 0
    je UCP_WriteHeader
    invoke crt_fgets, OFFSET g_fileLine, LINE_SIZE, hIn
    cmp eax, 0
    je UCP_WriteHeader
    invoke crt_fputs, OFFSET g_fileLine, hOut
    jmp UCP_ReadLoop
UCP_WriteHeader:
    invoke crt_fprintf, hOut, OFFSET szFmtLine, OFFSET szCSVHeader
    jmp UCP_AfterLoop
UCP_ReadLoop:
    invoke crt_fgets, OFFSET g_fileLine, LINE_SIZE, hIn
    cmp eax, 0
    je UCP_AfterLoop
    cmp found, 1
    je UCP_CopyOriginal
    invoke AccessionEquals, OFFSET g_fileLine, pAccession
    cmp eax, 1
    jne UCP_CopyOriginal
    invoke crt_fprintf, hOut, OFFSET szFmtLine, pLineOut
    mov found, 1
    jmp UCP_ReadLoop
UCP_CopyOriginal:
    invoke crt_fputs, OFFSET g_fileLine, hOut
    jmp UCP_ReadLoop
UCP_AfterLoop:
    cmp found, 1
    je UCP_Close
    invoke crt_fprintf, hOut, OFFSET szFmtLine, pLineOut
UCP_Close:
    cmp hIn, 0
    je UCP_NoCloseIn
    invoke crt_fclose, hIn
UCP_NoCloseIn:
    invoke crt_fclose, hOut
    invoke crt_remove, OFFSET szCSVFile
    invoke crt_rename, OFFSET szCSVTempFile, OFFSET szCSVFile
    mov eax, found
    ret
UCP_Fail:
    cmp hIn, 0
    je UCP_Return
    invoke crt_fclose, hIn
UCP_Return:
    xor eax, eax
    ret
UpdateCsvByPatient ENDP

ExtractDispParam PROC uses esi edi pLine:DWORD, pOut:DWORD
    mov esi, pLine
    add esi, 4
EDP_Skip:
    mov al, BYTE PTR [esi]
    cmp al, ' '
    jne EDP_Copy
    inc esi
    jmp EDP_Skip
EDP_Copy:
    mov edi, pOut
EDP_Loop:
    mov al, BYTE PTR [esi]
    cmp al, 0
    je EDP_Zero
    cmp al, 0Dh
    je EDP_Zero
    cmp al, 0Ah
    je EDP_Zero
    cmp al, ','
    je EDP_Zero
    mov BYTE PTR [edi], al
    inc esi
    inc edi
    jmp EDP_Loop
EDP_Zero:
    mov BYTE PTR [edi], 0
    invoke TrimInPlace, pOut
    ret
ExtractDispParam ENDP

LineMatchesDisp PROC pLine:DWORD, pParam:DWORD
    LOCAL tempEntry[ENTRY_SIZE]:BYTE
    LOCAL pField:DWORD
    LOCAL pTo:DWORD
    mov edx, pParam
    cmp BYTE PTR [edx], 0
    je LMD_Yes
    invoke ParseCSVLineToEntry, pLine, ADDR tempEntry
    invoke IsEightDigits, pParam
    cmp eax, 1
    jne LMD_Range
    invoke GetFieldPtr, ADDR tempEntry, 1
    invoke crt_strcmp, eax, pParam
    cmp eax, 0
    je LMD_Yes
    jmp LMD_No
LMD_Range:
    invoke IsDateRangeParam, pParam
    cmp eax, 1
    jne LMD_Modality
    invoke GetFieldPtr, ADDR tempEntry, 12
    mov pField, eax
    invoke crt_strncmp, pField, pParam, 8
    cmp eax, 0
    jl LMD_No
    mov eax, pParam
    add eax, 9
    mov pTo, eax
    invoke crt_strncmp, pField, pTo, 8
    cmp eax, 0
    jg LMD_No
    jmp LMD_Yes
LMD_Modality:
    invoke GetFieldPtr, ADDR tempEntry, 20
    invoke StrIEquals, eax, pParam
    cmp eax, 1
    je LMD_Yes
LMD_No:
    xor eax, eax
    ret
LMD_Yes:
    mov eax, 1
    ret
LineMatchesDisp ENDP

IsDateRangeParam PROC pParam:DWORD
    mov edx, pParam
    cmp BYTE PTR [edx+8], ' '
    jne IDR_No
    cmp BYTE PTR [edx+17], 0
    jne IDR_No
    mov eax, 1
    ret
IDR_No:
    xor eax, eax
    ret
IsDateRangeParam ENDP

ProcessDispCommand PROC clientIdx:DWORD, pLine:DWORD
    LOCAL hFile:DWORD
    LOCAL sock:DWORD
    invoke ExtractDispParam, pLine, OFFSET g_dispParam
    invoke GetClientSockPtr, clientIdx
    mov eax, DWORD PTR [eax]
    mov sock, eax
    invoke crt_fopen, OFFSET szCSVFile, OFFSET szModeRead
    mov hFile, eax
    cmp eax, 0
    je PDC_Done
    invoke crt_fgets, OFFSET g_fileLine, LINE_SIZE, hFile
PDC_Loop:
    invoke crt_fgets, OFFSET g_fileLine, LINE_SIZE, hFile
    cmp eax, 0
    je PDC_Close
    invoke LineMatchesDisp, OFFSET g_fileLine, OFFSET g_dispParam
    cmp eax, 1
    jne PDC_Loop
    invoke SendText, sock, OFFSET g_fileLine
    jmp PDC_Loop
PDC_Close:
    invoke crt_fclose, hFile
PDC_Done:
    ret
ProcessDispCommand ENDP

CommitPendingEntry PROC clientIdx:DWORD
    LOCAL pEntry:DWORD
    LOCAL pAccession:DWORD
    LOCAL sock:DWORD
    LOCAL found:DWORD
    invoke GetPendingEntryPtr, clientIdx
    mov pEntry, eax
    invoke BuildCSVLineFromEntry, pEntry, OFFSET g_lineOut
    invoke UpdateCsvByPatient, pEntry, OFFSET g_lineOut
    mov found, eax
    invoke GetFieldPtr, pEntry, 8
    mov pAccession, eax
    invoke GetClientSockPtr, clientIdx
    mov eax, DWORD PTR [eax]
    mov sock, eax
    cmp found, 1
    je CPE_Updated
    invoke SendText, sock, OFFSET szInsertedResp
    invoke crt_printf, OFFSET szInsertedFmt, pAccession
    jmp CPE_Clear
CPE_Updated:
    invoke SendText, sock, OFFSET szUpdatedResp
    invoke crt_printf, OFFSET szUpdatedFmt, pAccession
CPE_Clear:
    invoke GetPendingFlagPtr, clientIdx
    mov DWORD PTR [eax], 0
    invoke GetPendingSincePtr, clientIdx
    mov DWORD PTR [eax], 0
    ret
CommitPendingEntry ENDP

ProcessClientLine PROC clientIdx:DWORD, pLine:DWORD
    LOCAL entry[ENTRY_SIZE]:BYTE
    LOCAL pPending:DWORD
    LOCAL pAccession:DWORD
    LOCAL sock:DWORD
    invoke TrimInPlace, pLine
    mov edx, pLine
    cmp BYTE PTR [edx], 0
    je PCL2_Done
    invoke StartsWithDISP, pLine
    cmp eax, 1
    jne PCL2_CSV
    invoke ProcessDispCommand, clientIdx, pLine
    jmp PCL2_Done
PCL2_CSV:
    invoke ParseCSVLineToEntry, pLine, ADDR entry
    invoke ValidateEntry, ADDR entry
    cmp eax, 1
    je PCL2_Valid
    invoke GetClientSockPtr, clientIdx
    mov eax, DWORD PTR [eax]
    invoke SendText, eax, OFFSET szInvalidResp
    jmp PCL2_Done
PCL2_Valid:
    invoke GetPendingEntryPtr, clientIdx
    mov pPending, eax
    invoke crt_memcpy, pPending, ADDR entry, ENTRY_SIZE
    invoke GetPendingFlagPtr, clientIdx
    mov DWORD PTR [eax], 1
    invoke GetTickCount
    mov edx, eax
    invoke GetPendingSincePtr, clientIdx
    mov DWORD PTR [eax], edx
    invoke GetClientTimeoutPtr, clientIdx
    add DWORD PTR [eax], 1000
    invoke GetClientSockPtr, clientIdx
    mov eax, DWORD PTR [eax]
    mov sock, eax
    invoke SendText, sock, OFFSET szPendingResp
    invoke GetFieldPtr, pPending, 8
    mov pAccession, eax
    invoke crt_printf, OFFSET szPendingFmt, pAccession
PCL2_Done:
    ret
ProcessClientLine ENDP

; --- HTTP PARSING & RESPONSE SECTION ---
ProcessHttpClient PROC uses esi edi ebx clientIdx:DWORD, pBuf:DWORD
    LOCAL sock:DWORD
    LOCAL hFile:DWORD
    LOCAL entry[ENTRY_SIZE]:BYTE
    LOCAL fieldIdx:DWORD
    LOCAL isHeader:DWORD
    LOCAL pBody:DWORD
    LOCAL charIdxUpload:DWORD
    
    invoke GetClientSockPtr, clientIdx
    mov eax, DWORD PTR [eax]
    mov sock, eax

    invoke crt_strstr, pBuf, CStr(13,10,13,10)
    test eax, eax
    jz PHC_WaitMoreData

    mov esi, pBuf
    mov al, byte ptr [esi]
    cmp al, 'G'
    je ServeHttpUI
    cmp al, 'P'
    je VerifyPostPayloadComplete
    
    invoke RemoveClient, clientIdx
    ret

VerifyPostPayloadComplete:
    invoke crt_strstr, pBuf, CStr("POST /manual")
    test eax, eax
    jz HandleHttpPost
    
    invoke crt_strstr, pBuf, CStr(13,10,13,10)
    test eax, eax
    jz PHC_WaitMoreData
    add eax, 4
    cmp byte ptr [eax], 0
    je PHC_WaitMoreData
    
    jmp HandleHttpPost

PHC_WaitMoreData:
    ret

ServeHttpUI:
    invoke SendText, sock, OFFSET szHttp200OK
    invoke SendText, sock, OFFSET szHtmlStart
    invoke SendText, sock, OFFSET szHtmlHeader
    invoke SendText, sock, OFFSET szHtmlForms
    
    ; Add ProcedureCodes Integration script directly after forms if file exists
    invoke crt_fopen, OFFSET szProcCodesFile, OFFSET szModeRead
    mov hFile, eax
    cmp eax, 0
    je NoProcCodes
    invoke SendText, sock, CStr("<script>var pcsv=[")
ProcLoop:
    invoke crt_fgets, OFFSET g_fileLine, LINE_SIZE, hFile
    cmp eax, 0
    je ProcDone
    invoke TrimInPlace, OFFSET g_fileLine
    cmp byte ptr [g_fileLine], 0
    je ProcLoop
    invoke SendText, sock, CStr("`")
    invoke SendText, sock, OFFSET g_fileLine
    invoke SendText, sock, CStr("`,")
    jmp ProcLoop
ProcDone:
    invoke SendText, sock, CStr("];")
    invoke SendText, sock, CStr("var s=document.createElement('select');s.innerHTML='<option value=0>Select Code...</option>';")
    invoke SendText, sock, CStr("for(var i=1;i<pcsv.length;i++){var p=pcsv[i].split(',');if(p.length>=4)s.innerHTML+='<option value='+i+'>'+p[3]+'</option>';}")
    invoke SendText, sock, CStr("s.onchange=function(){if(this.value==0)return;var p=pcsv[this.value].split(',');")
    invoke SendText, sock, CStr("document.getElementsByName('ProcID')[0].value=p[0];document.getElementsByName('SPSID')[0].value=p[1];")
    invoke SendText, sock, CStr("document.getElementsByName('Modality')[0].value=p[2];document.getElementsByName('ProcDesc')[0].value=p[3];};")
    invoke SendText, sock, CStr("var inp=document.getElementsByName('ProcDesc')[0];inp.parentNode.insertBefore(s,inp.nextSibling);</script>")
    invoke crt_fclose, hFile
NoProcCodes:

    ; Add Rooms.csv Integration script if file exists
    invoke crt_fopen, OFFSET szRoomsFile, OFFSET szModeRead
    mov hFile, eax
    cmp eax, 0
    je NoRooms
    invoke SendText, sock, CStr("<script>var rcsv=[")
RoomsLoop:
    invoke crt_fgets, OFFSET g_fileLine, LINE_SIZE, hFile
    cmp eax, 0
    je RoomsDone
    invoke TrimInPlace, OFFSET g_fileLine
    cmp byte ptr [g_fileLine], 0
    je RoomsLoop
    invoke SendText, sock, CStr("`")
    invoke SendText, sock, OFFSET g_fileLine
    invoke SendText, sock, CStr("`,")
    jmp RoomsLoop
RoomsDone:
    invoke SendText, sock, CStr("];")
    invoke SendText, sock, CStr("var sr=document.createElement('select');sr.innerHTML='<option value=0>Select Room...</option>';")
    invoke SendText, sock, CStr("for(var i=1;i<rcsv.length;i++){var p=rcsv[i].split(',');if(p.length>=5)sr.innerHTML+='<option value='+i+'>'+p[1]+'</option>';}")
    invoke SendText, sock, CStr("sr.onchange=function(){if(this.value==0)return;var p=rcsv[this.value].split(',');")
    invoke SendText, sock, CStr("document.getElementsByName('Location')[0].value=p[0];document.getElementsByName('StationName')[0].value=p[4];")
    invoke SendText, sock, CStr("document.getElementsByName('Modality')[0].value=p[2];document.getElementsByName('StationAE')[0].value=p[3];};")
    invoke SendText, sock, CStr("var inpr=document.getElementsByName('StationName')[0];inpr.parentNode.insertBefore(sr,inpr.nextSibling);</script>")
    invoke crt_fclose, hFile
NoRooms:

    ; Output main patients table
    invoke crt_fopen, OFFSET szCSVFile, OFFSET szModeRead
    mov hFile, eax
    cmp hFile, 0
    je ServeEnd
    
    invoke SendText, sock, CStr("<table id='pTable'>")
    mov isHeader, 1
CSVLoop:
    invoke crt_fgets, OFFSET g_fileLine, LINE_SIZE, hFile
    cmp eax, 0
    je CloseHTML
    
    invoke ParseCSVLineToEntry, OFFSET g_fileLine, ADDR entry
    
    cmp isHeader, 1
    je SendHeaderTr
    invoke SendText, sock, CStr("<tr onclick='f(this)' style='cursor:pointer;'>")
    jmp AfterTr
SendHeaderTr:
    invoke SendText, sock, CStr("<tr>")
AfterTr:
    
    mov fieldIdx, 0
FieldLoop:
    cmp fieldIdx, FIELD_COUNT
    jge RowEnd
    
    cmp isHeader, 1
    jne NormalCell
    invoke SendText, sock, CStr("<th onclick='sortTable(this.cellIndex)' style='cursor:pointer;' title='Click to Sort'>")
    jmp WriteCell
NormalCell:
    invoke SendText, sock, CStr("<td>")
WriteCell:
    invoke GetFieldPtr, ADDR entry, fieldIdx
    invoke SendText, sock, eax
    
    cmp isHeader, 1
    jne EndNormalCell
    invoke SendText, sock, CStr("</th>")
    jmp EndCell
EndNormalCell:
    invoke SendText, sock, CStr("</td>")
EndCell:
    inc fieldIdx
    jmp FieldLoop
    
RowEnd:
    invoke SendText, sock, CStr("</tr>")
    mov isHeader, 0
    jmp CSVLoop

CloseHTML:
    invoke SendText, sock, CStr("</table><script>formatDates();</script>")
    invoke crt_fclose, hFile

ServeEnd:
    invoke SendText, sock, OFFSET szHtmlEnd
    invoke RemoveClient, clientIdx
    ret

HandleHttpPost:
    invoke crt_strstr, pBuf, CStr("POST /delete")
    test eax, eax
    jnz DoDelete
    
    invoke crt_strstr, pBuf, CStr("POST /manual")
    test eax, eax
    jnz DoManual
    
    invoke crt_strstr, pBuf, CStr("POST /upload")
    test eax, eax
    jnz DoUpload
    jmp PostRedirect

DoDelete:
    invoke crt_fopen, OFFSET szCSVFile, OFFSET szModeWrite
    mov hFile, eax
    cmp eax, 0
    je PostRedirect
    invoke crt_fprintf, hFile, OFFSET szFmtLine, OFFSET szCSVHeader
    invoke crt_fclose, hFile
    jmp PostRedirect

DoManual:
    invoke RtlZeroMemory, ADDR entry, ENTRY_SIZE
    invoke crt_strstr, pBuf, CStr(13,10,13,10)
    test eax, eax
    jz PostRedirect
    add eax, 4
    mov pBody, eax
    
    ; Parse the raw CSV string directly into the entry buffer
    invoke ParseCSVLineToEntry, pBody, ADDR entry
    
    ; Clean submitted UI format dates (YYYY-MM-DD) natively to YYYYMMDD before saving
    invoke GetFieldPtr, ADDR entry, 2
    invoke StripDashes, eax
    invoke GetFieldPtr, ADDR entry, 12
    invoke StripDashes, eax
    
    invoke ValidateEntry, ADDR entry
    cmp eax, 1
    jne PostRedirect
    invoke BuildCSVLineFromEntry, ADDR entry, OFFSET g_lineOut
    invoke UpdateCsvByPatient, ADDR entry, OFFSET g_lineOut
    jmp PostRedirect

DoUpload:
    invoke crt_strstr, pBuf, OFFSET szCSVHeader
    test eax, eax
    jz PostRedirect
    mov esi, eax
SkipHeaderLoop:
    mov al, byte ptr [esi]
    cmp al, 0
    je PostRedirect
    cmp al, 10
    je HeaderLineEnded
    inc esi
    jmp SkipHeaderLoop
HeaderLineEnded:
    inc esi
UploadLineLoop:
    mov al, byte ptr [esi]
    cmp al, 0
    je PostRedirect
    cmp al, '-' 
    je PostRedirect
    cmp al, 13
    je SkipEmptyChar
    cmp al, 10
    je SkipEmptyChar
    jmp CopyUploadLine
SkipEmptyChar:
    inc esi
    jmp UploadLineLoop
CopyUploadLine:
    lea edi, g_fileLine
    mov charIdxUpload, 0
CopyULLoop:
    mov al, byte ptr [esi]
    cmp al, 0
    je ULLineDone
    cmp al, 13
    je ULLineDone
    cmp al, 10
    je ULLineDone
    cmp charIdxUpload, LINE_SIZE - 1
    jge ULLineDone
    mov byte ptr [edi], al
    inc edi
    inc esi
    inc charIdxUpload
    jmp CopyULLoop
ULLineDone:
    mov byte ptr [edi], 0
AdvNL:
    mov al, byte ptr [esi]
    cmp al, 13
    je AdvNLInc
    cmp al, 10
    je AdvNLInc
    jmp ProcessUploadedLine
AdvNLInc:
    inc esi
    jmp AdvNL
ProcessUploadedLine:
    lea eax, g_fileLine
    cmp byte ptr [eax], 0
    je UploadLineLoop
    cmp byte ptr [eax], '-'
    je PostRedirect
    push esi
    invoke ParseCSVLineToEntry, OFFSET g_fileLine, ADDR entry
    invoke ValidateEntry, ADDR entry
    cmp eax, 1
    jne InvalidUploadLine
    invoke BuildCSVLineFromEntry, ADDR entry, OFFSET g_lineOut
    invoke UpdateCsvByPatient, ADDR entry, OFFSET g_lineOut
InvalidUploadLine:
    pop esi
    jmp UploadLineLoop

PostRedirect:
    invoke SendText, sock, OFFSET szHttp200OK
    invoke SendText, sock, OFFSET szHtmlStart
    invoke SendText, sock, CStr("<meta http-equiv='refresh' content='0; url=/' /><h2>Action Processed. Returning to main screen...</h2>")
    invoke SendText, sock, OFFSET szHtmlEnd
    invoke RemoveClient, clientIdx
    ret
ProcessHttpClient ENDP
; ---------------------------------------

CheckPendingAndTimeout PROC clientIdx:DWORD
    LOCAL nowTick:DWORD
    LOCAL sinceTick:DWORD
    LOCAL lastTick:DWORD
    LOCAL timeoutVal:DWORD
    LOCAL sock:DWORD
    invoke GetClientSockPtr, clientIdx
    mov eax, DWORD PTR [eax]
    mov sock, eax
    cmp eax, 0
    je CPAT_Done
    invoke GetTickCount
    mov nowTick, eax
    invoke GetPendingFlagPtr, clientIdx
    cmp DWORD PTR [eax], 1
    jne CPAT_Timeout
    invoke GetPendingSincePtr, clientIdx
    mov eax, DWORD PTR [eax]
    mov sinceTick, eax
    mov eax, nowTick
    sub eax, sinceTick
    cmp eax, APPEND_DELAY_MS
    jb CPAT_Timeout
    invoke CommitPendingEntry, clientIdx
    invoke GetTickCount
    mov edx, eax
    invoke GetClientLastPtr, clientIdx
    mov DWORD PTR [eax], edx
CPAT_Timeout:
    invoke GetClientLastPtr, clientIdx
    mov eax, DWORD PTR [eax]
    mov lastTick, eax
    invoke GetClientTimeoutPtr, clientIdx
    mov eax, DWORD PTR [eax]
    mov timeoutVal, eax
    mov eax, nowTick
    sub eax, lastTick
    cmp eax, timeoutVal
    jb CPAT_Done
    invoke crt_printf, OFFSET szTimeoutFmt, sock
    invoke RemoveClient, clientIdx
CPAT_Done:
    ret
CheckPendingAndTimeout ENDP

AppendReceivedBytes PROC uses ebx clientIdx:DWORD, pBytes:DWORD, cbBytes:DWORD
    LOCAL pBuf:DWORD
    LOCAL pLen:DWORD
    LOCAL curLen:DWORD
    LOCAL idx:DWORD
    LOCAL charCode:DWORD
    LOCAL isHttp:DWORD
    
    invoke GetClientTypePtr, clientIdx
    mov eax, DWORD PTR [eax]
    mov isHttp, eax
    
    invoke GetClientBufferPtr, clientIdx
    mov pBuf, eax
    invoke GetClientBufLenPtr, clientIdx
    mov pLen, eax
    mov eax, DWORD PTR [eax]
    mov curLen, eax
    mov idx, 0

ARB_Loop:
    mov eax, idx
    cmp eax, cbBytes
    jge ARB_Done
    mov edx, pBytes
    add edx, eax
    movzx eax, BYTE PTR [edx]
    mov charCode, eax
    
    cmp isHttp, 1
    je ARB_BufferAll
    
    cmp al, 0Dh
    je ARB_NextInc
    cmp al, 0Ah
    je ARB_LineComplete
    
ARB_BufferAll:
    mov eax, curLen
    cmp eax, CLIENT_BUF_SIZE - 1
    jge ARB_NextInc
    mov edx, pBuf
    add edx, eax
    mov ebx, charCode
    mov BYTE PTR [edx], bl
    mov eax, curLen
    inc eax
    mov curLen, eax

ARB_NextInc:
    mov eax, idx
    inc eax
    mov idx, eax
    
    cmp isHttp, 1
    jne ARB_Loop
    jmp ARB_Loop

ARB_LineComplete:
    mov edx, pBuf
    add edx, curLen
    mov BYTE PTR [edx], 0
    invoke ProcessClientLine, clientIdx, pBuf
    mov curLen, 0
    invoke RtlZeroMemory, pBuf, CLIENT_BUF_SIZE
    mov eax, idx
    inc eax
    mov idx, eax
    jmp ARB_Loop

ARB_Done:
    mov eax, pLen
    mov edx, curLen
    mov DWORD PTR [eax], edx
    
    cmp isHttp, 1
    jne ARB_Ret
    
    mov edx, pBuf
    add edx, curLen
    mov BYTE PTR [edx], 0
    
    invoke ProcessHttpClient, clientIdx, pBuf
ARB_Ret:
    ret
AppendReceivedBytes ENDP

PollClient PROC clientIdx:DWORD
    LOCAL sock:DWORD
    LOCAL cb:DWORD
    invoke GetClientSockPtr, clientIdx
    mov eax, DWORD PTR [eax]
    mov sock, eax
    cmp eax, 0
    je PC_Done
    cmp eax, INVALID_SOCKET
    je PC_Done
    invoke recv, sock, OFFSET g_recvBuf, RECV_SIZE, 0
    mov cb, eax
    cmp eax, 0
    jg PC_Data
    cmp eax, 0
    je PC_Disconnect
    invoke WSAGetLastError
    cmp eax, WSAEWOULDBLOCK
    je PC_CheckTimers
    jmp PC_Disconnect
PC_Data:
    invoke GetTickCount
    mov edx, eax
    invoke GetClientLastPtr, clientIdx
    mov DWORD PTR [eax], edx
    invoke AppendReceivedBytes, clientIdx, OFFSET g_recvBuf, cb
    jmp PC_CheckTimers
PC_Disconnect:
    invoke RemoveClient, clientIdx
    jmp PC_Done
PC_CheckTimers:
    invoke GetClientTypePtr, clientIdx
    cmp DWORD PTR [eax], 1
    je PC_Done
    invoke CheckPendingAndTimeout, clientIdx
PC_Done:
    ret
PollClient ENDP

InitArrays PROC
    invoke RtlZeroMemory, OFFSET g_clients, SIZEOF g_clients
    invoke RtlZeroMemory, OFFSET g_clientIDs, SIZEOF g_clientIDs
    invoke RtlZeroMemory, OFFSET g_clientType, SIZEOF g_clientType
    invoke RtlZeroMemory, OFFSET g_clientLastTick, SIZEOF g_clientLastTick
    invoke RtlZeroMemory, OFFSET g_clientTimeout, SIZEOF g_clientTimeout
    invoke RtlZeroMemory, OFFSET g_clientBufLen, SIZEOF g_clientBufLen
    invoke RtlZeroMemory, OFFSET g_pendingFlag, SIZEOF g_pendingFlag
    invoke RtlZeroMemory, OFFSET g_pendingSince, SIZEOF g_pendingSince
    ret
InitArrays ENDP

InitSockets PROC
    invoke WSAStartup, 0202h, OFFSET wsaData
    ret
InitSockets ENDP

SetNonBlocking PROC sock:DWORD
    LOCAL mode:DWORD
    mov mode, 1
    invoke ioctlsocket, sock, FIONBIO, ADDR mode
    ret
SetNonBlocking ENDP

StartTelnetServer PROC
    LOCAL sinbuf[16]:BYTE
    LOCAL s:DWORD
    invoke socket, AF_INET, SOCK_STREAM, IPPROTO_TCP
    mov g_listenSocket, eax
    cmp eax, INVALID_SOCKET
    jne STS_SocketOK
    invoke crt_printf, OFFSET szListenFailFmt, g_TelnetPort
    xor eax, eax
    ret
STS_SocketOK:
    invoke RtlZeroMemory, ADDR sinbuf, 16
    lea edx, sinbuf
    mov WORD PTR [edx+0], AF_INET
    invoke htons, g_TelnetPort
    lea edx, sinbuf
    mov WORD PTR [edx+2], ax
    mov DWORD PTR [edx+4], INADDR_ANY
    invoke bind, g_listenSocket, ADDR sinbuf, 16
    cmp eax, SOCKET_ERROR
    jne STS_BindOK
    invoke crt_printf, OFFSET szListenFailFmt, g_TelnetPort
    invoke closesocket, g_listenSocket
    mov g_listenSocket, INVALID_SOCKET
    xor eax, eax
    ret
STS_BindOK:
    invoke listen, g_listenSocket, SOMAXCONN
    cmp eax, SOCKET_ERROR
    jne STS_ListenOK
    invoke crt_printf, OFFSET szListenFailFmt, g_TelnetPort
    invoke closesocket, g_listenSocket
    mov g_listenSocket, INVALID_SOCKET
    xor eax, eax
    ret
STS_ListenOK:
    invoke SetNonBlocking, g_listenSocket
    invoke crt_printf, OFFSET szListenFmt, g_TelnetPort
    mov eax, 1
    ret
StartTelnetServer ENDP

StartHttpServer PROC
    LOCAL sinbuf[16]:BYTE
    LOCAL s:DWORD
    invoke socket, AF_INET, SOCK_STREAM, IPPROTO_TCP
    mov g_listenSocketHttp, eax
    cmp eax, INVALID_SOCKET
    jne SHS_SocketOK
    invoke crt_printf, OFFSET szListenFailFmt, g_HttpPort
    xor eax, eax
    ret
SHS_SocketOK:
    invoke RtlZeroMemory, ADDR sinbuf, 16
    lea edx, sinbuf
    mov WORD PTR [edx+0], AF_INET
    invoke htons, g_HttpPort
    lea edx, sinbuf
    mov WORD PTR [edx+2], ax
    mov DWORD PTR [edx+4], INADDR_ANY
    invoke bind, g_listenSocketHttp, ADDR sinbuf, 16
    cmp eax, SOCKET_ERROR
    jne SHS_BindOK
    invoke crt_printf, OFFSET szListenFailFmt, g_HttpPort
    invoke closesocket, g_listenSocketHttp
    mov g_listenSocketHttp, INVALID_SOCKET
    xor eax, eax
    ret
SHS_BindOK:
    invoke listen, g_listenSocketHttp, SOMAXCONN
    cmp eax, SOCKET_ERROR
    jne SHS_ListenOK
    invoke crt_printf, OFFSET szListenFailFmt, g_HttpPort
    invoke closesocket, g_listenSocketHttp
    mov g_listenSocketHttp, INVALID_SOCKET
    xor eax, eax
    ret
SHS_ListenOK:
    invoke SetNonBlocking, g_listenSocketHttp
    invoke crt_printf, OFFSET szListenHttpFmt, g_HttpPort
    mov eax, 1
    ret
StartHttpServer ENDP

FindFreeClientSlot PROC
    LOCAL idx:DWORD
    mov idx, 0
FFCS_Loop:
    cmp idx, MAX_CLIENTS
    jge FFCS_None
    invoke GetClientSockPtr, idx
    cmp DWORD PTR [eax], 0
    je FFCS_Found
    mov eax, idx
    inc eax
    mov idx, eax
    jmp FFCS_Loop
FFCS_Found:
    mov eax, idx
    ret
FFCS_None:
    mov eax, -1
    ret
FindFreeClientSlot ENDP

AddClient PROC sock:DWORD, clientType:DWORD
    LOCAL idx:DWORD
    LOCAL pBuf:DWORD
    invoke FindFreeClientSlot
    mov idx, eax
    cmp eax, -1
    jne AC_Add
    invoke closesocket, sock
    ret
AC_Add:
    invoke SetNonBlocking, sock
    invoke GetClientSockPtr, idx
    mov edx, sock
    mov DWORD PTR [eax], edx
    invoke GetClientIDPtr, idx
    mov edx, sock
    mov DWORD PTR [eax], edx
    
    invoke GetClientTypePtr, idx
    mov edx, clientType
    mov DWORD PTR [eax], edx
    
    invoke GetClientBufLenPtr, idx
    mov DWORD PTR [eax], 0
    invoke GetClientBufferPtr, idx
    mov pBuf, eax
    invoke RtlZeroMemory, pBuf, CLIENT_BUF_SIZE
    invoke GetTickCount
    mov edx, eax
    invoke GetClientLastPtr, idx
    mov DWORD PTR [eax], edx
    invoke GetClientTimeoutPtr, idx
    mov DWORD PTR [eax], DEFAULT_TIMEOUT_MS
    invoke GetPendingFlagPtr, idx
    mov DWORD PTR [eax], 0
    invoke GetPendingSincePtr, idx
    mov DWORD PTR [eax], 0
    
    cmp clientType, 1
    je AC_HttpLog
    invoke crt_printf, OFFSET szClientConnFmt, sock
    invoke SendText, sock, OFFSET szConnected
    jmp AC_Done
AC_HttpLog:
    invoke crt_printf, OFFSET szHttpConnFmt, sock
AC_Done:
    ret
AddClient ENDP

RemoveClient PROC idx:DWORD
    LOCAL sock:DWORD
    invoke GetClientSockPtr, idx
    mov eax, DWORD PTR [eax]
    mov sock, eax
    cmp sock, 0
    je RC_Clear
    invoke closesocket, sock
    invoke crt_printf, OFFSET szClientDiscFmt, sock
RC_Clear:
    invoke GetClientSockPtr, idx
    mov DWORD PTR [eax], 0
    invoke GetClientIDPtr, idx
    mov DWORD PTR [eax], 0
    invoke GetClientTypePtr, idx
    mov DWORD PTR [eax], 0
    invoke GetClientBufLenPtr, idx
    mov DWORD PTR [eax], 0
    invoke GetPendingFlagPtr, idx
    mov DWORD PTR [eax], 0
    invoke GetPendingSincePtr, idx
    mov DWORD PTR [eax], 0
    ret
RemoveClient ENDP

AcceptNewClients PROC
    LOCAL s:DWORD
ANC_TelnetLoop:
    invoke accept, g_listenSocket, NULL, NULL
    mov s, eax
    cmp eax, INVALID_SOCKET
    je ANC_HttpLoop
    invoke AddClient, s, 0
    jmp ANC_TelnetLoop

ANC_HttpLoop:
    invoke accept, g_listenSocketHttp, NULL, NULL
    mov s, eax
    cmp eax, INVALID_SOCKET
    je ANC_Done
    invoke AddClient, s, 1
    jmp ANC_HttpLoop

ANC_Done:
    ret
AcceptNewClients ENDP

ServerStart PROC
    cmp g_bRunning, 1
    je SS_Already
    invoke StartTelnetServer
    invoke StartHttpServer
    mov g_bRunning, 1
    invoke crt_printf, OFFSET szServerStarted
    invoke ModifyMenu, g_hMenu, ID_TRAY_TOGGLE, MF_BYCOMMAND or MF_STRING, ID_TRAY_TOGGLE, OFFSET szMenuStop
SS_Already:
    ret
ServerStart ENDP

ServerStop PROC
    LOCAL idx:DWORD
    cmp g_bRunning, 0
    je SST_Already
    mov idx, 0
SST_Loop:
    cmp idx, MAX_CLIENTS
    jge SST_Listen
    invoke RemoveClient, idx
    mov eax, idx
    inc eax
    mov idx, eax
    jmp SST_Loop
SST_Listen:
    cmp g_listenSocket, INVALID_SOCKET
    je SST_Http
    invoke closesocket, g_listenSocket
    mov g_listenSocket, INVALID_SOCKET
SST_Http:
    cmp g_listenSocketHttp, INVALID_SOCKET
    je SST_Done
    invoke closesocket, g_listenSocketHttp
    mov g_listenSocketHttp, INVALID_SOCKET
SST_Done:
    mov g_bRunning, 0
    invoke crt_printf, OFFSET szServerStopped
    invoke ModifyMenu, g_hMenu, ID_TRAY_TOGGLE, MF_BYCOMMAND or MF_STRING, ID_TRAY_TOGGLE, OFFSET szMenuStart
SST_Already:
    ret
ServerStop ENDP

ServerLoop PROC
    LOCAL idx:DWORD
    LOCAL msg:MSG
SL_PumpLoop:
    invoke PeekMessage, ADDR msg, NULL, 0, 0, PM_REMOVE
    test eax, eax
    jz SL_NoMsg
    invoke TranslateMessage, ADDR msg
    invoke DispatchMessage, ADDR msg
    jmp SL_PumpLoop
SL_NoMsg:
    cmp g_bRunning, 1
    jne SL_Done
    invoke AcceptNewClients
    mov idx, 0
SL_Loop:
    cmp idx, MAX_CLIENTS
    jge SL_Done
    invoke PollClient, idx
    mov eax, idx
    inc eax
    mov idx, eax
    jmp SL_Loop
SL_Done:
    ret
ServerLoop ENDP

WndProc PROC hWnd:HWND, uMsg:UINT, wParam:WPARAM, lParam:LPARAM
    LOCAL pt:POINT
    LOCAL cmd:DWORD
    mov eax, uMsg
    cmp eax, WM_TRAYICON
    jne WP_NotTray
    mov eax, lParam
    cmp eax, WM_RBUTTONUP
    jne WP_NotRClick
    invoke GetCursorPos, ADDR pt
    invoke SetForegroundWindow, hWnd
    invoke TrackPopupMenu, g_hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL
    xor eax, eax
    ret
WP_NotRClick:
    xor eax, eax
    ret
WP_NotTray:
    cmp eax, WM_COMMAND
    jne WP_NotCmd
    mov eax, wParam
    and eax, 0FFFFh
    mov cmd, eax
    cmp cmd, ID_TRAY_TOGGLE
    jne WP_NotToggle
    cmp g_bRunning, 1
    je WP_DoStop
    invoke ServerStart
    xor eax, eax
    ret
WP_DoStop:
    invoke ServerStop
    xor eax, eax
    ret
WP_NotToggle:
    cmp cmd, ID_TRAY_SHOW
    jne WP_NotShow
    invoke GetConsoleWindow
    test eax, eax
    jz WP_NotShow
    invoke ShowWindow, eax, SW_RESTORE
    invoke SetForegroundWindow, eax
    xor eax, eax
    ret
WP_NotShow:
    cmp cmd, ID_TRAY_EXIT
    jne WP_NotExit
    invoke ServerStop
    invoke Shell_NotifyIcon, NIM_DELETE, ADDR nid
    invoke PostQuitMessage, 0
    xor eax, eax
    ret
WP_NotExit:
    xor eax, eax
    ret
WP_NotCmd:
    cmp eax, WM_DESTROY
    jne WP_Def
    invoke ServerStop
    invoke Shell_NotifyIcon, NIM_DELETE, ADDR nid
    invoke PostQuitMessage, 0
    xor eax, eax
    ret
WP_Def:
    invoke DefWindowProc, hWnd, uMsg, wParam, lParam
    ret
WndProc ENDP

CreateTray PROC
    LOCAL wc:WNDCLASSEX
    invoke GetModuleHandle, NULL
    mov g_hInstance, eax
    mov wc.cbSize, SIZEOF WNDCLASSEX
    mov wc.style, 0
    mov wc.lpfnWndProc, OFFSET WndProc
    mov wc.cbClsExtra, 0
    mov wc.cbWndExtra, 0
    push g_hInstance
    pop wc.hInstance
    invoke LoadIcon, NULL, IDI_APPLICATION
    mov wc.hIcon, eax
    invoke LoadCursor, NULL, IDC_ARROW
    mov wc.hCursor, eax
    mov wc.hbrBackground, COLOR_WINDOW+1
    mov wc.lpszMenuName, 0
    mov wc.lpszClassName, OFFSET szWndClass
    mov wc.hIconSm, 0
    invoke RegisterClassEx, ADDR wc
    invoke CreateWindowEx, 0, OFFSET szWndClass, OFFSET szTrayTip,
        0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_hInstance, NULL
    mov g_hMainWnd, eax
    invoke CreatePopupMenu
    mov g_hMenu, eax
    cmp g_bRunning, 1
    je CT_AddStop
    invoke AppendMenu, g_hMenu, MF_STRING, ID_TRAY_TOGGLE, OFFSET szMenuStart
    jmp CT_AfterToggle
CT_AddStop:
    invoke AppendMenu, g_hMenu, MF_STRING, ID_TRAY_TOGGLE, OFFSET szMenuStop
CT_AfterToggle:
    invoke AppendMenu, g_hMenu, MF_SEPARATOR, 0, NULL
    invoke AppendMenu, g_hMenu, MF_STRING, ID_TRAY_SHOW, OFFSET szMenuShow
    invoke AppendMenu, g_hMenu, MF_SEPARATOR, 0, NULL
    invoke AppendMenu, g_hMenu, MF_STRING, ID_TRAY_EXIT, OFFSET szMenuExit
    mov nid.cbSize, SIZEOF NOTIFYICONDATA
    push g_hMainWnd
    pop nid.hwnd
    mov nid.uID, 1
    mov nid.uFlags, NIF_ICON or NIF_MESSAGE or NIF_TIP
    mov nid.uCallbackMessage, WM_TRAYICON
    invoke LoadIcon, NULL, IDI_APPLICATION
    mov nid.hIcon, eax
    invoke lstrcpy, ADDR nid.szTip, OFFSET szTrayTip
    invoke Shell_NotifyIcon, NIM_ADD, ADDR nid
    ret
CreateTray ENDP

Main PROC
    LOCAL qmsg:MSG
    invoke ResolveIniPath   ; Map .exe to .ini
    invoke crt_printf, OFFSET szStartup
    invoke LoadConfig
    invoke crt_printf, OFFSET szConfigFmt, OFFSET g_aeCalled, g_TelnetPort, g_HttpPort, g_TelnetTimeout, g_DebugLog
    invoke InitArrays
    invoke InitSockets
    invoke CreateCSVIfMissing
    invoke CreateTray
    invoke ServerStart
MainLoop:
    invoke ServerLoop
    invoke PeekMessage, ADDR qmsg, NULL, WM_QUIT, WM_QUIT, PM_NOREMOVE
    test eax, eax
    jnz MainExit
    invoke Sleep, 10
    jmp MainLoop
MainExit:
    ret
Main ENDP

start:
    invoke Main
    invoke WSACleanup
    invoke ExitProcess, 0
end start