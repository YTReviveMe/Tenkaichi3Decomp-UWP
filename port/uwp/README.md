# Tenkaichi 3 Decomp for Xbox Dev Mode

`bt3_uwp_game` builds the decompiled game for x64 UWP. The package includes SDL3, DXIL shaders, menu assets, fonts, and the executable's data and state maps. Game files are supplied by the player.

The upstream 0.1.37 test package is `build/uwp-playable/AppPackages/bt3_uwp_game/BT3-Xbox-DevMode-Upstream-0.1.37-0.0.37.0.zip`. Version 0.0.27.0 remains the stable fallback while testers check this update on Xbox. Each ZIP contains the MSIX, signing certificate, and x64 VCLibs dependency.

## Game data

On first launch, choose **Internal** or **External**, browse to an unmodified USA ISO (SLUS-21678), and select **Install and Play**. The installer verifies the disc and extracts it into `BT3`. Put an internal ISO in `D:\DevelopmentFiles\BT3`, or browse to one on a removable drive. The ISO can be removed after extraction.

To add Spanish, German, French and Italian during setup, select **European ISO (optional)** and choose an ISO of the European release (SLES-54945) before installing. The USA disc is still required for the game. For an existing installation, open the in-game settings at **UI > Add European languages on next launch**, close the app, and reopen it to select the European ISO. The language archives are extracted beside the installed game data, whether it is internal or external. A language selector appears on the next start; the language can also be changed under **UI > Language**.

Existing PC game data can also be copied from `gamedata` into `BT3`. The extracted files should include:

```text
<Drive>:\BT3\disc\SLUS_216.78
<Drive>:\BT3\disc\BIN\DBZP.BIN
<Drive>:\BT3\pzs3us0\...
<Drive>:\BT3\pzs3us1\...
<Drive>:\BT3\pzs3us2\...
```

Internal data lives at `D:\DevelopmentFiles\BT3`. External data lives at the removable drive's `BT3` folder. Saves, settings, and `port.log` stay under `D:\DevelopmentFiles\BT3`. The selected external path is saved in the app's LocalFolder as `external-content.txt`. If the drive is unavailable, startup checks internal content and then opens setup. Windows UWP tests use the app LocalFolder for internal data. Early startup messages are written to `boot.log` there.

## Textures and online play

Put `.dds` or `.png` replacement textures in `D:\DevelopmentFiles\BT3\textures`. When this folder exists, the app uses it even if the game data is on an external drive. Otherwise it uses `<Drive>:\BT3\textures` with external content. Subfolders are supported. To switch back to external textures, rename or remove the internal `textures` folder. `boot.log` records the selected texture folder.

When a texture pack is present, slow gameplay windows are recorded in `boot.log` as `Texture perf` lines with frame, decoding, and submission times.

Dragon Net Battle and Tenkaichi Budokai Online use the online lobby. Direct addresses, room codes, and quick match use the desktop port's network protocol. Upstream 0.1.37 uses a new online protocol; both players need 0.1.37-based builds. Quick match depends on the configured matchmaking service and may reject unofficial builds. For a custom service, put its base URL in `D:\DevelopmentFiles\BT3\matchmaking-url.txt`. `BT3_MATCH_URL` takes priority when set in the app environment. Xbox to Xbox play was confirmed by a tester with a two-frame setting on an earlier build.

## Build

Build in an x64 Store Visual Studio environment with Python, `libclang==18.1.1`, `clang`, `clang-cl`, `llc`, and `llvm-nm` on `PATH`. Regenerate Direct3D shaders after changes under `port/src/gs/shaders` with `port/tools/dxil_shaders.py`; it needs `glslangValidator`, `spirv-cross`, and `dxc`. The patched SDL3 source and build trees are configured with `BT3_SDL3_UWP_SOURCE` and `BT3_SDL3_UWP_BUILD`; see `sdl3-gpu.patch`.

1. Run `build_game_objects.py` and `build_port_objects.py` from the repository root. Configure `port/uwp` with the Visual Studio 2022 WindowsStore x64 generator and `BT3_GAME=ON`.
2. If the first link reports missing game-library entries, run `generate_link_stubs.py` on its log, rerun `build_port_objects.py`, reconfigure, and link again.
3. After the final link, run `make_uwp_dat.py` and `make_uwp_state.py`, then rebuild the package. Regenerate both files after any relink that changes addresses.
4. Run `finalize_package.ps1 -PackagePath <msix> -GameExePath <Release exe> -StateMapPath <.exe.mem> -CertificateThumbprint <thumbprint>` to package the final executable and sign the MSIX. The certificate subject must match the manifest publisher.

This checkout uses `build/SDL3-uwp` and `build/sdl3-uwp-gpu`. The SDL fork is pinned to `df16f8be71bdc68ade12a90b993c8f02232a76de` and patched with `sdl3-gpu.patch`.
