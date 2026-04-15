#include "bptree.h"
#include "table_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 테스트 조건이 참인지 검사하고, 실패하면 사람이 읽기 쉬운 메시지를 출력하는 함수다.
 * 각 테스트 단계에서 공통으로 사용해 실패 지점을 빠르게 확인하게 해준다.
 */
static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

/*
 * id를 제외한 일반 INSERT 문 구조체를 빠르게 만드는 테스트 헬퍼 함수다.
 * 런타임 테이블의 auto-id 정책을 검증할 때 기본 입력으로 사용한다.
 */
static void prepare_insert_without_id(InsertStatement *stmt, const char *table_name,
                                      const char *name, const char *age) {
    memset(stmt, 0, sizeof(*stmt));
    snprintf(stmt->table_name, sizeof(stmt->table_name), "%s", table_name);
    stmt->column_count = 2;
    snprintf(stmt->columns[0], sizeof(stmt->columns[0]), "name");
    snprintf(stmt->columns[1], sizeof(stmt->columns[1]), "age");
    snprintf(stmt->values[0], sizeof(stmt->values[0]), "%s", name);
    snprintf(stmt->values[1], sizeof(stmt->values[1]), "%s", age);
}

/*
 * 사용자가 id를 직접 넣으려는 INSERT 문 구조체를 만드는 테스트 헬퍼 함수다.
 * 런타임 정책상 거부되어야 하는 경로를 명시적으로 검증할 때 사용한다.
 */
static void prepare_insert_with_id(InsertStatement *stmt, const char *table_name,
                                   const char *id, const char *name,
                                   const char *age) {
    memset(stmt, 0, sizeof(*stmt));
    snprintf(stmt->table_name, sizeof(stmt->table_name), "%s", table_name);
    stmt->column_count = 3;
    snprintf(stmt->columns[0], sizeof(stmt->columns[0]), "id");
    snprintf(stmt->columns[1], sizeof(stmt->columns[1]), "name");
    snprintf(stmt->columns[2], sizeof(stmt->columns[2]), "age");
    snprintf(stmt->values[0], sizeof(stmt->values[0]), "%s", id);
    snprintf(stmt->values[1], sizeof(stmt->values[1]), "%s", name);
    snprintf(stmt->values[2], sizeof(stmt->values[2]), "%s", age);
}

/*
 * 테스트 전체를 실행하면서 메모리 런타임 초기화, auto-id, 선형 탐색,
 * id 인덱스 등록, 삽입 실패 시 상태 유지, 활성 테이블 교체 정책이 기대대로 동작하는지 검증하는 함수다.
 */
