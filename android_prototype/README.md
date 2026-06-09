Android 原生原型说明

目标：在 Android 上捕获播放音频（AudioPlaybackCapture / MediaProjection），对音频做短时 FFT，并把不同频段能量映射到手机振动（Vibrator）。

主要文件：
- app/src/main/java/com/example/audihaptic/MainActivity.kt
- app/src/main/AndroidManifest.xml

如何运行：
1. 在 Android Studio 中打开 `android_prototype` 目录作为项目。
2. 在设备（建议 Android 10+）上运行，授予屏幕捕获权限。该示例使用 `AudioPlaybackCapture` + `MediaProjection`。

注意：这是示例原型，真实产品建议将 FFT 迁移到 NDK 并优化性能。

SDL2 集成（用于手柄 / 马达振动）

步骤：
1. 下载 SDL2 源代码（例如 SDL2-2.26.x）：https://www.libsdl.org/download-2.0.php
2. 将 SDL2 源解压到 `android_prototype/SDL2`（该文件夹应包含 `CMakeLists.txt`、`include/`、`src/` 等）。
3. 在 Android Studio 的 CMake 配置中为该 module 传入 `-DSDL2_DIR=${projectDir}/SDL2`（或者在 `build.gradle` 的 externalNativeBuild.cmake.arguments 中添加该变量）。
4. 构建时，`app/src/main/cpp/CMakeLists.txt` 会把 SDL2 当作子目录添加并链接到 `audihaptic_native`。

示例 Gradle snippet（在 module 的 `build.gradle` 的 `android.defaultConfig.externalNativeBuild.cmake` 区块中添加）：

externalNativeBuild {
	cmake {
		arguments "-DSDL2_DIR=${projectDir}/SDL2"
	}
}

注意：SDL2 的 Android 构建需要正确的 Android NDK 和一些平台调整；若不想集成源码，可以使用预构建的 SDL2 Android AAR 或静态库，并在 `CMakeLists.txt` 中使用 `find_library()` 指定库路径，然后链接。

后端融合说明：
- Android 原型现在直接复用根目录的 `AudioProcessor` 后端逻辑。你可以在 `AudioProcessor.h` / `AudioProcessor.cpp` 中切换分析模式：
  - `AnalysisMode::TimeDomain`：保留原来的低通 / 高通滤波器模式
  - `AnalysisMode::FrequencyDomain`：使用 FFT 计算频带能量
- `android_prototype/app/src/main/cpp/native-lib.cpp` 已改为调用 `AudioProcessor::ProcessAudio(...)`，并把结果返回给 Java 层。
- Android UI 保持原生 Java，SDL2 仅用于桌面前端（Windows / Linux）和手柄振动支持。

跨平台架构建议：
- Windows / Linux：SDL2 作为 GUI 前端，调用共享后端 `AudioProcessor` 和（在 Windows 上）现有 `AudioCaptureManager` / `HapticController`。
- Android：本地 Java UI + `AudioPlaybackCapture`，后端复用 `AudioProcessor`。如果需要，还可以让 SDL2 仅处理 Android gamepad haptics，而 UI 仍保持原生。
