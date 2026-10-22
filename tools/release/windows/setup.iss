#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#ifndef ReleasePath
  #error ReleasePath must point to the staged Release files
#endif
#ifndef OutputPath
  #define OutputPath ReleasePath
#endif

[Setup]
AppId={{8E02F975-4CA8-4C3A-A174-588B975D87A5}
AppName=vinoGram
AppVersion={#MyAppVersion}
AppPublisher=vinoGram contributors
DefaultDirName={localappdata}\Programs\vinoGram
DefaultGroupName=vinoGram
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputPath}
OutputBaseFilename=vinoGram-Setup-{#MyAppVersion}-x64
SetupIconFile=..\..\..\Telegram\Resources\art\icon256.ico
UninstallDisplayIcon={app}\Telegram.exe
LicenseFile={#ReleasePath}\LICENSE.txt
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
CloseApplications=yes
CloseApplicationsFilter=Telegram.exe,xray.exe
VersionInfoVersion={#MyAppVersion}.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#ReleasePath}\Telegram.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleasePath}\xray\*"; DestDir: "{app}\xray"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ReleasePath}\modules\*"; DestDir: "{app}\modules"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ReleasePath}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleasePath}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\vinoGram"; Filename: "{app}\Telegram.exe"; AppUserModelID: "vinoGram.Desktop"
Name: "{autodesktop}\vinoGram"; Filename: "{app}\Telegram.exe"; Tasks: desktopicon; AppUserModelID: "vinoGram.Desktop"

[Run]
Filename: "{app}\Telegram.exe"; Description: "{cm:LaunchProgram,vinoGram}"; Flags: nowait postinstall skipifsilent
