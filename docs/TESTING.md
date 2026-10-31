# Verification for 1.0.0

Local verification date: 2026-10-31 (Asia/Shanghai). These results describe the tested environment, not every Windows configuration.

## Environment

- Windows 10 Pro 22H2, build 19045, x64; x86 executable.
- Visual Studio 2022 MSVC 14.44.35207; Windows SDK 10.0.26100.0; `/MT`.
- Python 3.12, Pillow 12.3.0, NumPy 2.3.5 for independent decoding.
- Microsoft YaHei available for the Chinese layout tests.

## Results

| Check | Result |
|---|---|
| Build from the reorganized source | Passed |
| PowerShell compatibility | Build and complete tests passed in Windows PowerShell 5.1; build and complete tests also passed in PowerShell 7 |
| Runtime imports | Only Windows system DLLs; no separate MSVC runtime DLL, Python or Node dependency |
| Encoder fixtures | 47 GIFs decoded independently; pixel equality or bounded quantization error, frame merging, delays, transparency, disposal and looping checked |
| Malformed encoder input | All 6 cases rejected |
| Renderer | Default/long/missing-font layouts, hold/transition/loop behavior, single phrase and opaque background passed |
| Integration encoding | Four palette sizes; 48 frames, exactly 7,800 ms and exact alpha mask for each |
| App export and independent decode | All 6 configurations below passed |
| Preset roundtrip | Chinese text, quotes, equals sign, timing, font, color and disabled size-limit setting passed |
| Hidden-window widgets | Font/text/size controls, independent hold/transition values, add/remove/reorder, pause/restart passed |
| Visual review | App-rendered interface and palette comparison image inspected; no desktop capture used |

| Actual app export | Bytes | Frames | Total duration |
|---|---:|---:|---:|
| Default | 419,918 | 48 | 7,800 ms |
| Longer hold | 419,918 | 48 | 13,800 ms |
| Slower transition | 491,339 | 57 | 9,600 ms |
| Longer text / auto-fit | 342,548 | 48 | 7,800 ms |
| Single phrase | 21,254 | 1 | 2,600 ms |
| Opaque custom colors | 413,580 | 48 | 7,800 ms |

The slow-transition case exercises fallback from the initial animation settings. Every listed limited export is strictly less than 500,000 bytes. These cases do not exhaust all combinations or prove that every requested animation can fit the cap. The exporter reports an error when all allowed compression attempts remain too large.

## Reproduce

Run `test.ps1 -Python <python-executable>` from the project root. It builds x86 binaries, generates fresh fixtures, decodes the outputs using Pillow, and runs the application's built-in tests. Outputs stay under `build/test-results/` and are not checked in. Python can be in a separate environment; the EXE itself does not use it.

The app's `--ui-snapshot` mode renders its own hidden window and exercises internal widget/model callbacks. It is not a manual test of OS file dialogs, screen-reader behavior, every DPI setting, or live desktop interaction. Windows 11 and a native 32-bit Windows installation have not been tested locally. CI separately builds on Windows Server 2022 and checks the font-independent encoder; that is not a Windows 11 desktop certification.
