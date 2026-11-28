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
!insertmacro GetParameters
!insertmacro GetOptions
!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_FINISHPAGE_RUN "$INSTDIR\Corpus.exe"
!define MUI_FINISHPAGE_RUN_TEXT "$(StartCorpus)"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "PortugueseBR"

LangString RunningRetry ${LANG_ENGLISH} "Corpus is running and did not close. Close it, then press Retry."
LangString RunningRetry ${LANG_PORTUGUESEBR} "O Corpus está aberto e não fechou. Feche-o e clique em Repetir."
LangString DeleteLibrary ${LANG_ENGLISH} "Also delete your Corpus library?$\r$\n$\r$\nThis deletes your tags, boards, settings, backups and thumbnails. Your files stay where they are, and the Inbox folder is kept while it has files in it.$\r$\n$\r$\nThis cannot be undone."
LangString DeleteLibrary ${LANG_PORTUGUESEBR} "Apagar também a sua biblioteca do Corpus?$\r$\n$\r$\nIsso apaga suas tags, quadros, configurações, backups e miniaturas. Seus arquivos ficam onde estão, e a pasta Inbox é mantida enquanto tiver arquivos.$\r$\n$\r$\nIsso não pode ser desfeito."
LangString StartCorpus ${LANG_ENGLISH} "Start Corpus"
LangString StartCorpus ${LANG_PORTUGUESEBR} "Iniciar o Corpus"

; The window class of every Corpus window (main and "Open in another window"); apps/gui/window.cpp.
!define WINDOW_CLASS "Kafkas Window Class"

; Asks every running Corpus to close the way its own close button does (the catalog and the page reached in a book are
; saved), waits up to about ten seconds for the exe to unlock, and only then falls back to asking the person to close
; it. A silent run has no one to ask and stops instead. Used by the install and the uninstall.
!macro CheckRunningMacro un
Function ${un}CheckRunning
  IfFileExists "$INSTDIR\Corpus.exe" 0 done
  StrCpy $3 0
  pass:
  ; One window per pass; each pass then waits a little for the exe to unlock: 8 passes are about ten seconds.
  FindWindow $0 "${WINDOW_CLASS}"
  IntCmp $0 0 +2
  SendMessage $0 ${WM_CLOSE} 0 0 /TIMEOUT=2000
  StrCpy $4 0
  waitloop:
  ClearErrors
  FileOpen $1 "$INSTDIR\Corpus.exe" a
  IfErrors 0 closed
  IntOp $4 $4 + 1
  IntCmp $4 5 nextpass 0 nextpass
  Sleep 250
  Goto waitloop
  nextpass:
  IntOp $3 $3 + 1
  IntCmp $3 8 giveup pass giveup
  giveup:
  IfSilent abort
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(RunningRetry)" IDRETRY retry
  abort:
  Abort
  retry:
  StrCpy $3 0
  Goto pass
  closed:
  FileClose $1
  done:
FunctionEnd
!macroend
!insertmacro CheckRunningMacro ""

; The updater runs the setup as "Corpus-Setup.exe /S /RELAUNCH": no pages, and Corpus is started again at the end.
Var Relaunch
Function .onInit
  StrCpy $Relaunch ""
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/RELAUNCH" $1
  IfErrors +2 0
  StrCpy $Relaunch "1"
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

  ; The updater's run: start Corpus again, now the new one.
  IfSilent 0 skiprelaunch
  StrCmp $Relaunch "1" 0 skiprelaunch
  Exec '"$INSTDIR\Corpus.exe"'
  skiprelaunch:
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
  ; The Inbox holds the only copy of what was pasted or dropped into it, so it is never deleted with anything in it:
  ; neither the one of older versions inside the library folder nor the one Settings points at (InboxFolder).
  ReadRegStr $3 HKCU "Software\Corpus" "InboxFolder"
  IfSilent keep
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "$(DeleteLibrary)" IDNO keep
  FindFirst $0 $1 "$LOCALAPPDATA\Corpus\*"
  wipe:
    StrCmp $1 "" wiped
    StrCmp $1 "." wipenext
    StrCmp $1 ".." wipenext
    IfFileExists "$LOCALAPPDATA\Corpus\$1\*.*" 0 wipefile
    StrCmp $1 "Inbox" wipeinbox
    StrCmp "$LOCALAPPDATA\Corpus\$1" $3 wipeinbox
    RMDir /r "$LOCALAPPDATA\Corpus\$1"
    Goto wipenext
    wipeinbox:
    RMDir "$LOCALAPPDATA\Corpus\$1"
    Goto wipenext
    wipefile:
    Delete "$LOCALAPPDATA\Corpus\$1"
    wipenext:
    FindNext $0 $1
    Goto wipe
  wiped:
  FindClose $0
  DeleteRegKey HKCU "Software\Corpus"
  ; An Inbox elsewhere (Pictures\Corpus Inbox) goes only when it is empty.
  StrCmp $3 "" keep
  RMDir "$3"
  keep:
  RMDir "$LOCALAPPDATA\Corpus"

  DeleteRegKey HKCU "${UNINST_KEY}"
SectionEnd

!insertmacro CheckRunningMacro "un."
