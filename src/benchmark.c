#include "benchmark.h"

#include "bptree.h"
#include "table_runtime.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define BENCHMARK_ROW_COUNT 100000
#define BENCHMARK_SELECT_REPEAT 1000
#define BENCHMARK_NO_INDEX_COL_COUNT 2

/*
 * 현재 시각을 밀리초 단위 실수로 반환하는 함수다.
 * 삽입/검색 구간 시간을 단순하게 측정하기 위해 gettimeofday 기반으로 구현한다.
 */
static double benchmark_now_ms(void) {
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0) {
        return 0.0;
    }

    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

/*
 * 인덱스 없는 벤치마크용 행 하나의 셀 문자열들을 모두 해제하는 함수다.
 * row는 `id`, `name` 두 칸으로 할당된 메모리라는 전제에서만 사용한다.
 */
static void benchmark_free_row(char **row) {
    int i;

    if (row == NULL) {
        return;
    }

    for (i = 0; i < BENCHMARK_NO_INDEX_COL_COUNT; i++) {
        free(row[i]);
        row[i] = NULL;
    }

    free(row);
}

/*
 * 인덱스 없는 벤치마크용 행 배열 전체를 해제하는 함수다.
 * 삽입 비교가 끝난 뒤 메모리만 깨끗하게 회수하도록 별도 경로를 둔다.
 */
static void benchmark_free_rows(char ***rows, int row_count) {
    int i;

    if (rows == NULL) {
        return;
    }

    for (i = 0; i < row_count; i++) {
        benchmark_free_row(rows[i]);
        rows[i] = NULL;
    }

    free(rows);
}

/*
 * 인덱스 없는 삽입 비교용 행 포인터 배열의 용량을 확보하는 함수다.
 * 부족하면 2배씩 확장해 append 비용을 단순한 동적 배열 모델로 유지한다.
 */
static int benchmark_reserve_rows(char ****rows, int *capacity, int row_count) {
    int new_capacity;
    char ***new_rows;

    if (rows == NULL || capacity == NULL) {
        return FAILURE;
    }

    if (row_count < *capacity) {
        return SUCCESS;
    }

    new_capacity = *capacity == 0 ? INITIAL_ROW_CAPACITY : *capacity * 2;
    new_rows = (char ***)realloc(*rows, (size_t)new_capacity * sizeof(char **));
    if (new_rows == NULL) {
        fprintf(stderr, "Error: Failed to allocate benchmark rows.\n");
        return FAILURE;
    }

    *rows = new_rows;
    *capacity = new_capacity;
    return SUCCESS;
}

/*
 * 인덱스 없는 삽입 비교용 행 하나를 `id`, `name` 구조로 만드는 함수다.
 * 메모리 런타임과 비슷한 행 복제 비용을 맞추기 위해 문자열 복제를 동일하게 수행한다.
 */
static char **benchmark_build_row(long long id_value, const char *name_value) {
    char **row;
    char id_buffer[MAX_VALUE_LEN];
    int written;

    row = (char **)calloc((size_t)BENCHMARK_NO_INDEX_COL_COUNT, sizeof(char *));
    if (row == NULL) {
        fprintf(stderr, "Error: Failed to allocate benchmark row.\n");
        return NULL;
    }

    written = snprintf(id_buffer, sizeof(id_buffer), "%lld", id_value);
    if (written < 0 || written >= (int)sizeof(id_buffer)) {
        benchmark_free_row(row);
        return NULL;
    }

    row[0] = utils_strdup(id_buffer);
    if (row[0] == NULL) {
        benchmark_free_row(row);
        return NULL;
    }

    row[1] = utils_strdup(name_value);
    if (row[1] == NULL) {
        benchmark_free_row(row);
        return NULL;
    }

    return row;
}

/*
 * 인덱스 없는 삽입 비교용 동적 배열 끝에 행 하나를 append하는 함수다.
 * append만 수행하고 인덱스는 전혀 갱신하지 않아 기준 삽입 비용을 측정하게 한다.
 */
static int benchmark_append_row_without_index(char ****rows, int *row_count,
                                              int *capacity, long long id_value,
                                              const char *name_value) {
    char **row;

    if (rows == NULL || row_count == NULL || capacity == NULL || name_value == NULL) {
        return FAILURE;
    }

    if (benchmark_reserve_rows(rows, capacity, *row_count) != SUCCESS) {
        return FAILURE;
    }

    row = benchmark_build_row(id_value, name_value);
    if (row == NULL) {
        return FAILURE;
    }

    (*rows)[*row_count] = row;
    (*row_count)++;
    return SUCCESS;
}

/*
 * 메모리 런타임 삽입 비교에 사용할 INSERT 문 구조체를 준비하는 함수다.
 * 런타임 스키마는 `name` 한 컬럼만 사용해 자료구조 차이에 집중하게 한다.
 */
static int benchmark_prepare_insert_statement(InsertStatement *stmt,
                                              const char *table_name,
                                              const char *name_value) {
    if (stmt == NULL || table_name == NULL || name_value == NULL) {
        return FAILURE;
    }

    memset(stmt, 0, sizeof(*stmt));
    if (utils_safe_strcpy(stmt->table_name, sizeof(stmt->table_name), table_name) != SUCCESS ||
        utils_safe_strcpy(stmt->columns[0], sizeof(stmt->columns[0]), "name") != SUCCESS ||
        utils_safe_strcpy(stmt->values[0], sizeof(stmt->values[0]), name_value) != SUCCESS) {
        return FAILURE;
    }

    stmt->column_count = 1;
    return SUCCESS;
}

