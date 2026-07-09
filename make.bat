rem c:\masm32\bin\ml /c /coff /Cp pacs-server01.asm
rem c:\masm32\bin\link /subsystem:windows pacs-server01.obj
rem c:\masm32\bin\ml /c /coff /Cp worklist-server01.asm
rem c:\masm32\bin\link /subsystem:windows worklist-server01.obj
rem c:\masm32\bin\ml /c /coff /Cp PACS-MANAGER.asm
rem c:\masm32\bin\link /subsystem:windows PACS-MANAGER.obj
c:\masm32\bin\ml /c /coff /Cp WORKLIST_ADDCSV.asm
c:\masm32\bin\link /subsystem:windows WORKLIST_ADDCSV.obj
@rem pacs-server01.exe