int main(void) {
    TableRuntime *table;
    InsertStatement stmt;
    char **row;
    int row_index;
    int indexed_row_index;
    long long generated_id;
    int *matches;
    int match_count;

    table_runtime_cleanup();

    if (assert_true(table_get_or_load("runtime_users", &table) == SUCCESS,
                    "table_get_or_load should create a new active runtime table") != SUCCESS ||
        assert_true(strcmp(table->table_name, "runtime_users") == 0,
                    "runtime table name should match requested table") != SUCCESS ||
        assert_true(table->row_count == 0,
                    "new runtime table should start with zero rows") != SUCCESS ||
        assert_true(table->next_id == 1,
                    "new runtime table should start auto-id from 1") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_insert_without_id(&stmt, "runtime_users", "Alice", "30");
    if (assert_true(table_insert_row(table, &stmt, &row_index, &generated_id) == SUCCESS,
                    "table_insert_row should append first row") != SUCCESS ||
        assert_true(row_index == 0, "first inserted row index should be 0") != SUCCESS ||
        assert_true(generated_id == 1, "first generated id should be 1") != SUCCESS ||
        assert_true(table->col_count == 3,
                    "runtime schema should add id column automatically") != SUCCESS ||
        assert_true(table->id_index_root != NULL,
                    "runtime table should create id index root on first insert") != SUCCESS ||
        assert_true(strcmp(table->columns[0], "id") == 0,
                    "runtime first column should be id") != SUCCESS ||
        assert_true(strcmp(table->rows[0][0], "1") == 0,
                    "first row should store generated id string") != SUCCESS ||
        assert_true(bptree_search(table->id_index_root, generated_id,
                                  &indexed_row_index) == SUCCESS &&
                        indexed_row_index == 0,
                    "first generated id should be searchable through B+ tree index") != SUCCESS ||
        assert_true(strcmp(table->rows[0][1], "Alice") == 0,
                    "first row should store name value") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_insert_without_id(&stmt, "runtime_users", "Bob", "25");
    if (assert_true(table_insert_row(table, &stmt, &row_index, &generated_id) == SUCCESS,
                    "table_insert_row should append second row") != SUCCESS ||
        assert_true(row_index == 1, "second inserted row index should be 1") != SUCCESS ||
        assert_true(generated_id == 2, "second generated id should be 2") != SUCCESS ||
        assert_true(table->row_count == 2,
                    "runtime table should count inserted rows") != SUCCESS ||
        assert_true(bptree_search(table->id_index_root, generated_id,
                                  &indexed_row_index) == SUCCESS &&
                        indexed_row_index == 1,
                    "second generated id should map to second row in B+ tree index") != SUCCESS ||
        assert_true(table->next_id == 3,
                    "next_id should advance after insert") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    row = table_get_row_by_slot(table, 1);
    if (assert_true(row != NULL, "table_get_row_by_slot should return existing row") != SUCCESS ||
        assert_true(strcmp(row[0], "2") == 0,
                    "second row should expose generated id 2") != SUCCESS ||
        assert_true(strcmp(row[1], "Bob") == 0,
                    "second row should expose inserted name") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    matches = NULL;
    match_count = 0;
    if (assert_true(table_linear_scan_by_field(table, "name", "=", "Alice",
                                               &matches, &match_count) == SUCCESS,
                    "scan by name should succeed") != SUCCESS ||
        assert_true(match_count == 1,
                    "scan by name should find exactly one row") != SUCCESS ||
        assert_true(matches[0] == 0,
                    "Alice should remain at row index 0") != SUCCESS) {
        free(matches);
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }
    free(matches);

    matches = NULL;
    match_count = 0;
    if (assert_true(table_linear_scan_by_field(table, NULL, NULL, NULL,
                                               &matches, &match_count) == SUCCESS,
                    "scan without condition should return all rows") != SUCCESS ||
        assert_true(match_count == 2,
                    "scan without condition should include every row") != SUCCESS ||
        assert_true(matches[0] == 0 && matches[1] == 1,
                    "full scan should keep insertion order") != SUCCESS) {
        free(matches);
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }
    free(matches);

    prepare_insert_with_id(&stmt, "runtime_users", "99", "Mallory", "44");
    if (assert_true(table_insert_row(table, &stmt, &row_index, &generated_id) == FAILURE,
                    "runtime table should reject explicit id input") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    if (assert_true(bptree_insert(&table->id_index_root, table->next_id, 99) == SUCCESS,
                    "test should be able to inject conflicting next_id into B+ tree") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_insert_without_id(&stmt, "runtime_users", "Carol", "41");
    if (assert_true(table_insert_row(table, &stmt, &row_index, &generated_id) == FAILURE,
                    "runtime insert should fail when B+ tree rejects generated id") != SUCCESS ||
        assert_true(table->row_count == 2,
                    "failed index registration should not commit a new row") != SUCCESS ||
        assert_true(table->next_id == 3,
                    "failed index registration should keep next_id unchanged") != SUCCESS ||
        assert_true(table_get_row_by_slot(table, 2) == NULL,
                    "failed index registration should not expose a pending row slot") != SUCCESS ||
        assert_true(bptree_search(table->id_index_root, 3, &indexed_row_index) == SUCCESS &&
                        indexed_row_index == 99,
                    "failed insert should leave preexisting conflicting index entry unchanged") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    if (assert_true(table_get_or_load("runtime_users", &table) == SUCCESS,
                    "same table should keep active runtime available") != SUCCESS ||
        assert_true(table->row_count == 2,
                    "reloading same table should preserve active rows") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    if (assert_true(table_get_or_load("runtime_orders", &table) == SUCCESS,
                    "different table should replace active runtime") != SUCCESS ||
        assert_true(strcmp(table->table_name, "runtime_orders") == 0,
                    "active runtime should switch to new table name") != SUCCESS ||
        assert_true(table->row_count == 0,
                    "new active runtime should start empty after switch") != SUCCESS ||
        assert_true(table->col_count == 0,
                    "new active runtime should not have schema before insert") != SUCCESS ||
        assert_true(table->next_id == 1,
                    "new active runtime should reset auto-id sequence") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    table_runtime_cleanup();
    puts("[PASS] table_runtime");
    return EXIT_SUCCESS;
}
