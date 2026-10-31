# LightGif 1.0.0 portable edition

Extract the ZIP and run LightGif.exe. No installation, account or network access is needed. The UI is currently Simplified Chinese.

1. Enter one phrase per text box. “+ 添加” adds a phrase (up to 12); the arrow moves it up and the cross removes it.
2. Choose an installed font (字体) and size (字号). “文字太长时自动适配” automatically fits longer text.
3. “停留时长” is the hold time; “动画变化时长” is the transition time. They are independent. Three phrases at 2 + 0.6 seconds make a 7.8-second loop.
4. Set dimensions, gradient, text and border colors in the right-hand panel. “透明背景” enables transparency.
5. “控制在 500 KB 以内” requires the output to be strictly smaller than 500,000 bytes. The app may reduce colors or animation frames but preserves dimensions, text and timing. It reports failure if the cap cannot be met.
6. “导出 GIF” exports. “保存方案” saves a .gifcfg preset; “打开方案” loads it; “恢复默认” resets controls.

The included default.gifcfg and example.gif use generic demonstration text. Fonts are not included; use fonts licensed for your output and supporting your language.

Tested on Windows 10 22H2 x64. The executable is x86; Windows 11 and a native 32-bit OS have not been tested locally. The executable is unsigned. The single LightGif.exe file is sufficient for normal use; Python and Node are not needed.

To update, close the app and extract a new release into another directory. Keep the old EXE and your presets for rollback. To uninstall, delete the program directory. There is no service or auto-updater.

Source, tests and releases: https://github.com/abel0325/lightgif
