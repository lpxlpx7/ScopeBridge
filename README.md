# ScopeBridge

ScopeBridge converts a CRC profile and GeoJSON video maps into a EuroScope package using **Jurina's Renderer**. The Windows release folder contains `ScopeBridge.exe`, the Qt runtime, a precompiled 32-bit `JurinasRenderer.dll`, TopSky files, and the example EuroScope symbology preset.

The desktop interface uses [Qlementine](https://github.com/oclero/qlementine) v1.4.2 (MIT license) with a bundled dark theme and requests San Francisco UI fonts, falling back to Segoe UI when San Francisco is not installed. The complete portable archive includes `QLEMENTINE-LICENSE.txt`; the Qt GUI is statically linked with Qlementine and still dynamically links to Qt.

ScopeBridge source is licensed under [GNU GPL version 3](LICENSE). The portable archive includes the license text; bundled Qt and TopSky files retain their own licenses. The renderer source is maintained in the sibling `plugin/` workspace, with the ScopeBridge-specific changes provided as a [patch](renderer-world-polygons.patch).

## Use

1. Unzip the whole `ScopeBridge-Windows-x64.zip` archive and launch `ScopeBridge.exe` (keep `assets/`, the Qt DLLs and `platforms/` beside the executable).
2. Choose the CRC **Profiles directory** (usually `%LOCALAPPDATA%/CRC/Profiles`) and the `VideoMaps` folder (the folder containing `ZME`, `ZOA`, etc., or an individual ARTCC map folder). CRC `ARTCCs/<id>.json` must be installed locally or alongside the selected data.
3. Click **Scan profiles**. The scrollable list displays each valid JSON profile's `Name`, sorted alphabetically. Select one profile to preview its video maps and initial ON/OFF states. Choose a sector directory in the first card; the left panel also contains a scrollable `.sct` list, and the selected file is used as the base sector.
4. A selected **Base sector** preserves navigation, airports, runways and other non-GEO sections. If no sector is selected, the output only contains basic `[INFO]`; CRC profiles/video maps alone cannot reconstruct runway/navigation records.
5. Click **Generate EuroScope sector**, then open the output `.prf` in EuroScope. The selected list item determines which profile is converted; only one profile is generated per click.

**CRC:1 tab selection:** ScopeBridge exports the first visible CRC display window saved in the profile (the `CRC:1` page). Within that window, it uses the tab identified by `SelectedDisplayId`; if no saved tab matches, it falls back to the first usable tab. Other CRC windows and tabs are not combined into the export. To export a different tab, select and save it in the CRC profile before generating again.

The output folder contains:

- `.prf` loading Jurina's Renderer, TopSky, EuroScope symbology, voice and profile settings;
- `.asr` with the display view and active renderer layers;
- `.sct` (the source base sector without obsolete `[GEO]` sections, or minimal `[INFO]`);
- `.ese` generated from the ARTCC position/frequency records;
- `Settings/Voice.txt`, `Settings/Profile.txt`, `Settings/Symbology.txt`;
- `Plugins/JurinasRenderer/{JurinasRenderer.dll,ground.json,style.json}` and `Plugins/TopSky/`.

The renderer displays each available video map in its layer panel with a readable English name. The profile's selected STARS maps or enabled ERAM filters start visible; all other available maps start hidden and can be enabled from the panel. If a saved STARS number no longer resolves, all available maps start hidden rather than being activated automatically. GeoJSON line and polygon geometry (including filled polygons) is converted from longitude/latitude into the renderer's latitude/longitude format. The bundled renderer is built from the sibling `plugin/src/CANGroundRender.cpp` with world-layer polygon drawing enabled. `style.json` uses the source feature colors when provided and the ZME example's video color for uncolored features. CRC-only symbols and text cannot be recreated from these features. The TopSky visual theme comes from the ZME example; its Japan-specific airspace file is replaced with an empty placeholder so RJxx rules are not applied to another ARTCC. The symbology preset is likewise the ZME example's palette, not a CRC-specific export.

The renderer's pixel compositing path preserves the real radar background while changing layer opacity; the standalone opacity fix is provided in [`renderer-opacity.patch`](renderer-opacity.patch). The renderer panel includes a vertical scrollbar with up/down controls for long map lists.

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

The CMake build downloads Qlementine from its pinned v1.4.2 commit on GitHub. `QLEMENTINE_SANDBOX` and `QLEMENTINE_SHOWCASE` are disabled. Offline builders may set `-DFETCHCONTENT_SOURCE_DIR_QLEMENTINE=<local-source-directory>` to a local copy of that commit. Package Qlementine's MIT license alongside the portable app.

The renderer DLL must be built **as x86** using `plugin/build.bat` in a Visual Studio x86 Native Tools environment; the Qt GUI is x64. For a standalone checkout, apply [`renderer-world-polygons.patch`](renderer-world-polygons.patch) to the matching renderer workspace before building; it includes layer defaults and UTF-8 panel labels. Copy the resulting `plugin/bin/JurinasRenderer.dll` to `build/release/assets/JurinasRenderer.dll`, and the workspace's `ZME/Plugins/TopSky` and `ZME/Settings/Symbology.txt` to `build/release/assets/TopSky` and `build/release/assets/Symbology.txt`. Deploy Qt with `windeployqt` and copy `build/ScopeBridge.exe` into `build/release` before distributing that directory. The app reports a missing asset rather than silently creating a broken PRF.

For batch use: `ScopeBridge.exe --convert <profile.json> <output-directory> [VideoMaps-folder] [base-sector.sct]`.
