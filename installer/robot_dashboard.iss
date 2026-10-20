; 로봇 대시보드 설치 파일(Inno Setup 6.7). installer\build_installer.cmd 가 dist\stage 를 채우고 /DAppVersion=x.y.z 로 컴파일한다.
; - 같은 AppId 로 새 버전을 깔면 업그레이드: 이전 프로그램 파일은 [InstallDelete] 로 지우고 새로 깐다. 설정(레지스트리)은 그대로.
; - 설정은 대시보드의 QSettings 와 같은 곳(HKCU\Software\RobotDashboard\RobotDashboard)의 host · port · updateCheck.
;   값이 없을 때만 '기본 설정' 페이지 값으로 쓰고, 있으면 페이지에 미리 채워 두었다가 바뀐 값만 쓴다.
; - 숨은 인자: /SETTINGSKEY=<HKCU 아래 키>(시험용 — 실제 설정 대신 다른 키를 쓴다) · /RELAUNCH=1(무인 설치 뒤 다시 켬, 자동 업데이트가 준다)

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define StageDir "..\dist\stage"
#define ExeName "robot_dashboard.exe"

[Setup]
AppId={{B7D82307-ECDE-49A5-A9C4-80D2C7990238}
AppName=Robot Dashboard
AppVersion={#AppVersion}
AppVerName=Robot Dashboard {#AppVersion}
AppPublisher=ghlee0786
AppPublisherURL=https://github.com/ghlee0786/ros-mobile-robot
AppSupportURL=https://github.com/ghlee0786/ros-mobile-robot/issues
AppUpdatesURL=https://github.com/ghlee0786/ros-mobile-robot/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\RobotDashboard
DisableProgramGroupPage=yes
DisableWelcomePage=no
; 기본은 사용자별 설치(관리자 권한 없이 자동 업데이트), 마법사 첫 대화상자에서 '모든 사용자' 도 고를 수 있다
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=RobotDashboard-Setup-{#AppVersion}
SetupIconFile=..\dashboard\icons\app.ico
UninstallDisplayIcon={app}\{#ExeName}
UninstallDisplayName=Robot Dashboard
WizardStyle=modern
WizardImageFile=assets\wizard.bmp
WizardSmallImageFile=assets\wizard_small.bmp
Compression=lzma2/max
SolidCompression=yes
CloseApplications=yes
RestartApplications=yes

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
korean.WelcomeLabel2=[name/ver] 을(를) 설치합니다.%n%n라즈베리 파이 + Pico 로 움직이는 ROS 2 로봇을 Windows PC 에서 보고 · 조종하고 · 지도 위 목표로 보내는 대시보드입니다.%n%n다음 화면에서 주요 기능을 소개합니다. 계속하기 전에 다른 프로그램을 닫는 것이 좋습니다.
english.WelcomeLabel2=This will install [name/ver] on your computer.%n%nWatch, drive and send navigation goals to a Raspberry Pi + Pico ROS 2 robot from your Windows PC.%n%nThe next page shows the main features. It is recommended that you close all other applications before continuing.

[CustomMessages]
korean.AppDisplayName=로봇 대시보드
english.AppDisplayName=Robot Dashboard
korean.FeaturesTitle=제품 특징
english.FeaturesTitle=Features
korean.FeaturesDesc=로봇 대시보드로 할 수 있는 일
english.FeaturesDesc=What Robot Dashboard can do
korean.SettingsTitle=기본 설정
english.SettingsTitle=Basic settings
korean.SettingsDesc=로봇 연결과 업데이트 설정
english.SettingsDesc=Robot connection and updates
korean.SettingsSub=대시보드가 처음 연결할 로봇(rosbridge) 주소입니다. 나중에 '연결' 화면에서 바꿀 수 있습니다. 이미 설치되어 있으면 지금 쓰는 값이 채워져 있습니다.
english.SettingsSub=The robot (rosbridge) the dashboard connects to first. You can change it later on the Connect page. On upgrade the current values are filled in.
korean.HostLabel=로봇 주소:
english.HostLabel=Robot address:
korean.PortLabel=rosbridge 포트:
english.PortLabel=rosbridge port:
korean.UpdateLabel=시작할 때 새 버전 확인(GitHub Releases)
english.UpdateLabel=Check for new versions at startup (GitHub Releases)
korean.DesktopLabel=바탕 화면에 바로 가기 만들기
english.DesktopLabel=Create a desktop shortcut
korean.RunLabel=설치가 끝나면 로봇 대시보드 실행
english.RunLabel=Launch Robot Dashboard when setup finishes
korean.BadHost=로봇 주소를 입력하세요.
english.BadHost=Enter the robot address.
korean.BadPort=포트는 1 ~ 65535 사이의 숫자여야 합니다.
english.BadPort=The port must be a number between 1 and 65535.
korean.AskDeleteSettings=로봇 주소 · 창 크기 같은 설정도 지울까요?%n%n'아니요' 를 누르면 설정을 남겨 두어 다시 설치했을 때 그대로 씁니다.
english.AskDeleteSettings=Also delete settings such as the robot address and window size?%n%nChoose No to keep them for a later reinstall.

[InstallDelete]
; 업그레이드: 이전 버전의 프로그램 파일을 지우고 새로 깐다(설정 · 문서 폴더의 기록은 건드리지 않는다)
Type: files; Name: "{app}\{#ExeName}"
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\LICENSE.txt"
Type: files; Name: "{app}\README.md"
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\model"
Type: filesandordirs; Name: "{app}\assetimporters"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\qmltooling"
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\translations"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "assets\features_ko.bmp"; Flags: dontcopy
Source: "assets\features_en.bmp"; Flags: dontcopy

[Icons]
Name: "{autoprograms}\{cm:AppDisplayName}"; Filename: "{app}\{#ExeName}"
Name: "{autodesktop}\{cm:AppDisplayName}"; Filename: "{app}\{#ExeName}"; Check: WantDesktop

[Run]
Filename: "{app}\{#ExeName}"; Flags: nowait runasoriginaluser; Check: WantRun

[Code]
const
  DefaultHost = '192.168.1.201';
  DefaultPort = '9090';

var
  SettingsPage: TInputQueryWizardPage;
  UpdateCheck, DesktopCheck, RunCheck: TNewCheckBox;
  OldHost, OldPort, OldUpdate: String;
  HadHost, HadPort, HadUpdate: Boolean;

function SettingsKey: String;
begin
  Result := ExpandConstant('{param:SETTINGSKEY|Software\RobotDashboard\RobotDashboard}');
end;

// QSettings 는 int 를 REG_DWORD, 문자열 · bool 을 REG_SZ 로 쓴다
procedure ReadOldSettings;
var
  D: Cardinal;
begin
  HadHost := RegQueryStringValue(HKCU, SettingsKey, 'host', OldHost);
  HadPort := RegQueryDWordValue(HKCU, SettingsKey, 'port', D);
  if HadPort then
    OldPort := IntToStr(D)
  else
    HadPort := RegQueryStringValue(HKCU, SettingsKey, 'port', OldPort);
  HadUpdate := RegQueryStringValue(HKCU, SettingsKey, 'updateCheck', OldUpdate);
end;

function AddCheck(Top: Integer; const Caption: String; Checked: Boolean): TNewCheckBox;
begin
  Result := TNewCheckBox.Create(SettingsPage);
  Result.Parent := SettingsPage.Surface;
  Result.Top := Top;
  Result.Width := SettingsPage.SurfaceWidth;
  Result.Height := ScaleY(18);
  Result.Caption := Caption;
  Result.Checked := Checked;
end;

procedure InitializeWizard;
var
  Page: TWizardPage;
  Img: TBitmapImage;
  F: String;
  W, H, Top: Integer;
begin
  ReadOldSettings;

  // (a) 제품 특징: installer\make_assets.py 가 그린 기능 그림
  Page := CreateCustomPage(wpWelcome, CustomMessage('FeaturesTitle'), CustomMessage('FeaturesDesc'));
  if ActiveLanguage = 'korean' then F := 'features_ko.bmp' else F := 'features_en.bmp';
  ExtractTemporaryFile(F);
  Img := TBitmapImage.Create(Page);
  Img.Parent := Page.Surface;
  Img.Bitmap.LoadFromFile(ExpandConstant('{tmp}\') + F);
  Img.Stretch := True;
  W := Page.SurfaceWidth;
  H := W * 570 div 1000;
  if H > Page.SurfaceHeight then
  begin
    H := Page.SurfaceHeight;
    W := H * 1000 div 570;
  end;
  Img.SetBounds((Page.SurfaceWidth - W) div 2, 0, W, H);

  // (c) 기본 설정: 설치 위치 다음
  SettingsPage := CreateInputQueryPage(wpSelectDir, CustomMessage('SettingsTitle'), CustomMessage('SettingsDesc'),
    CustomMessage('SettingsSub'));
  SettingsPage.Add(CustomMessage('HostLabel'), False);
  SettingsPage.Add(CustomMessage('PortLabel'), False);
  if HadHost then SettingsPage.Values[0] := OldHost else SettingsPage.Values[0] := DefaultHost;
  if HadPort then SettingsPage.Values[1] := OldPort else SettingsPage.Values[1] := DefaultPort;
  Top := SettingsPage.Edits[1].Top + SettingsPage.Edits[1].Height + ScaleY(16);
  UpdateCheck := AddCheck(Top, CustomMessage('UpdateLabel'), not HadUpdate or (LowerCase(OldUpdate) <> 'false'));
  DesktopCheck := AddCheck(Top + ScaleY(26), CustomMessage('DesktopLabel'), GetPreviousData('DesktopIcon', '1') = '1');
  RunCheck := AddCheck(Top + ScaleY(52), CustomMessage('RunLabel'), True);
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  if DesktopCheck.Checked then
    SetPreviousData(PreviousDataKey, 'DesktopIcon', '1')
  else
    SetPreviousData(PreviousDataKey, 'DesktopIcon', '0');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Port: Integer;
begin
  Result := True;
  if CurPageID = SettingsPage.ID then
  begin
    Port := StrToIntDef(Trim(SettingsPage.Values[1]), 0);
    if Trim(SettingsPage.Values[0]) = '' then
    begin
      MsgBox(CustomMessage('BadHost'), mbError, MB_OK);
      Result := False;
    end
    else if (Port < 1) or (Port > 65535) then
    begin
      MsgBox(CustomMessage('BadPort'), mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function WantDesktop: Boolean;
begin
  Result := DesktopCheck.Checked;
end;

// 무인 설치(자동 업데이트)는 /RELAUNCH=1 일 때만 다시 켠다
function WantRun: Boolean;
begin
  if WizardSilent then
    Result := ExpandConstant('{param:RELAUNCH|0}') = '1'
  else
    Result := RunCheck.Checked;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Host, Port: String;
begin
  if CurStep <> ssPostInstall then
    exit;
  Host := Trim(SettingsPage.Values[0]);
  Port := Trim(SettingsPage.Values[1]);
  if not HadHost or (Host <> OldHost) then
    RegWriteStringValue(HKCU, SettingsKey, 'host', Host);
  if not HadPort or (Port <> OldPort) then
    RegWriteDWordValue(HKCU, SettingsKey, 'port', StrToIntDef(Port, 9090));
  if not HadUpdate or (UpdateCheck.Checked <> (LowerCase(OldUpdate) <> 'false')) then
  begin
    if UpdateCheck.Checked then
      RegWriteStringValue(HKCU, SettingsKey, 'updateCheck', 'true')
    else
      RegWriteStringValue(HKCU, SettingsKey, 'updateCheck', 'false');
  end;
end;

// 제거: 설정은 기본으로 남긴다. 대화형 제거에서 '예' 를 고를 때만 지운다(무인 제거는 묻지 않고 남긴다).
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then
    if MsgBox(CustomMessage('AskDeleteSettings'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    begin
      RegDeleteKeyIncludingSubkeys(HKCU, 'Software\RobotDashboard\RobotDashboard');
      RegDeleteKeyIfEmpty(HKCU, 'Software\RobotDashboard');
    end;
end;
