# 像素守城 Pixel Keep

一个 Tikpal 应用（Tikpal Voice 的扩展固件），用 Tikpal 开发包做的：像素风休闲守城，果冻怪从四面围过来。

- 手指拖动：城里的弓箭手朝手指方向射。
- 摇一摇，或短按侧键：放炸弹。
- 双击侧键：系统的应用环（回 Tikpal、换应用）；按住侧键：「回到 Pip 说？」。两者都会让游戏暂停。

```bash
./kit build              # 编译，再跑主机测试
./kit install --slot 2   # USB 写进测试圆屏的位置 2，写完直接打开
./kit logs               # 看日志；./kit snap 截图，./kit heap 看内存
./kit build --release    # 提交用的那份
```

- `devkit/` 是开发包（git 子模块），钉在一个版本上，别改它。`git clone` 这个仓库以后，`./kit` 会自己把它拉下来。
- 应用清单是 `app.json`。游戏核心用 C++ 编写，`./kit test` 可以在电脑上运行。
- 做完、自测过，照 `devkit/AGENTS.md` 第 4 步 `./kit submit` 打提交包，到开发者平台 developer.tikpal.ai 这个应用的「版本」页上传。

## 实现要点

- 游戏核心在 `main/game/`，`main/pk_core.*` 是给 `main.c` 用的 C 接口。`sh sim/build.sh` 输出中文场景截图，`sh sim/build.sh en` 输出英文截图。
- 画面：155×155 调色板帧，放大 3 倍成 466×466 大端 RGB565（PSRAM），用 `board_direct_blit()` 直接送屏，游戏任务在核 1。
- 系统界面（应用环、「回到 Pip 说？」、提示箭头、下拉面板、正在回到）一出现，游戏就暂停并把屏交还 LVGL；LVGL 底下垫着暂停那一帧。
- 声音：一个常驻 synth 声部（24 kHz、5 ms 块），开局时 `yk_speak_hold_amp(true)`。
- 只在一局进行中 `yk_app_keep_screen_on(true)`；最好成绩存在 NVS `yk_pixelkeep/best_ms`。
- 游戏跟随设备语言，调色板取自 Tikpal Voice 的圆屏主题。
