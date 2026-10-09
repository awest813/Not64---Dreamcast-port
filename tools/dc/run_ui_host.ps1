$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskRun=Join-Path $taskRoot 'build/dc/validation/ui-host-run'
New-Item -ItemType Directory -Force -Path "$taskRun/ui-fixtures/empty","$taskRun/saves" | Out-Null
$taskSources=@('tools/dc/test_ui_host.c','platform/dc_config.c','platform/dc_settings.c','platform/dc_overlay.c','platform/dc_menu/dc_ui.c','platform/dc_menu/dc_menu.c','platform/dc_menu/dc_draw_host.c','platform/dc_storage.c','platform/dc_video.c','fileBrowser/fileBrowser.c','fileBrowser/fileBrowser-kos.c','gc_input/controller-DC.c')
Push-Location $taskRoot
try {
  & gcc -std=gnu11 -Wno-macro-redefined -Wno-missing-declarations -D__DREAMCAST__ -DDC_HOST_STUB -D__LINUX__ -I. @taskSources -o "$taskRun/ui-host-tests.exe"
  if($LASTEXITCODE){throw 'Frontend test compilation failed'}
  foreach($taskFormat in @(@('b.z64',0x80,0x37,0x12,0x40),@('a.v64',0x37,0x80,0x40,0x12),@('c.n64',0x40,0x12,0x37,0x80))) {
    $taskBytes=New-Object byte[] 4096
    for($taskIndex=0;$taskIndex -lt 4;$taskIndex++){$taskBytes[$taskIndex]=$taskFormat[$taskIndex+1]}
    [IO.File]::WriteAllBytes("$taskRun/ui-fixtures/$($taskFormat[0])",$taskBytes)
  }
  [IO.File]::WriteAllText("$taskRun/ui-fixtures/bad.z64",'Invalid ROM')
  [IO.File]::WriteAllText("$taskRun/ui-fixtures/ignored.txt",'Ignored text file')
  Push-Location $taskRun
  try { & ./ui-host-tests.exe; if($LASTEXITCODE){throw 'Frontend contract tests failed'} } finally {Pop-Location}
} finally {Pop-Location}
