@rem If dds_oob.dll / dds3_oob.dll are missing next to dtest.exe, build them first:
@rem   powershell -ExecutionPolicy Bypass -File tools\build_oob_dlls.ps1

build-cmake\Release\dtest.exe --pbn-source "test\pbn\DDS_Camrose24_1- BENCAM22 v WBridge5.pbn" --verify

build-cmake\Release\dtest.exe --random-deals 1000 --verify

build-cmake\Release\dtest.exe --pbn-source "https://github.com/BSalita/Calculate_PBN_Results/blob/master/DDS_Camrose24_1-%%20BENCAM22%%20v%%20WBridge5.pbn" --report-dir smoke_probe --html-report smoke_probe\report.html --verify

