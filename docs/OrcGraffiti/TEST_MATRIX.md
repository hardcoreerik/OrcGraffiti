# OrcGraffiti — Test Matrix

## Unit Tests

| Test | File | Command | Status | Requirement |
|---|---|---|---|---|
| topology_equal | test_image_paint_fingerprint.cpp | ctest -R image_paint_fingerprint | Not implemented | INV-006, Phase 1 |
| topology_vertex_change | test_image_paint_fingerprint.cpp | ctest -R image_paint_fingerprint | Not implemented | Phase 1 |
| topology_connectivity_change | test_image_paint_fingerprint.cpp | ctest -R image_paint_fingerprint | Not implemented | Phase 1 |
| topology_triangle_reorder | test_image_paint_fingerprint.cpp | ctest -R image_paint_fingerprint | Not implemented | Phase 1 |
| topology_invalid_index | test_image_paint_fingerprint.cpp | ctest -R image_paint_fingerprint | Not implemented | Phase 1 |
| decoder_png_rgba | test_image_paint_decoder.cpp | ctest -R image_paint_decoder | Not implemented | FR-IMG-001 |
| decoder_jpeg_opaque | test_image_paint_decoder.cpp | ctest -R image_paint_decoder | Not implemented | FR-IMG-001 |
| decoder_bmp | test_image_paint_decoder.cpp | ctest -R image_paint_decoder | Not implemented | FR-IMG-001 |
| decoder_alpha_preserved | test_image_paint_decoder.cpp | ctest -R image_paint_decoder | Not implemented | FR-IMG-002 |
| decoder_corrupt_rejected | test_image_paint_decoder.cpp | ctest -R image_paint_decoder | Not implemented | FR-IMG-003 |
| decoder_too_large_rejected | test_image_paint_decoder.cpp | ctest -R image_paint_decoder | Not implemented | FR-IMG-004 |
| projection_known_point | test_image_paint_projection.cpp | ctest -R image_paint_projection | Not implemented | FR-PROJ-006 |
| projection_rotation_90 | test_image_paint_projection.cpp | ctest -R image_paint_projection | Not implemented | FR-PROJ-003 |
| projection_mirror_u | test_image_paint_projection.cpp | ctest -R image_paint_projection | Not implemented | FR-PROJ-003 |
| projection_back_face_rejected | test_image_paint_projection.cpp | ctest -R image_paint_projection | Not implemented | FR-PROJ-004 |
| sampling_bilinear | test_image_paint_sampling.cpp | ctest -R image_paint_sampling | Not implemented | FR-SAMP-002 |
| sampling_gaussian7_weights | test_image_paint_sampling.cpp | ctest -R image_paint_sampling | Not implemented | FR-SAMP-002 |
| sampling_transparent_preserves | test_image_paint_sampling.cpp | ctest -R image_paint_sampling | Not implemented | FR-SAMP-003 |
| sampling_deterministic | test_image_paint_sampling.cpp | ctest -R image_paint_sampling | Not implemented | FR-SAMP-005 |
| color_srgb_to_linear | test_image_paint_color.cpp | ctest -R image_paint_color | Not implemented | Phase 3 |
| color_linear_to_lab | test_image_paint_color.cpp | ctest -R image_paint_color | Not implemented | Phase 3 |
| color_ciede2000_reference_pairs | test_image_paint_color.cpp | ctest -R image_paint_color | Not implemented | FR-COL-002 |
| quantizer_deterministic | test_image_paint_color.cpp | ctest -R image_paint_color | Not implemented | FR-SAMP-005 |
| regions_tiny_island_merge | test_image_paint_regions.cpp | ctest -R image_paint_regions | Not implemented | FR-CLN-003 |
| regions_sharp_edge_preserved | test_image_paint_regions.cpp | ctest -R image_paint_regions | Not implemented | FR-CLN-004 |

## Integration Tests

| Test | File | Command | Status | Requirement |
|---|---|---|---|---|
| headless_cube_projection | test_image_paint_pipeline.cpp | ctest -R image_paint_pipeline | Not implemented | Phase 2 exit gate |
| mmu_apply_known_states | test_image_paint_apply.cpp | ctest -R image_paint_apply | Not implemented | FR-APP-002 |
| mmu_undo_restores | test_image_paint_apply.cpp | ctest -R image_paint_apply | Not implemented | FR-APP-007 |
| 3mf_roundtrip_states | test_image_paint_3mf_roundtrip.cpp | ctest -R image_paint_3mf | Not implemented | FR-PER-001 |
| 3mf_settings_unchanged | test_image_paint_3mf_roundtrip.cpp | ctest -R image_paint_3mf | Not implemented | FR-PER-002 |

## Manual Tests

| Test | Platform | Status | Notes |
|---|---|---|---|
| Launch Orca with Image Paint gizmo | Windows | Not yet | Phase 5 |
| Select model volume | Windows | Not yet | Phase 5 |
| Load PNG and preview | Windows | Not yet | Phase 5 |
| Apply and undo | Windows | Not yet | Phase 5 |
| Save 3MF and reopen | Windows | Not yet | Phase 5 |
| Launch Orca with Image Paint gizmo | macOS | Not yet | Phase 5 |
| Launch Orca with Image Paint gizmo | Linux | Not yet | Phase 5 |

## Run Commands

```bash
# All image paint tests
ctest --test-dir tests -R image_paint --output-on-failure

# Full test suite
ctest --test-dir tests -C Release --output-on-failure

# Windows build
build_release_vs.bat tests
```
