; Corpus installer. Build: tools/installer/build-installer.sh <path to Corpus.exe> [version]
; Per-user install (no admin prompt): %LOCALAPPDATA%\Programs\Corpus. The only payload is Corpus.exe; everything else
; Corpus needs is inside it. The Start menu shortcut, the AppUserModelId key and the "Open with" entries are written by
; Corpus itself at startup, so the installer only makes the first shortcut and removes all of it again on uninstall.

Unicode true
RequestExecutionLevel user
SetCompressor /SOLID lzma

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef EXE
  !error "Pass /DEXE=<path to Corpus.exe>"
!endif
!ifndef OUTFILE
  !define OUTFILE "Corpus-Setup.exe"
!endif

!define APP "Corpus"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Corpus"

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\Corpus"
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"
BrandingText " "

VIAddVersionKey "ProductName" "Corpus"
VIAddVersionKey "FileDescription" "Corpus setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "MIT License"
VIProductVersion "${VERSION}.0"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!insertmacro GetSize
!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_FINISHPAGE_RUN "$INSTDIR\Corpus.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start Corpus"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "PortugueseBR"

; Lets the install and the uninstall wait for a running Corpus to close instead of failing on the locked exe.
Function CheckRunning
  retry:
  ClearErrors
  IfFileExists "$INSTDIR\Corpus.exe" 0 done
  FileOpen $1 "$INSTDIR\Corpus.exe" a
  IfErrors 0 closed
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Corpus is running. Close it, then press Retry." IDRETRY retry
  Abort
  closed:
  FileClose $1
  done:
FunctionEnd

Section "Install"
  SetOutPath "$INSTDIR"
  Call CheckRunning
  File "${EXE}"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; This shortcut has no AppUserModelID. Corpus's own startup shortcut code is what adds Corpus.Corpus (the media
  ; card name); it skips a .lnk whose target already matches, so that needs a GUI change to rewrite it.
  CreateShortcut "$SMPROGRAMS\Corpus.lnk" "$INSTDIR\Corpus.exe"

  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "Corpus"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "GothHeit"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\Corpus.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "Uninstall"
  Call un.CheckRunning
  Delete "$INSTDIR\Corpus.exe"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\Corpus.lnk"

  ; What Corpus registered by itself: media card name, "Open with" (the program's key and the entry under each type).
  DeleteRegKey HKCU "Software\Classes\AppUserModelId\Corpus.Corpus"
  DeleteRegKey HKCU "Software\Classes\Applications\Corpus.exe"
  DeleteRegKey HKCU "Software\Classes\Applications\corpus-gui.exe"
  StrCpy $0 0
  loop:
    EnumRegKey $1 HKCU "Software\Classes" $0
    StrCmp $1 "" done
    StrCpy $2 $1 1
    StrCmp $2 "." 0 next
    DeleteRegKey HKCU "Software\Classes\$1\OpenWithList\Corpus.exe"
    DeleteRegKey HKCU "Software\Classes\$1\OpenWithList\corpus-gui.exe"
    next:
    IntOp $0 $0 + 1
    Goto loop
  done:

  ; The unpacked libmpv is a cache and goes. The library and backups in %LOCALAPPDATA%\Corpus are the user's data and
  ; stay unless they say so.
  Delete "$LOCALAPPDATA\Corpus\libmpv-2-*.dll"
  Delete "$LOCALAPPDATA\Corpus\*.part"
  Delete "$TEMP\Corpus\libmpv-2-*.dll"
  Delete "$TEMP\Corpus\*.part"
  RMDir "$TEMP\Corpus"
  IfSilent keep
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Also delete your Corpus library (tags, settings, backups)?$\r$\nThis cannot be undone." IDNO keep
  RMDir /r "$LOCALAPPDATA\Corpus"
  DeleteRegKey HKCU "Software\Corpus"
  keep:
  RMDir "$LOCALAPPDATA\Corpus"

  DeleteRegKey HKCU "${UNINST_KEY}"
SectionEnd

Function un.CheckRunning
  retry:
  IfFileExists "$INSTDIR\Corpus.exe" 0 done
  ClearErrors
  FileOpen $1 "$INSTDIR\Corpus.exe" a
  IfErrors 0 closed
  IfSilent abort
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Corpus is running. Close it, then press Retry." IDRETRY retry
  abort:
  Abort
  closed:
  FileClose $1
  done:
FunctionEnd
