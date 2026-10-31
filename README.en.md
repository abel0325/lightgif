# LightGif

**A tiny, offline Windows app for animated text-carousel GIFs.** Native C++, portable, no account or runtime installation required.

[Download](https://github.com/abel0325/lightgif/releases/latest) · [中文](README.md)

![Example animation](assets/demo.gif)

- Rotate 1–12 text phrases with independent hold and transition durations.
- Customize installed fonts, size, gradient, border, dimensions and transparency.
- Fit long phrases to a shared font size; save reusable `.gifcfg` presets.
- Optional strict **under 500,000 bytes** export: reduce palette/frame count while preserving dimensions, text and total timing; fail explicitly if the target cannot be met.
- Native Win32/GDI+ application, statically linked MSVC runtime. No networking, telemetry, Python or Node needed to use the app.

## Use

Download the ZIP from Releases, extract it, and run `LightGif.exe`. The UI is currently Simplified Chinese. The primary action is **导出 GIF** (Export GIF); **保存方案** saves a preset and **打开方案** opens one. Hold and transition fields control pause and movement separately.

Tested on Windows 10 22H2 x64. The executable is x86 and targets Windows 10/11; Windows 11 and a native 32-bit OS have not been manually tested. Use installed fonts supporting your text. No font files are distributed. Release binaries are currently unsigned.

## Build and test

Install Visual Studio 2022 Build Tools with Desktop development with C++ and a Windows SDK. In PowerShell 5.1 or 7:

```powershell
./build.ps1
# build/LightGif.exe

# Optional tests: Python 3.12+, Pillow, NumPy; full layout tests need Microsoft YaHei.
python -m venv .venv
./.venv/Scripts/python.exe -m pip install -r tests/requirements.txt
./test.ps1 -Python ./.venv/Scripts/python.exe
```

Build tools are discovered using `vswhere`; no developer-specific paths are required. Tests independently decode GIF pixels, transparency, disposal, timing and size. Hidden-window integration renders only the app itself, never other windows. CI builds the app and runs font-independent encoder tests. See [test scope](docs/TESTING.md).

## Updates, contributions, license

Close the app and extract a new version into a separate folder. Keep the previous executable and your presets for rollback. There is no installer, service or auto-updater.

Bug reports with a minimal public example are welcome. Please exclude private or customer content. This project grew from a practical text-animation workflow and was developed with AI coding assistance. [MIT](LICENSE); [dependency and asset notes](THIRD_PARTY.md).
