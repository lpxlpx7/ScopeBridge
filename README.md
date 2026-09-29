# ScopeBridge

ScopeBridge turns a CRC profile and its GeoJSON video maps into EuroScope sector files. The Windows release includes `ScopeBridge.exe` and the Qt runtime files it needs.

## Use the Windows app

1. Open `ScopeBridge.exe` in the **release folder**. Keep its accompanying DLLs and `platforms` folder beside the exe.
2. Browse to one CRC profile `.json` (usually under `%LOCALAPPDATA%\CRC\Profiles`).
3. Choose the CRC `VideoMaps` folder (or its ARTCC subfolder, such as `VideoMaps\ZME`).
4. Click **Generate EuroScope sector**. The output folder opens automatically. Open the generated `.prf` in EuroScope.

Packages are saved in `~/ScopeBridge-Output/<ARTCC>_<profile>/` by default. You can change the output folder in the app. The converter automatically finds the facility definition under `CRC/ARTCCs` alongside the profile or VideoMaps folders, or in the local CRC installation. The original CRC files are not modified. **Preview maps** is optional.

The output contains a `.sct` with the selected video-map outlines in `[GEO]`, an `.asr` with the CRC display's view and a `.prf` that references both. CRC profiles and video maps alone do **not** contain all navigation, runway, positions, settings and plugin data in the full ZME example package. Thus the output is a functional map sector package, not a byte-for-byte reproduction of the ZME example. Point symbols, text, fills and CRC-specific styles are not converted; polygon boundaries are drawn as lines.

The converter supports any ARTCC with local CRC facility and VideoMaps data. Some ASDEX displays, including SMF in ZOA, use the facility's SAID video map. If a saved STARS map number is stale or no maps are selected, ScopeBridge includes all maps offered by that facility and reports the fallback in the app; a facility without STARS maps can fall back to its tower-cab map. These fallbacks may create a large sector file and do not represent the profile's exact original map selection.

## Build from source

Requires CMake 3.21+, Qt 6 Widgets and a matching C++17 compiler. For Qt 6.11.2 and MinGW installed under `I:\Qt`, run from this directory in PowerShell:

```powershell
$env:PATH="I:\Qt\Tools\mingw1310_64\bin;I:\Qt\6.11.2\mingw_64\bin;$env:PATH"
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="I:/Qt/6.11.2/mingw_64" -DCMAKE_C_COMPILER="I:/Qt/Tools/mingw1310_64/bin/gcc.exe" -DCMAKE_CXX_COMPILER="I:/Qt/Tools/mingw1310_64/bin/g++.exe" -DCMAKE_MAKE_PROGRAM="I:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe"
cmake --build build --config Release
& "I:\Qt\6.11.2\mingw_64\bin\windeployqt.exe" --release --compiler-runtime --dir "build\release" "build\ScopeBridge.exe"
```

Copy `ScopeBridge.exe` into `build/release` before distributing that folder (or use the prebuilt release folder). Qt is dynamically linked, so copying only the exe does not create a standalone app.

For batch use: `ScopeBridge.exe --convert <profile.json> <output-directory> [VideoMaps-folder] [base-sector.sct]`. If a base `.sct` is supplied, its non-`[GEO]` sections are preserved and its `[GEO]` maps are replaced. The GUI requires only the profile and VideoMaps locations.
