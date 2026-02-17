build-cmake\Release\dtest.exe --pbn-source "test\pbn\DDS_Camrose24_1- BENCAM22 v WBridge5.pbn" --verify

build-cmake\Release\dtest.exe --random-deals 1000 --verify

build-cmake\Release\dtest.exe --pbn-source "https://github.com/BSalita/Calculate_PBN_Results/blob/master/DDS_Camrose24_1-%%20BENCAM22%%20v%%20WBridge5.pbn" --report-dir smoke_probe --html-report smoke_probe\report.html --verify

