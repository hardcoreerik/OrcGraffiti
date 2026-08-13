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
// Known v1 limitation: contour points along a mesh edge shared by two
// faces are computed independently by each face's own local grid, so
// adjacent faces are not guaranteed to agree exactly on where a color
// boundary crosses that shared edge. This can produce a non-watertight
// result where a boundary crosses a face edge. validate_baked_mesh()
// (MeshBake.hpp) is expected to catch this (its_num_open_edges) rather
// than let it silently commit — treat that as the safety net, not a
// repair strategy. Planar projection only for v1.
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
