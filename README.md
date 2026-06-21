# Logger_Adapter - Quill Windows Adapter

A Windows-focused adapter layer for the Quill logging library (v10.0.1) that fills platform-specific gaps.

## Dependencies

This project requires **vcpkg** to manage external dependencies:

```powershell
vcpkg install quill:x64-windows zlib:x64-windows
```

## Build

Open `Logger_Adapter.vcxproj` in Visual Studio 2022 and build, or use MSBuild:

```powershell
msbuild Logger_Adapter.vcxproj /p:Configuration=Debug /p:Platform=x64
```

## Features Implemented

| Feature | Description |
|---------|-------------|
| **AA-M02** Daily Rotation | `DailyRotatingFileSink` with gzip compression, retention by days |
| **AA-M18** Log Sanitization | `SanitizingSink` with Aho-Corasick multi-pattern matcher |
| **AA-M07** Error Notifier | `error_notifier` callback wired to `quill::Backend` |
| **AA-C04** Dynamic LogLevel | `SetLogLevel`/`GetLogLevel` APIs for runtime level changes |