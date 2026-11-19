// Row Geometry tests

#include "list/common.h"
#include "ui/list/list_geometry.h"

// Row Geometry (list_geometry.h): pure functions from a row height, a gap and a row count to where rows are. The
// cases use rows 24 px tall with a 2 px gap at scale 1, so row N starts at N * 26.

#define GEO_EPS 0.001

static ListRowGeometry geo_rows(u32 count) {
    return list_row_geometry(24.0f, 2.0f, 1.0f, count);
}

UTEST(list_geometry, unset_height_and_gap_take_the_defaults) {
    const ListRowGeometry rows = list_row_geometry(0.0f, 0.0f, 1.0f, 3);
    ASSERT_NEAR(32.0, (double)rows.row_height, GEO_EPS);
    ASSERT_NEAR(8.0, (double)rows.gap, GEO_EPS);
    ASSERT_NEAR(32.0, (double)rows.row_px, GEO_EPS);
    ASSERT_NEAR(8.0, (double)rows.gap_px, GEO_EPS);
    ASSERT_EQ(3u, rows.count);
}

UTEST(list_geometry, scale_multiplies_the_row_and_snaps_the_gap) {
    const ListRowGeometry rows = list_row_geometry(21.0f, 3.0f, 1.5f, 1);
    ASSERT_NEAR(21.0, (double)rows.row_height, GEO_EPS);
    ASSERT_NEAR(3.0, (double)rows.gap, GEO_EPS);
    ASSERT_NEAR(31.5, (double)rows.row_px, GEO_EPS);
    ASSERT_NEAR(5.0, (double)rows.gap_px, GEO_EPS);
}

UTEST(list_geometry, a_scale_of_zero_counts_as_one) {
    const ListRowGeometry rows = list_row_geometry(24.0f, 2.0f, 0.0f, 1);
    ASSERT_NEAR(24.0, (double)rows.row_px, GEO_EPS);
    ASSERT_NEAR(2.0, (double)rows.gap_px, GEO_EPS);
    ASSERT_TRUE(list_geometry_rows_have_extent(rows));
}

UTEST(list_geometry, content_height_counts_a_gap_for_every_row) {
    ASSERT_NEAR(260.0, list_geometry_content_height(geo_rows(10)), GEO_EPS);
    ASSERT_NEAR(0.0, list_geometry_content_height(geo_rows(0)), GEO_EPS);
}

UTEST(list_geometry, span_height_has_gaps_only_between_rows) {
    ASSERT_NEAR(128.0, (double)list_geometry_span_height(geo_rows(10), 5), GEO_EPS);
    ASSERT_NEAR(24.0, (double)list_geometry_span_height(geo_rows(10), 1), GEO_EPS);
    ASSERT_NEAR(0.0, (double)list_geometry_span_height(geo_rows(10), 0), GEO_EPS);
}

UTEST(list_geometry, row_top_is_exact_for_billions_of_rows) {
    ASSERT_NEAR(78.0, list_geometry_row_top(geo_rows(10), 3), GEO_EPS);
    ASSERT_TRUE(list_geometry_row_top(geo_rows(UINT32_MAX), 4000000000u) == 104000000000.0);
}

UTEST(list_geometry, visible_rows_cover_the_viewport) {
    const ScrollRange top = list_geometry_visible_rows(geo_rows(100), 0.0, 52.0f, 0);
    ASSERT_EQ(0u, top.start);
    ASSERT_TRUE(top.end >= 2u && top.end <= 3u);

    const ScrollRange scrolled = list_geometry_visible_rows(geo_rows(100), 260.0, 52.0f, 0);
    ASSERT_EQ(10u, scrolled.start);

    const ScrollRange padded = list_geometry_visible_rows(geo_rows(100), 260.0, 52.0f, 2);
    ASSERT_EQ(8u, padded.start);
    ASSERT_EQ(scrolled.end + 2u, padded.end);
}

UTEST(list_geometry, a_band_covers_the_rows_it_touches) {
    const ListRowSpan first = list_geometry_rows_in_band(geo_rows(10), 0.0, 10.0);
    ASSERT_EQ(0, (int)first.first);
    ASSERT_EQ(0, (int)first.last);

    const ListRowSpan three = list_geometry_rows_in_band(geo_rows(10), 10.0, 60.0);
    ASSERT_EQ(0, (int)three.first);
    ASSERT_EQ(2, (int)three.last);

    const ListRowSpan lower = list_geometry_rows_in_band(geo_rows(10), 30.0, 60.0);
    ASSERT_EQ(1, (int)lower.first);
    ASSERT_EQ(2, (int)lower.last);
}

UTEST(list_geometry, a_band_inside_one_gap_covers_no_row) {
    const ListRowSpan span = list_geometry_rows_in_band(geo_rows(10), 25.0, 25.5);
    ASSERT_TRUE(span.first > span.last);
}

UTEST(list_geometry, a_band_is_clamped_to_the_rows_that_exist) {
    const ListRowSpan all = list_geometry_rows_in_band(geo_rows(10), -500.0, 1.0e9);
    ASSERT_EQ(0, (int)all.first);
    ASSERT_EQ(9, (int)all.last);

    const ListRowSpan above = list_geometry_rows_in_band(geo_rows(10), -50.0, -10.0);
    ASSERT_TRUE(above.first > above.last);

    const ListRowSpan below = list_geometry_rows_in_band(geo_rows(10), 400.0, 500.0);
    ASSERT_TRUE(below.first > below.last);

    const ListRowSpan none = list_geometry_rows_in_band(geo_rows(0), 0.0, 100.0);
    ASSERT_TRUE(none.first > none.last);
}

UTEST(list_geometry, a_drop_lands_before_or_after_the_row_under_it) {
    ASSERT_EQ(0u, list_geometry_insertion_index(geo_rows(10), 5.0));
    ASSERT_EQ(1u, list_geometry_insertion_index(geo_rows(10), 20.0));
    ASSERT_EQ(1u, list_geometry_insertion_index(geo_rows(10), 30.0));
    ASSERT_EQ(2u, list_geometry_insertion_index(geo_rows(10), 45.0));
}

UTEST(list_geometry, a_drop_outside_the_rows_lands_at_an_end) {
    ASSERT_EQ(0u, list_geometry_insertion_index(geo_rows(10), -40.0));
    ASSERT_EQ(10u, list_geometry_insertion_index(geo_rows(10), 1.0e9));
    ASSERT_EQ(0u, list_geometry_insertion_index(geo_rows(0), 50.0));
}

UTEST(list_geometry, the_insertion_line_sits_in_the_middle_of_the_gap_above) {
    ASSERT_NEAR(-1.0, list_geometry_insertion_y(geo_rows(10), 0), GEO_EPS);
    ASSERT_NEAR(51.0, list_geometry_insertion_y(geo_rows(10), 2), GEO_EPS);
    ASSERT_NEAR(259.0, list_geometry_insertion_y(geo_rows(10), 10), GEO_EPS);
}
