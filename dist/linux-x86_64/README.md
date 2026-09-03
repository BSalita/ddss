# Linux `libdds.so` (x86_64)

Portable Release build (`DDS_THREADING=STL`, no `-march=native`) for
`python:3.12-slim` / the postmortem container.

## Produce the artifact

On Windows (wslc):

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_linux_so.ps1
```

On Linux:

```bash
bash tools/build_linux_so.sh
python3 tools/smoke_libdds.py dist/linux-x86_64/libdds.so
```

## Copy into the postmortem image

`src/ops/deploy_postmortem.ps1` stages this file and `Dockerfile.postmortem`
installs it as `/usr/local/lib/libdds.so`.

mlBridge `dds_ddss.py` loads that path on Linux via `ctypes.CDLL` (cdecl).
Call `SetMaxThreads(0)` after load (already done). `CalcAllTablesPBNx` is
not thread-safe — hold a process-wide lock if FastAPI or Streamlit can call
it from more than one thread.
