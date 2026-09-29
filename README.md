# ScopeBridge

ScopeBridge converts a CRC profile and GeoJSON video maps into a EuroScope package using **Jurina's Renderer**. The Windows release folder contains `ScopeBridge.exe`, the Qt runtime, a precompiled 32-bit `JurinasRenderer.dll`, TopSky files, and the example EuroScope symbology preset.

ScopeBridge source is licensed under [GNU GPL version 3](LICENSE). The portable archive includes the license text; bundled Qt and TopSky files retain their own licenses. The renderer source is maintained in the sibling `plugin/` workspace, with the ScopeBridge-specific changes provided as a [patch](renderer-world-polygons.patch).

## Use

1. Unzip the whole `ScopeBridge-Windows-x64.zip` archive and launch `ScopeBridge.exe` (keep `assets/`, the Qt DLLs and `platforms/` beside the executable).
2. Choose a CRC profile `.json` and its `VideoMaps` folder (the folder containing `ZME`, `ZOA`, etc., or an individual ARTCC map folder). CRC `ARTCCs/<id>.json` must be installed locally or alongside the selected data.
3. Optionally choose a **Base sector** `.sct` to preserve navigation, airports, runways and other non-GEO sections. If omitted, the sector only contains basic `[INFO]`; CRC profiles/video maps alone cannot reconstruct runway/navigation records.
4. Click **Preview maps** to see which are initially **ON** and which available maps start **OFF**. Click **Generate EuroScope sector**, then open the output `.prf` in EuroScope.

The output folder contains:

- `.prf` loading Jurina's Renderer, TopSky, EuroScope symbology, voice and profile settings;
- `.asr` with the display view and active renderer layers;
- `.sct` (the source base sector without obsolete `[GEO]` sections, or minimal `[INFO]`);
- `.ese` generated from the ARTCC position/frequency records;
- `Settings/Voice.txt`, `Settings/Profile.txt`, `Settings/Symbology.txt`;
- `Plugins/JurinasRenderer/{JurinasRenderer.dll,ground.json,style.json}` and `Plugins/TopSky/`.

The renderer displays each available video map in its layer panel with a readable English name. The profile's selected STARS maps or enabled ERAM filters start visible; all other available maps start hidden and can be enabled from the panel. If a saved STARS number no longer resolves, all available maps start hidden rather than being activated automatically. GeoJSON line and polygon geometry (including filled polygons) is converted from longitude/latitude into the renderer's latitude/longitude format. The bundled renderer is built from the sibling `plugin/src/CANGroundRender.cpp` with world-layer polygon drawing enabled. `style.json` uses the source feature colors when provided and the ZME example's video color for uncolored features. CRC-only symbols and text cannot be recreated from these features. The TopSky visual theme comes from the ZME example; its Japan-specific airspace file is replaced with an empty placeholder so RJxx rules are not applied to another ARTCC. The symbology preset is likewise the ZME example's palette, not a CRC-specific export.

## ARTCC compatibility

The facility is selected from the profile's `ArtccId`, not hard-coded to ZME. ScopeBridge finds the corresponding `ARTCCs/<ArtccId>.json`, resolves a STARS `SelectedVideoMapIds` entry against the display facility's `starsConfiguration.videoMapIds` (or its parent facility), maps ERAM `ActiveGeoMap` and `MapFilters` through the facility's geo-map list and feature filters, and supports tower-cab, ASDEX and SAID displays. It looks for GeoJSON files in `VideoMaps/<ArtccId>/` or in a directly selected ARTCC map folder. Tested with profiles from ZDC, ZLA, ZME, ZNY, ZOA and ZSU. If CRC has changed its facility data since a profile was saved, out-of-range map numbers are reported; unselected maps remain available in the renderer panel but start hidden.

Generated `.ese` positions and frequencies come from CRC facility data; position coordinates use the selected profile view center because CRC positions do not supply per-position coordinates. CRC profile/map data cannot reproduce the full original ZME package's external NAV/TopSky/other-plugin setup. Source files are never changed.

## Build

Build the Qt 6 / C++17 app using the MinGW toolchain compatible with your Qt installation. The project includes only its own source; package assets are copied from the surrounding Jurinas Renderer workspace. On the current development machine:

```powershell
$env:PATH="I:\Qt\Tools\mingw1310_64\bin;I:\Qt\6.11.2\mingw_64\bin;$env:PATH"
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="I:/Qt/6.11.2/mingw_64" -DCMAKE_CXX_COMPILER="I:/Qt/Tools/mingw1310_64/bin/g++.exe" -DCMAKE_MAKE_PROGRAM="I:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe"
cmake --build build
```

The renderer DLL must be built **as x86** using `plugin/build.bat` in a Visual Studio x86 Native Tools environment; the Qt GUI is x64. For a standalone checkout, apply [`renderer-world-polygons.patch`](renderer-world-polygons.patch) to the matching renderer workspace before building; it includes layer defaults and UTF-8 panel labels. Copy the resulting `plugin/bin/JurinasRenderer.dll` to `build/release/assets/JurinasRenderer.dll`, and the workspace's `ZME/Plugins/TopSky` and `ZME/Settings/Symbology.txt` to `build/release/assets/TopSky` and `build/release/assets/Symbology.txt`. Deploy Qt with `windeployqt` and copy `build/ScopeBridge.exe` into `build/release` before distributing that directory. The app reports a missing asset rather than silently creating a broken PRF.

For batch use: `ScopeBridge.exe --convert <profile.json> <output-directory> [VideoMaps-folder] [base-sector.sct]`.
