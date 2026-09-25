/* test/sql_test.c — round-trip the schemagen SQLite bindings
 * (gen/domain/example_sql.c) through a real SQLite: Cosmopolitan's
 * third_party/sqlite3 when built with cosmocc, the host library natively. */

#include <stdio.h>
#include <string.h>

#include "example_sql.h"
#include "testlib.h"

TEST(example_sql, insert_then_select_round_trips) {
    sqlite3 *db = NULL;
    ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &db));
    ASSERT_EQ(SQLITE_OK, Example_create_table(db));

    Example in;
    Example_init(&in);
    in.id = 7;
    snprintf(in.name, sizeof(in.name), "%s", "seven");
    in.value = -3;
    in.enabled = 1;
    EXPECT_EQ(0, Example_insert(db, &in));

    Example out;
    Example_init(&out);
    EXPECT_EQ(0, Example_select_by_id(db, 7, &out));
    EXPECT_EQ(7, out.id);
    EXPECT_STREQ("seven", out.name);
    EXPECT_EQ(-3, out.value);
    EXPECT_EQ(1, out.enabled);

    EXPECT_EQ(-1, Example_select_by_id(db, 8, &out));
    EXPECT_EQ(SQLITE_OK, sqlite3_close(db));
}

TEST(example_sql, header_matches_linked_library) {
    EXPECT_STREQ(SQLITE_VERSION, sqlite3_libversion());
    printf("  sqlite %s\n", sqlite3_libversion());
}

TESTLIB_MAIN()
