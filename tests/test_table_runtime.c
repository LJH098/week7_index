#include "bptree.h"
#include "table_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

int main(void) {
    TableRuntime table;
    char columns[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    char values[MAX_COLUMNS][MAX_VALUE_LEN];
    WhereClause where;
    int *matching_slots;
    int match_count;
    int row_index;
    long long inserted_id;

    if (assert_true(table_init(&table, "runtime_users") == SUCCESS,
                    "table_init should initialize runtime table") != SUCCESS) {
        return EXIT_FAILURE;
    }

    snprintf(columns[0], sizeof(columns[0]), "name");
    snprintf(columns[1], sizeof(columns[1]), "age");
    snprintf(values[0], sizeof(values[0]), "Alice");
    snprintf(values[1], sizeof(values[1]), "30");
    if (assert_true(table_insert_row(&table, columns, values, 2, 1,
                                     &inserted_id) == SUCCESS,
                    "table_insert_row should insert first row") != SUCCESS ||
        assert_true(inserted_id == 1, "first inserted id should be 1") != SUCCESS) {
        table_free(&table);
        return EXIT_FAILURE;
    }

    snprintf(columns[0], sizeof(columns[0]), "age");
    snprintf(columns[1], sizeof(columns[1]), "name");
    snprintf(values[0], sizeof(values[0]), "25");
    snprintf(values[1], sizeof(values[1]), "Bob");
    if (assert_true(table_insert_row(&table, columns, values, 2, 1,
                                     &inserted_id) == SUCCESS,
                    "table_insert_row should support reordered columns") != SUCCESS ||
        assert_true(inserted_id == 2, "second inserted id should be 2") != SUCCESS) {
        table_free(&table);
        return EXIT_FAILURE;
    }

    if (assert_true(table.loaded, "runtime table should be marked loaded") != SUCCESS ||
        assert_true(table.row_count == 2, "runtime table should track row count") != SUCCESS ||
        assert_true(table.next_id == 3, "runtime table should advance next id") != SUCCESS ||
        assert_true(strcmp(table.rows[1][1], "Bob") == 0,
                    "reordered insert should map name correctly") != SUCCESS ||
        assert_true(strcmp(table.rows[1][2], "25") == 0,
                    "reordered insert should map age correctly") != SUCCESS) {
        table_free(&table);
        return EXIT_FAILURE;
    }

    if (assert_true(bptree_search(table.id_index_root, 2, &row_index) == SUCCESS,
                    "id index should find second row") != SUCCESS ||
        assert_true(row_index == 1, "id index should point to second slot") != SUCCESS) {
        table_free(&table);
        return EXIT_FAILURE;
    }

    memset(&where, 0, sizeof(where));
    snprintf(where.column, sizeof(where.column), "age");
    snprintf(where.op, sizeof(where.op), ">=");
    snprintf(where.value, sizeof(where.value), "30");
    matching_slots = NULL;
    match_count = 0;
    if (assert_true(table_linear_scan_by_field(&table, &where, &matching_slots,
                                               &match_count) == SUCCESS,
                    "table_linear_scan_by_field should scan rows") != SUCCESS ||
        assert_true(match_count == 1,
                    "linear scan should return one matching row") != SUCCESS ||
        assert_true(matching_slots[0] == 0,
                    "linear scan should match first row") != SUCCESS) {
        free(matching_slots);
        table_free(&table);
        return EXIT_FAILURE;
    }

    free(matching_slots);
    table_free(&table);
    puts("[PASS] table_runtime");
    return EXIT_SUCCESS;
}
