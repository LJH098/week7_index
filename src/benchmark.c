#include "benchmark.h"

#include "bptree.h"
#include "parser.h"
#include "table_runtime.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define BENCHMARK_ROW_COUNT 1000000
#define BENCHMARK_LOOKUP_COUNT 1000

/*
 * 현재 시각을 밀리초 단위 실수로 반환한다.
 */
static double benchmark_now_ms(void) {
    struct timeval current;

    gettimeofday(&current, NULL);
    return (double)current.tv_sec * 1000.0 +
           (double)current.tv_usec / 1000.0;
}

/*
 * 벤치마크용 INSERT 문을 name 컬럼 하나 기준으로 준비한다.
 */
static void benchmark_prepare_insert(InsertStatement *stmt, const char *table_name,
                                     const char *name_value) {
    memset(stmt, 0, sizeof(*stmt));
    snprintf(stmt->table_name, sizeof(stmt->table_name), "%s", table_name);
    stmt->column_count = 1;
    snprintf(stmt->columns[0], sizeof(stmt->columns[0]), "name");
    snprintf(stmt->values[0], sizeof(stmt->values[0]), "%s", name_value);
}

/*
 * row_index를 id 인덱스에 반영하는 삽입 경로를 실행한다.
 */
static int benchmark_insert_with_index(TableRuntime *table, const char *table_name,
                                       int row_count) {
    InsertStatement stmt;
    char name_buffer[MAX_VALUE_LEN];
    char **row;
    int row_index;
    int i;

    for (i = 1; i <= row_count; i++) {
        if (benchmark_generate_row_value(name_buffer, sizeof(name_buffer), i) != SUCCESS) {
            return FAILURE;
        }

        benchmark_prepare_insert(&stmt, table_name, name_buffer);
        if (table_insert_row(table, &stmt, &row_index) != SUCCESS) {
            return FAILURE;
        }

        row = table_get_row_by_slot(table, row_index);
        if (row == NULL || !utils_is_integer(row[0])) {
            return FAILURE;
        }

        if (bptree_insert(&table->id_index_root, (int)utils_parse_integer(row[0]),
                          row_index) != SUCCESS) {
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * row_index만 유지하고 인덱스 등록 없이 삽입만 수행한다.
 */
static int benchmark_insert_without_index(TableRuntime *table, const char *table_name,
                                          int row_count) {
    InsertStatement stmt;
    char name_buffer[MAX_VALUE_LEN];
    int row_index;
    int i;

    for (i = 1; i <= row_count; i++) {
        if (benchmark_generate_row_value(name_buffer, sizeof(name_buffer), i) != SUCCESS) {
            return FAILURE;
        }

        benchmark_prepare_insert(&stmt, table_name, name_buffer);
        if (table_insert_row(table, &stmt, &row_index) != SUCCESS) {
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * B+ 트리 id 검색을 반복 실행한다.
 */
static int benchmark_measure_select_by_id(const TableRuntime *table, int row_count,
                                          int lookup_count, double *elapsed_ms) {
    double started_at;
    int id_value;
    int row_index;
    int i;

    if (table == NULL || elapsed_ms == NULL || lookup_count <= 0) {
        return FAILURE;
    }

    started_at = benchmark_now_ms();
    for (i = 0; i < lookup_count; i++) {
        id_value = (i % row_count) + 1;
        if (bptree_search(table->id_index_root, id_value, &row_index) != SUCCESS) {
            return FAILURE;
        }
    }
    *elapsed_ms = benchmark_now_ms() - started_at;
    return SUCCESS;
}

/*
 * 일반 필드 선형 탐색을 반복 실행한다.
 */
static int benchmark_measure_select_by_other_field(const TableRuntime *table,
                                                   int row_count,
                                                   int lookup_count,
                                                   double *elapsed_ms) {
    WhereClause where;
    char name_buffer[MAX_VALUE_LEN];
    int *row_indices;
    int match_count;
    double started_at;
    int i;

    if (table == NULL || elapsed_ms == NULL || lookup_count <= 0) {
        return FAILURE;
    }

    memset(&where, 0, sizeof(where));
    snprintf(where.column, sizeof(where.column), "name");
    snprintf(where.op, sizeof(where.op), "=");

    started_at = benchmark_now_ms();
    for (i = 0; i < lookup_count; i++) {
        if (benchmark_generate_row_value(name_buffer, sizeof(name_buffer),
                                         (i % row_count) + 1) != SUCCESS) {
            return FAILURE;
        }
        snprintf(where.value, sizeof(where.value), "%s", name_buffer);

        row_indices = NULL;
        match_count = 0;
        if (table_linear_scan_by_field(table, &where, &row_indices, &match_count) != SUCCESS) {
            return FAILURE;
        }
        free(row_indices);
        if (match_count != 1) {
            return FAILURE;
        }
    }
    *elapsed_ms = benchmark_now_ms() - started_at;
    return SUCCESS;
}

/*
 * 시간 결과를 총합/평균 형식으로 출력한다.
 */
static void benchmark_print_metric(const char *label, double total_ms,
                                   int operations) {
    double average_us;

    average_us = (total_ms * 1000.0) / (double)operations;
    printf("%s: total_ms=%.3f avg_us=%.3f ops=%d\n",
           label, total_ms, average_us, operations);
}

int benchmark_generate_row_value(char *buffer, size_t buffer_size,
                                 int row_number) {
    int written;

    if (buffer == NULL || buffer_size == 0 || row_number <= 0) {
        return FAILURE;
    }

    written = snprintf(buffer, buffer_size, "user_%07d", row_number);
    if (written < 0 || (size_t)written >= buffer_size) {
        return FAILURE;
    }

    return SUCCESS;
}

int benchmark_run(void) {
    TableRuntime without_index_table;
    TableRuntime with_index_table;
    double started_at;
    double insert_without_index_ms;
    double insert_with_index_ms;
    double select_by_id_ms;
    double select_by_other_field_ms;

    table_init(&without_index_table);
    table_init(&with_index_table);

    started_at = benchmark_now_ms();
    if (benchmark_insert_without_index(&without_index_table, "benchmark_without_index",
                                       BENCHMARK_ROW_COUNT) != SUCCESS) {
        table_free(&without_index_table);
        table_free(&with_index_table);
        return FAILURE;
    }
    insert_without_index_ms = benchmark_now_ms() - started_at;

    started_at = benchmark_now_ms();
    if (benchmark_insert_with_index(&with_index_table, "benchmark_with_index",
                                    BENCHMARK_ROW_COUNT) != SUCCESS) {
        table_free(&without_index_table);
        table_free(&with_index_table);
        return FAILURE;
    }
    insert_with_index_ms = benchmark_now_ms() - started_at;

    if (benchmark_measure_select_by_id(&with_index_table, BENCHMARK_ROW_COUNT,
                                       BENCHMARK_LOOKUP_COUNT,
                                       &select_by_id_ms) != SUCCESS ||
        benchmark_measure_select_by_other_field(&with_index_table,
                                                BENCHMARK_ROW_COUNT,
                                                BENCHMARK_LOOKUP_COUNT,
                                                &select_by_other_field_ms) != SUCCESS) {
        table_free(&without_index_table);
        table_free(&with_index_table);
        return FAILURE;
    }

    printf("benchmark_rows=%d benchmark_lookups=%d\n",
           BENCHMARK_ROW_COUNT, BENCHMARK_LOOKUP_COUNT);
    benchmark_print_metric("insert_without_index", insert_without_index_ms,
                           BENCHMARK_ROW_COUNT);
    benchmark_print_metric("insert_with_bptree_index", insert_with_index_ms,
                           BENCHMARK_ROW_COUNT);
    benchmark_print_metric("select_by_id_with_index", select_by_id_ms,
                           BENCHMARK_LOOKUP_COUNT);
    benchmark_print_metric("select_by_other_field_linear_scan",
                           select_by_other_field_ms, BENCHMARK_LOOKUP_COUNT);

    table_free(&without_index_table);
    table_free(&with_index_table);
    return SUCCESS;
}
