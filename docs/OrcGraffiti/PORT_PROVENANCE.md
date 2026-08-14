# OrcGraffiti — Port Provenance

## Bambu Studio Reference

Repository: https://github.com/bambulab/BambuStudio
Reference commit: f43cbe4296b16cb6089a47af8fe82d2b79d2442f
License: GNU Affero General Public License v3.0

## Reference Files (Inspected, Not Copied)

The following files were studied during architecture research. No code has been copied yet.

| File | Purpose | Status |
|---|---|---|
| src/libslic3r/TexturePainting.hpp | Overall texture painting structure | Inspected, not copied |
| src/libslic3r/TexturePainting.cpp | Texture pipeline implementation | Inspected, not copied |
| src/libslic3r/TextureToColor/TextureToColor.hpp | Color pipeline | Inspected, not copied |
| src/libslic3r/TextureToColor/TextureToColor.cpp | Color pipeline implementation | Inspected, not copied |
| src/libslic3r/TextureToColor/ColorUtils.hpp | CIEDE2000 and color utilities | Inspected, not copied |
| src/libslic3r/TextureToColor/ColorUtils.cpp | Color utility implementation | Inspected, not copied |
| src/slic3r/GUI/TextureImportDialog.hpp | Texture import UI | Inspected, not copied |
| src/slic3r/GUI/TextureImportDialog.cpp | Texture import UI implementation | Inspected, not copied |

## Adapted Code

None yet. The following will be adapted with attribution when implemented:

- Seven-point barycentric Gaussian sampling constants (algorithm concept)
- CIEDE2000 implementation (utility, will be adapted with full attribution)
- Deterministic color quantization approach (algorithm concept)

## Required Header for Any Adapted Code

```cpp
// Portions adapted from Bambu Studio.
// Source: https://github.com/bambulab/BambuStudio
// Reference commit: f43cbe4296b16cb6089a47af8fe82d2b79d2442f
// Original path: src/libslic3r/TextureToColor/ColorUtils.cpp
// License: GNU Affero General Public License v3.0
```
