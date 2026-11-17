; SPDX-FileCopyrightText: 2026 Mugen Art Lab
; SPDX-License-Identifier: GPL-2.0-or-later

#define MyAppName "Mugen Heartbeat"
#ifndef MyAppVersion
  #error MyAppVersion is required. Use installer\build-installer.ps1.
#endif
#define MyAppPublisher "Mugen Art Lab"
#define MyAppURL "https://mugenartlab.com/"
#define MyRepoURL "https://github.com/Mugen-Art-Lab/Mugen-Heartbeat"
#define MyPluginName "mugen-heartbeat"

#ifndef PayloadRoot
  #error PayloadRoot is required. Use installer\build-installer.ps1.
#endif
#ifndef OutputDir
  #error OutputDir is required. Use installer\build-installer.ps1.
#endif
#ifndef RepoRoot
  #error RepoRoot is required. Use installer\build-installer.ps1.
#endif

[Setup]
AppId={{A1CBF1C6-2AF3-4E97-81E9-9DBC3B2D8917}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyRepoURL}/releases
AppCopyright=Copyright (C) 2026 Mugen Art Lab
DefaultDirName={commonappdata}\obs-studio\plugins\{#MyPluginName}
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Mugen-Heartbeat-{#MyAppVersion}-Windows-x64-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
Uninstallable=yes
CreateUninstallRegKey=yes
CloseApplications=no
RestartApplications=no
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} installer
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"; InfoAfterFile: "{#RepoRoot}\installer\AFTER-INSTALL-EN.txt"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"; InfoAfterFile: "{#RepoRoot}\installer\AFTER-INSTALL-RU.txt"

[Messages]
english.SelectLanguageTitle=Select setup language / Выберите язык установки
english.SelectLanguageLabel=Choose the language for installation / Выберите язык установки
russian.SelectLanguageTitle=Select setup language / Выберите язык установки
russian.SelectLanguageLabel=Choose the language for installation / Выберите язык установки

; The InfoAfterFile page is shown after files have already been installed.
; Replace Inno Setup's generic "continue installation" language only here.
english.WizardInfoAfter=Installation complete
english.InfoAfterLabel=Mugen Heartbeat is ready to use. Here are a few helpful tips.
english.InfoAfterClickLabel=Click Next to finish Setup.
russian.WizardInfoAfter=Установка завершена
russian.InfoAfterLabel=Mugen Heartbeat готов к работе. Ниже — несколько полезных советов.
russian.InfoAfterClickLabel=Нажмите «Далее», чтобы завершить работу мастера.

[CustomMessages]
english.CloseObsTitle=OBS Studio is running
russian.CloseObsTitle=OBS Studio запущена
english.CloseObsRetry=Close OBS Studio completely, then click Retry.
russian.CloseObsRetry=Полностью закройте OBS Studio, затем нажмите «Повторить».
english.CloseObsUninstallTitle=OBS Studio is running
russian.CloseObsUninstallTitle=OBS Studio запущена
english.CloseObsUninstallRetry=Close OBS Studio completely, then click Retry to continue uninstalling.
russian.CloseObsUninstallRetry=Полностью закройте OBS Studio, затем нажмите «Повторить», чтобы продолжить удаление.
english.RetryButton=Retry
russian.RetryButton=Повторить
english.CancelButton=Cancel
russian.CancelButton=Отмена

[Files]
Source: "{#PayloadRoot}\{#MyPluginName}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
; The plugin's ProgramData directory contains release files only.
; User settings and community skins live under %APPDATA%\obs-studio\plugin_config\mugen-heartbeat
; and are intentionally preserved.
Type: filesandordirs; Name: "{app}"
[Code]
function ObsIsRunning(): Boolean;
var
  ResultCode: Integer;
  PowerShellPath: String;
begin
  PowerShellPath := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
  Result := False;
  if FileExists(PowerShellPath) then
  begin
    if Exec(PowerShellPath,
      '-NoProfile -NonInteractive -WindowStyle Hidden -Command "if (Get-Process obs64 -ErrorAction SilentlyContinue) { exit 10 } else { exit 0 }"',
      '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
      Result := ResultCode = 10;
  end;
end;

function AskCloseObs(const ATitle, AText: String): Boolean;
var
  ButtonLabels: TArrayOfString;
begin
  SetArrayLength(ButtonLabels, 2);
  ButtonLabels[0] := CustomMessage('RetryButton');
  ButtonLabels[1] := CustomMessage('CancelButton');

  Result := TaskDialogMsgBox(
    ATitle,
    AText,
    mbError,
    MB_RETRYCANCEL,
    ButtonLabels,
    0) = IDRETRY;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpReady then
  begin
    while ObsIsRunning() do
    begin
      if not AskCloseObs(
        CustomMessage('CloseObsTitle'),
        CustomMessage('CloseObsRetry')) then
      begin
        Result := False;
        Exit;
      end;
    end;
  end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  while ObsIsRunning() do
  begin
    if not AskCloseObs(
      CustomMessage('CloseObsUninstallTitle'),
      CustomMessage('CloseObsUninstallRetry')) then
    begin
      Result := False;
      Exit;
    end;
  end;
end;