/*
 * 메모리 런타임용 benchmark 테이블을 초기 상태로 준비하는 함수다.
 * 전역 활성 테이블을 쓰지 않고 로컬 TableRuntime만 사용해 benchmark를 독립적으로 수행한다.
 */
static int benchmark_prepare_runtime_table(TableRuntime *table, const char *table_name) {
    if (table == NULL || table_name == NULL) {
        return FAILURE;
    }

    table_init(table);
    if (utils_safe_strcpy(table->table_name, sizeof(table->table_name), table_name) != SUCCESS) {
        return FAILURE;
    }

    table->loaded = 1;
    return SUCCESS;
}

/*
 * 벤치마크용 일반 필드 값을 행 번호 기반 문자열로 생성하는 함수다.
 * 현재 구현은 `user_000001` 형태를 사용해 선형 탐색 목표 값을 쉽게 재현한다.
 */
int benchmark_generate_row_value(char *buffer, size_t buffer_size, int row_number) {
    int written;

    if (buffer == NULL || buffer_size == 0 || row_number <= 0) {
        return FAILURE;
    }

    written = snprintf(buffer, buffer_size, "user_%06d", row_number);
    if (written < 0 || (size_t)written >= buffer_size) {
        return FAILURE;
    }

    return SUCCESS;
}

/*
 * 메모리 기준 삽입/검색 성능 비교를 한 번에 실행하는 함수다.
 * 인덱스 없는 삽입, B+ 트리 포함 삽입, id 검색, 일반 필드 선형 탐색 시간을 차례대로 측정한다.
 */
int benchmark_run(void) {
    char ***rows_without_index;
    int row_count_without_index;
    int capacity_without_index;
    TableRuntime indexed_table;
    InsertStatement stmt;
    char value_buffer[MAX_VALUE_LEN];
    double insert_without_index_ms;
    double insert_with_index_ms;
    double select_by_id_ms;
    double select_by_scan_ms;
    double start_ms;
    double end_ms;
    int i;
    int row_index;
    int *matches;
    int match_count;
    int status;

    rows_without_index = NULL;
    row_count_without_index = 0;
    capacity_without_index = 0;
    matches = NULL;
    status = FAILURE;

    if (benchmark_prepare_runtime_table(&indexed_table, "benchmark_users") != SUCCESS) {
        return FAILURE;
    }

    start_ms = benchmark_now_ms();
    for (i = 1; i <= BENCHMARK_ROW_COUNT; i++) {
        if (benchmark_generate_row_value(value_buffer, sizeof(value_buffer), i) != SUCCESS ||
            benchmark_append_row_without_index(&rows_without_index, &row_count_without_index,
                                               &capacity_without_index, i,
                                               value_buffer) != SUCCESS) {
            goto cleanup;
        }
    }
    end_ms = benchmark_now_ms();
    insert_without_index_ms = end_ms - start_ms;

    start_ms = benchmark_now_ms();
    for (i = 1; i <= BENCHMARK_ROW_COUNT; i++) {
        if (benchmark_generate_row_value(value_buffer, sizeof(value_buffer), i) != SUCCESS ||
            benchmark_prepare_insert_statement(&stmt, "benchmark_users",
                                              value_buffer) != SUCCESS ||
            table_insert_row(&indexed_table, &stmt, NULL, NULL) != SUCCESS) {
            goto cleanup;
        }
    }
    end_ms = benchmark_now_ms();
    insert_with_index_ms = end_ms - start_ms;

    start_ms = benchmark_now_ms();
    for (i = 0; i < BENCHMARK_SELECT_REPEAT; i++) {
        long long target_id = (long long)(i % BENCHMARK_ROW_COUNT) + 1;

        if (bptree_search(indexed_table.id_index_root, target_id, &row_index) != SUCCESS ||
            table_get_row_by_slot(&indexed_table, row_index) == NULL) {
            goto cleanup;
        }
    }
    end_ms = benchmark_now_ms();
    select_by_id_ms = end_ms - start_ms;

    start_ms = benchmark_now_ms();
    for (i = 0; i < BENCHMARK_SELECT_REPEAT; i++) {
        int target_row_number = (i % BENCHMARK_ROW_COUNT) + 1;

        if (benchmark_generate_row_value(value_buffer, sizeof(value_buffer),
                                         target_row_number) != SUCCESS) {
            goto cleanup;
        }

        matches = NULL;
        match_count = 0;
        if (table_linear_scan_by_field(&indexed_table, "name", "=",
                                       value_buffer, &matches, &match_count) != SUCCESS) {
            goto cleanup;
        }

        if (match_count != 1) {
            free(matches);
            goto cleanup;
        }

        free(matches);
        matches = NULL;
    }
    end_ms = benchmark_now_ms();
    select_by_scan_ms = end_ms - start_ms;

    printf("=== Memory Benchmark ===\n");
    printf("rows: %d\n", BENCHMARK_ROW_COUNT);
    printf("select_repeats: %d\n", BENCHMARK_SELECT_REPEAT);
    printf("insert_without_index: %.3f ms\n", insert_without_index_ms);
    printf("insert_with_bptree_index: %.3f ms\n", insert_with_index_ms);
    printf("select_by_id_with_index: %.3f ms\n", select_by_id_ms);
    printf("select_by_other_field_linear_scan: %.3f ms\n", select_by_scan_ms);

    status = SUCCESS;

cleanup:
    free(matches);
    benchmark_free_rows(rows_without_index, row_count_without_index);
    table_free(&indexed_table);
    return status;
}
