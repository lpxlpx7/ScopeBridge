# ScopeBridge

A C++17 / Qt 6 desktop converter for local CRC profiles and video maps. It creates a standalone EuroScope `.sct`, `.asr`, and `.prf` for the selected profile. The interface and generated comments are in English.

## Build

Install Qt 6 Widgets, CMake 3.21+ and a C++17 compiler. This machine has Qt 6.11.2 for MinGW in `I:/Qt/6.11.2/mingw_64` and the matching compiler in `I:/Qt/Tools/mingw1310_64`. From this directory, build with PowerShell:

```powershell
$env:PATH="I:\Qt\Tools\mingw1310_64\bin;I:\Qt\6.11.2\mingw_64\bin;$env:PATH"
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="I:/Qt/6.11.2/mingw_64" -DCMAKE_C_COMPILER="I:/Qt/Tools/mingw1310_64/bin/gcc.exe" -DCMAKE_CXX_COMPILER="I:/Qt/Tools/mingw1310_64/bin/g++.exe" -DCMAKE_MAKE_PROGRAM="I:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe"
cmake --build build --config Release
```

Replace the Qt paths for another installation. To distribute the executable independently, run `windeployqt` on the built executable.

## Use

1. Launch **ScopeBridge**. By default it reads `%LOCALAPPDATA%/CRC/Profiles`, `%LOCALAPPDATA%/CRC/ARTCCs` and `%LOCALAPPDATA%/CRC/VideoMaps` (override any path in the UI).
2. Optionally choose a legacy EuroScope `.sct` as **Base sector**. Its navigation, airports, runways and other non-`[GEO]` sections are preserved; its old `[GEO]` blocks are replaced with the selected CRC maps. Without a base sector, the generated sector contains basic `[INFO]` and video geometry only.
3. Click **Scan profiles**, choose a profile and inspect its selected map names/IDs. Click **Generate package**. Open the generated `.prf` in EuroScope, then load its `.asr` if EuroScope does not open it automatically.

For command-line conversion (useful for automation), run `ScopeBridge.exe --convert <profile.json> <output-directory> [base.sct]`. This uses the default CRC `ARTCCs` and `VideoMaps` directories and does not open the GUI.

The converter creates one folder per profile under the chosen output directory. Source CRC files and the base sector are never modified. Each run replaces the generated `.sct`, `.asr` and `.prf` in that profile folder.

## Map selection and geometry

- STARS: the active visible window's `SelectedDisplayId` selects a display. `CurrentPrefSet.SelectedVideoMapIds` contains **1-based indices** into that display position's facility `starsConfiguration.videoMapIds` in `ARTCCs/<ArtccId>.json`.
- ERAM: `ActiveGeoMap` names a group under the ARTCC's `eramConfiguration.geoMaps`; all maps in the active group are included.
- Tower cab / ASDEX displays: `towerCabConfiguration.videoMapId` and `asdexConfiguration.videoMapId` are used when available.
- The app resolves every ID to `VideoMaps/<ArtccId>/<id>.geojson`, previews the names, and reports missing files or invalid indices. Only the currently selected display is converted; hidden tabs are not combined.
- GeoJSON `LineString`, `MultiLineString`, `Polygon`, `MultiPolygon` (including inner rings), and `GeometryCollection` lines become EuroScope `[GEO]` segments. GeoJSON coordinates are longitude/latitude; EuroScope uses latitude/longitude in DMS. Polygons are represented as outlines, and point features, fills, text, and CRC-specific line styling are not transferred. One shared `CRCMap` color is defined in the sector file.

Large ERAM groups can produce very large sector files. Conversion reads only the selected maps; supplying a compatible base sector yields the most complete EuroScope sector.
