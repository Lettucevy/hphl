; ==============================================================================
; HP-HL SDK Official Windows Installer Script (Inno Setup 6)
; High Performance and High Level Language
; ==============================================================================

#define MyAppName "HP-HL SDK"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "HP-HL Team"
#define MyAppURL "https://hphl.dev"
#define MyAppSupportURL "https://hphl.dev/docs"
#define MyAppUpdatesURL "https://hphl.dev/download"
#define MyAppExeName "hphlc.exe"

[Setup]
AppId={{8B49B15F-2D38-4AE8-B39C-B5C4AE463876}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppSupportURL}
AppUpdatesURL={#MyAppUpdatesURL}
DefaultDirName=C:\HPHL
DefaultGroupName=HP-HL
AllowNoIcons=yes
LicenseFile=..\..\LICENSE
OutputDir=..\..\dist
OutputBaseFilename=hphl-setup-v1.0.0-windows-x64
SetupIconFile=..\..\assets\icon.ico
WizardImageFile=..\..\assets\wizard_large.bmp
WizardSmallImageFile=..\..\assets\wizard_small.bmp
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ChangesEnvironment=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
UninstallDisplayIcon={app}\assets\icon.ico

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[CustomMessages]
brazilianportuguese.CompCore=Compilador Core (hphlc) e Runtime Nativo (Obrigatório)
brazilianportuguese.CompVsCode=Extensão oficial para Visual Studio Code / Cursor
brazilianportuguese.CompExamples=Exemplos de Código e Demonstrações
brazilianportuguese.CompDocs=Documentação Técnica Oficial
brazilianportuguese.TaskEnvPath=Adicionar pasta bin do HP-HL ao PATH do sistema/usuário
brazilianportuguese.TaskEnvHome=Definir variável de ambiente HPHL_HOME
brazilianportuguese.TaskAssoc=Associar arquivos de código-fonte (.hphl) ao HP-HL
brazilianportuguese.TaskInstallExt=Instalar extensão automaticamente no VS Code / Cursor detectado
brazilianportuguese.InstallingExt=Instalando extensão oficial no Visual Studio Code...
brazilianportuguese.RunVerify=Executar 'hphlc --version' para verificar a instalação

english.CompCore=Core Compiler (hphlc) and Native Runtime (Required)
english.CompVsCode=Official Visual Studio Code / Cursor Extension
english.CompExamples=Code Examples and Demos
english.CompDocs=Official Technical Documentation
english.TaskEnvPath=Add HP-HL bin folder to system/user PATH
english.TaskEnvHome=Set HPHL_HOME environment variable
english.TaskAssoc=Associate .hphl source code files with HP-HL
english.TaskInstallExt=Automatically install extension into detected VS Code / Cursor
english.InstallingExt=Installing official extension into Visual Studio Code...
english.RunVerify=Run 'hphlc --version' to verify installation

spanish.CompCore=Compilador Principal (hphlc) y Runtime Nativo (Requerido)
spanish.CompVsCode=Extensión oficial para Visual Studio Code / Cursor
spanish.CompExamples=Ejemplos de Código y Demostraciones
spanish.CompDocs=Documentación Técnica Oficial
spanish.TaskEnvPath=Agregar carpeta bin de HP-HL al PATH del sistema/usuario
spanish.TaskEnvHome=Establecer la variable de entorno HPHL_HOME
spanish.TaskAssoc=Asociar archivos de código fuente (.hphl) con HP-HL
spanish.TaskInstallExt=Instalar extensión automáticamente en VS Code / Cursor detectado
spanish.InstallingExt=Instalando extensión oficial en Visual Studio Code...
spanish.RunVerify=Ejecutar 'hphlc --version' para verificar la instalación

[Components]
Name: "core"; Description: "{cm:CompCore}"; Types: full compact custom; Flags: fixed
Name: "vscode"; Description: "{cm:CompVsCode}"; Types: full custom
Name: "examples"; Description: "{cm:CompExamples}"; Types: full custom
Name: "docs"; Description: "{cm:CompDocs}"; Types: full custom

[Tasks]
Name: "envPath"; Description: "{cm:TaskEnvPath}"; GroupDescription: "Configurações de Ambiente:"; Flags: checkedonce
Name: "envHome"; Description: "{cm:TaskEnvHome}"; GroupDescription: "Configurações de Ambiente:"; Flags: checkedonce
Name: "assocHphl"; Description: "{cm:TaskAssoc}"; GroupDescription: "Associações de Arquivo:"
Name: "installVscodeExt"; Description: "{cm:TaskInstallExt}"; GroupDescription: "Integração com Editores:"; Components: vscode

[Files]
Source: "..\..\compiler\bin\hphlc.exe"; DestDir: "{app}\bin"; Flags: ignoreversion; Components: core
Source: "..\..\runtime\*"; DestDir: "{app}\runtime"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: core
Source: "..\..\compiler\src\runtime\*"; DestDir: "{app}\runtime\include"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: core
Source: "..\..\examples\*"; DestDir: "{app}\examples"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: examples
Source: "..\..\docs\*"; DestDir: "{app}\docs"; Excludes: "*DOCSLOCAIS*,*.bak,*.tmp"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: docs
Source: "..\..\editors\vscode\hphl-1.0.0.vsix"; DestDir: "{app}\editors\vscode"; Flags: ignoreversion; Components: vscode
Source: "..\..\editors\vscode\install_extension.bat"; DestDir: "{app}\editors\vscode"; Flags: ignoreversion; Components: vscode
Source: "..\..\editors\vscode\install_extension.ps1"; DestDir: "{app}\editors\vscode"; Flags: ignoreversion; Components: vscode
Source: "..\..\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\HP-HL Documentation"; Filename: "{app}\docs\index.md"; Components: docs
Name: "{group}\Install VS Code Extension"; Filename: "{app}\editors\vscode\install_extension.bat"; Components: vscode
Name: "{group}\HP-HL Website"; Filename: "{#MyAppURL}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"

[Registry]
; HPHL_HOME
Root: HKA; Subkey: "Environment"; ValueType: string; ValueName: "HPHL_HOME"; ValueData: "{app}"; Flags: uninsdeletevalue; Tasks: envHome

; Associacao .hphl
Root: HKA; Subkey: "Software\Classes\.hphl"; ValueType: string; ValueName: ""; ValueData: "HPHL.SourceFile"; Flags: uninsdeletevalue; Tasks: assocHphl
Root: HKA; Subkey: "Software\Classes\HPHL.SourceFile"; ValueType: string; ValueName: ""; ValueData: "HP-HL Source File"; Flags: uninsdeletekey; Tasks: assocHphl
Root: HKA; Subkey: "Software\Classes\HPHL.SourceFile\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\assets\icon.ico,0"; Tasks: assocHphl
Root: HKA; Subkey: "Software\Classes\HPHL.SourceFile\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\hphlc.exe"" ""%1"""; Tasks: assocHphl

[Run]
Filename: "cmd.exe"; Parameters: "/c code --install-extension ""{app}\editors\vscode\hphl-1.0.0.vsix"""; Flags: runhidden; Tasks: installVscodeExt; StatusMsg: "{cm:InstallingExt}"
Filename: "{app}\bin\hphlc.exe"; Parameters: "--version"; Flags: nowait postinstall skipifsilent; Description: "{cm:RunVerify}"

[Code]
procedure AddToPath(Param: string);
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
    OrigPath := '';

  if Pos(UpperCase(Param), UpperCase(OrigPath)) = 0 then
  begin
    if (Length(OrigPath) > 0) and (Copy(OrigPath, Length(OrigPath), 1) <> ';') then
      OrigPath := OrigPath + ';';
    OrigPath := OrigPath + Param;
    RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath);
  end;
end;

procedure RemoveFromPath(Param: string);
var
  OrigPath: string;
  P, L: Integer;
begin
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
  begin
    P := Pos(UpperCase(Param), UpperCase(OrigPath));
    if P > 0 then
    begin
      L := Length(Param);
      Delete(OrigPath, P, L);
      StringChange(OrigPath, ';;', ';');
      if (Length(OrigPath) > 0) and (Copy(OrigPath, Length(OrigPath), 1) = ';') then
        Delete(OrigPath, Length(OrigPath), 1);
      if (Length(OrigPath) > 0) and (Copy(OrigPath, 1, 1) = ';') then
        Delete(OrigPath, 1, 1);
      RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath);
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if WizardIsTaskSelected('envPath') then
      AddToPath(ExpandConstant('{app}\bin'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RemoveFromPath(ExpandConstant('{app}\bin'));
  end;
end;
