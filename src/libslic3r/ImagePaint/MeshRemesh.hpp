#pragma once

#include "ImagePaintTypes.hpp"
#include "ImagePaintErrors.hpp"
#include "ImagePaintCompat.hpp"
#include "Projection.hpp"
#include "ColorQuantizer.hpp"
#include "MeshBake.hpp"

#include <functional>
#include <vector>

namespace Slic3r::ImagePaint {

// Mesh Graffiti-style remeshing: real geometry whose triangle edges follow
// the image's actual color-region boundaries, instead of a uniform grid
// (contrast with bake_candidate_mesh()/Option 1 in
// docs/OrcGraffiti/MeshGraffiti_Bake_Plan.md section 4).
//
// Self-contained: samples the whole mesh to pick candidate (front-facing,
// covered) faces and quantize colors — same as run_image_paint() — then,
// per candidate face, samples a local NxN grid in barycentric space,
// classifies each sample against those clusters, and runs marching squares
// (per cluster) to extract contour rings within that local grid. The rings
// become constraint edges fed into a constrained Delaunay triangulation
// (Slic3r::Triangulation, CGAL-backed — the same tool the Emboss/SVG tools
// already use to turn 2D shapes into mesh geometry). Resulting triangles
// are classified by centroid and lifted back to 3D via barycentric
// interpolation of the original face's vertices — so, like
// bake_candidate_mesh(), every new vertex stays exactly on the original
// surface.
//
// Shared-edge crossings are computed once, not per face. Each unique mesh
// edge is sampled in 3D (keyed by its two original vertex indices) and
// every color-boundary crossing is recorded as a single Steiner vertex.
// Both faces incident to that edge reuse that same vertex (same index, same
// 3D position) so a color boundary that crosses a triangle edge cannot
// produce a T-junction. Neighbors that are not themselves paint candidates
// but share a crossed edge are remeshed just enough to include those
// Steiner points (no extra vertices on their other edges). On-edge
// marching-squares points are snapped to the cached crossings / corners
// rather than kept as independently interpolated positions.
//
// Planar projection only for v1.
Expected<BakedMesh, ImagePaintError>
remesh_by_color_boundary(const std::vector<Vec3f>&       vertices,
                         const std::vector<Vec3i32>&     indices,
                         const DecodedImage&              image,
                         const PlanarProjectionSettings&  projection,
                         const std::vector<FilamentColor>& filaments,
                         const QuantizationSettings&      quantization,
                         int                                grid_resolution = 12,
                         const std::function<bool()>&      cancel = {});

} // namespace Slic3r::ImagePaint
