#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;

// subdivide_facet_uniform()/collect_leaves()/set_leaf_state() back OrcGraffiti's
// Image Paint per-color-patch detail: they let the pipeline subdivide a face's
// *paint resolution* (TriangleSelector's own virtual split tree) without ever
// touching the underlying TriangleMesh, so per-triangle painting isn't capped
// by the source mesh's original facet density. See docs/OrcGraffiti/AI_STATUS.md.

TEST_CASE("subdivide_facet_uniform splits a facet down to the edge-length limit", "[TriangleSelector]")
{
    TriangleMesh mesh = make_cube(20., 20., 20.);
    TriangleSelector selector(mesh);

    selector.subdivide_facet_uniform(0, 2.f);
    const auto leaves = selector.collect_leaves(0);

    REQUIRE(leaves.size() > 1);

    for (const auto& leaf : leaves) {
        const float e01 = (leaf.p1 - leaf.p0).norm();
        const float e12 = (leaf.p2 - leaf.p1).norm();
        const float e20 = (leaf.p0 - leaf.p2).norm();
        // split_triangle() only ever stops recursing into a triangle once all
        // three of its own sides are <= the limit, so this is a hard bound on
        // every leaf collect_leaves() returns, not an approximation.
        CHECK(e01 <= 2.01f);
        CHECK(e12 <= 2.01f);
        CHECK(e20 <= 2.01f);
    }
}

TEST_CASE("subdivide_facet_uniform leaves other facets of the mesh untouched", "[TriangleSelector]")
{
    TriangleMesh mesh = make_cube(20., 20., 20.);
    TriangleSelector selector(mesh);

    selector.subdivide_facet_uniform(0, 2.f);

    const auto leaves_other = selector.collect_leaves(1);
    REQUIRE(leaves_other.size() == 1);
    CHECK(leaves_other[0].leaf_index == 1);
}

TEST_CASE("collect_leaves visits facets in a stable, repeatable order", "[TriangleSelector]")
{
    TriangleMesh mesh = make_cube(20., 20., 20.);
    TriangleSelector a(mesh);
    TriangleSelector b(mesh);

    a.subdivide_facet_uniform(0, 2.f);
    b.subdivide_facet_uniform(0, 2.f);

    const auto leaves_a = a.collect_leaves(0);
    const auto leaves_b = b.collect_leaves(0);

    REQUIRE(leaves_a.size() == leaves_b.size());
    for (std::size_t i = 0; i < leaves_a.size(); ++i) {
        CHECK(leaves_a[i].p0 == leaves_b[i].p0);
        CHECK(leaves_a[i].p1 == leaves_b[i].p1);
        CHECK(leaves_a[i].p2 == leaves_b[i].p2);
    }
}

TEST_CASE("set_leaf_state assigns independent colors per leaf and round-trips through serialize", "[TriangleSelector]")
{
    TriangleMesh mesh = make_cube(20., 20., 20.);
    TriangleSelector selector(mesh);

    selector.subdivide_facet_uniform(0, 2.f);
    auto leaves = selector.collect_leaves(0);
    REQUIRE(leaves.size() >= 2);

    // Paint alternating leaves with two different filaments — this is the
    // "precisely-shaped color patch" capability the flat per-face painter
    // (TriangleSelector::set_facet) cannot express on its own.
    for (std::size_t i = 0; i < leaves.size(); ++i) {
        const auto state = (i % 2 == 0) ? EnforcerBlockerType::Extruder2 : EnforcerBlockerType::Extruder3;
        selector.set_leaf_state(leaves[i].leaf_index, state);
    }

    REQUIRE(selector.num_facets(EnforcerBlockerType::Extruder2) > 0);
    REQUIRE(selector.num_facets(EnforcerBlockerType::Extruder3) > 0);

    const auto data = selector.serialize();

    TriangleSelector reloaded(mesh);
    reloaded.deserialize(data, /*needs_reset=*/true, EnforcerBlockerType::ExtruderMax);

    CHECK(reloaded.num_facets(EnforcerBlockerType::Extruder2) == selector.num_facets(EnforcerBlockerType::Extruder2));
    CHECK(reloaded.num_facets(EnforcerBlockerType::Extruder3) == selector.num_facets(EnforcerBlockerType::Extruder3));
}

TEST_CASE("subdivide_facet_uniform with a large edge limit leaves the facet unsplit", "[TriangleSelector]")
{
    TriangleMesh mesh = make_cube(20., 20., 20.);
    TriangleSelector selector(mesh);

    selector.subdivide_facet_uniform(0, 1000.f);
    const auto leaves = selector.collect_leaves(0);

    REQUIRE(leaves.size() == 1);
    CHECK(leaves[0].leaf_index == 0);
}
