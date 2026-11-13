; Установщик Harmony для Windows (NSIS 3).
;
; Берёт готовую папку dist\win (её раскладывает tools/deploy-win.sh: harmony.exe,
; harmony-cli.exe, DLL и плагины Qt) и собирает один Harmony-<версия>-setup.exe.
;
; Сборка (обычно через tools/make-installer-win.sh):
;   makensis /DVERSION=0.1.0 /DSRCDIR=dist\win /DOUTFILE=dist\Harmony-0.1.0-setup.exe harmony.nsi
;
; Что делает установщик: копирует программу в Program Files\Harmony, создаёт ярлык
; в меню «Пуск» (и по желанию на рабочем столе), регистрирует программу в
; «Приложения и возможности» с деинсталлятором. Настройки, версии и резервные копии
; пользователя (AppData, Документы\Harmony) не трогает ни установка, ни удаление.

Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

!ifndef VERSION
    !error "Не задана версия: makensis /DVERSION=…"
!endif
!ifndef SRCDIR
    !error "Не задана папка с программой: makensis /DSRCDIR=…"
!endif
!ifndef OUTFILE
    !define OUTFILE "Harmony-${VERSION}-setup.exe"
!endif

; Ключ программы в списке установленных.
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Harmony"

Name "Harmony ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\Harmony"
; Повторная установка — в ту же папку, что и прошлая.
InstallDirRegKey HKLM "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "Harmony ${VERSION}"

; Свойства самого setup.exe (видны в «Свойства → Подробно»).
VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=0 "ProductName" "Harmony"
VIAddVersionKey /LANG=0 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileDescription" "Harmony ${VERSION} Setup"
VIAddVersionKey /LANG=0 "LegalCopyright" "Harmony"

; --- Страницы мастера -----------------------------------------------------------
!define MUI_ICON   "..\..\app\resources\app\harmony.ico"
!define MUI_UNICON "..\..\app\resources\app\harmony.ico"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC

; Последняя страница: галочка «Запустить Harmony».
!define MUI_FINISHPAGE_RUN "$INSTDIR\harmony.exe"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Язык мастера выбирается по языку Windows; первый в списке — запасной.
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

LangString SecProgram ${LANG_ENGLISH} "Harmony"
LangString SecProgram ${LANG_RUSSIAN} "Harmony"
LangString SecDesktop ${LANG_ENGLISH} "Desktop shortcut"
LangString SecDesktop ${LANG_RUSSIAN} "Ярлык на рабочем столе"
LangString Need64Bit  ${LANG_ENGLISH} "Harmony requires 64-bit Windows."
LangString Need64Bit  ${LANG_RUSSIAN} "Для Harmony нужна 64-разрядная Windows."

; --- Установка ----------------------------------------------------------------
Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP $(Need64Bit)
        Abort
    ${EndIf}
    ; Ключи деинсталлятора — в 64-разрядной ветке реестра, рядом с другими программами.
    SetRegView 64
FunctionEnd

Section "!$(SecProgram)" SecMain
    SectionIn RO   ; саму программу снять нельзя

    SetOutPath "$INSTDIR"
    File /r "${SRCDIR}\*.*"
    WriteUninstaller "$INSTDIR\uninstall.exe"

    CreateShortcut "$SMPROGRAMS\Harmony.lnk" "$INSTDIR\harmony.exe"

    ; «Приложения и возможности»: имя, версия, значок, размер, удаление.
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "DisplayName"     "Harmony"
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "DisplayVersion"  "${VERSION}"
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "DisplayIcon"     "$INSTDIR\harmony.exe"
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "Publisher"       "Harmony"
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
    SectionGetSize ${SecMain} $0
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" $0
SectionEnd

Section "$(SecDesktop)" SecDesktopShortcut
    CreateShortcut "$DESKTOP\Harmony.lnk" "$INSTDIR\harmony.exe"
SectionEnd

; --- Удаление -----------------------------------------------------------------
Function un.onInit
    SetRegView 64
FunctionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\Harmony.lnk"
    Delete "$DESKTOP\Harmony.lnk"

    ; Удаляем только то, что кладёт установщик, а не всю папку целиком:
    ; если её выбрали неудачно (например, C:\Program Files), чужое не пострадает.
    Delete "$INSTDIR\*.exe"
    Delete "$INSTDIR\*.dll"
    !macro RemovePluginDir dir
        RMDir /r "$INSTDIR\${dir}"
    !macroend
    !insertmacro RemovePluginDir "platforms"
    !insertmacro RemovePluginDir "styles"
    !insertmacro RemovePluginDir "imageformats"
    !insertmacro RemovePluginDir "iconengines"
    !insertmacro RemovePluginDir "tls"
    !insertmacro RemovePluginDir "networkinformation"
    ; В translations пользователь мог положить свои переводы (.ts) — их оставляем.
    Delete "$INSTDIR\translations\qtbase_*.qm"
    RMDir "$INSTDIR\translations"
    RMDir "$INSTDIR"   ; без /r: папка удаляется, только если опустела

    DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd
