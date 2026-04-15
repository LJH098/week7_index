#include "benchmark.h"

#include "bptree.h"
#include "table_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BENCHMARK_DEFAULT_ROWS 1000000
#define BENCHMARK_LOOKUP_COUNT 100

/*
 * clock() 차이를 초 단위 실수로 바꾼다.
 */
static double benchmark_elapsed_seconds(clock_t start, clock_t end) {
    return (double)(end - start) / (double)CLOCKS_PER_SEC;
}

/*
 * 같은 입력 집합을 테이블에 반복 삽입한다.
 */
static int benchmark_insert_rows(TableRuntime *table, int row_count, int use_index) {
    char columns[1][MAX_IDENTIFIER_LEN];
    char values[1][MAX_VALUE_LEN];
    char *generated_value;
    int index;

    if (utils_safe_strcpy(columns[0], sizeof(columns[0]), "name") != SUCCESS) {
        return FAILURE;
    }

    for (index = 0; index < row_count; index++) {
        generated_value = benchmark_generate_row_value(index);
        if (generated_value == NULL) {
            return FAILURE;
        }

        if (utils_safe_strcpy(values[0], sizeof(values[0]), generated_value) != SUCCESS) {
            free(generated_value);
            fprintf(stderr, "Error: Benchmark value is too long.\n");
            return FAILURE;
        }
        free(generated_value);

        if (table_insert_row(table, columns, values, 1, use_index, NULL) != SUCCESS) {
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * 균등 간격 샘플로 id 검색을 반복 실행한다.
 */
static int benchmark_run_id_searches(TableRuntime *table, int row_count, int iterations) {
    int row_index;
    int lookup_index;
    long long lookup_id;

    for (lookup_index = 0; lookup_index < iterations; lookup_index++) {
        lookup_id = (long long)((lookup_index * row_count) / iterations) + 1;
        if (bptree_search(table->id_index_root, lookup_id, &row_index) != SUCCESS ||
            row_index < 0) {
            fprintf(stderr, "Error: Benchmark id lookup failed for %lld.\n", lookup_id);
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * 같은 샘플 집합을 name 선형 탐색으로 반복 검색한다.
 */
static int benchmark_run_linear_scans(TableRuntime *table, int row_count, int iterations) {
    WhereClause where;
    char *generated_value;
    int *matching_slots;
    int match_count;
    int lookup_index;
    int sample_index;

    memset(&where, 0, sizeof(where));
    if (utils_safe_strcpy(where.column, sizeof(where.column), "name") != SUCCESS ||
        utils_safe_strcpy(where.op, sizeof(where.op), "=") != SUCCESS) {
        return FAILURE;
    }

    for (lookup_index = 0; lookup_index < iterations; lookup_index++) {
        sample_index = (lookup_index * row_count) / iterations;
        generated_value = benchmark_generate_row_value(sample_index);
        if (generated_value == NULL) {
            return FAILURE;
        }

        if (utils_safe_strcpy(where.value, sizeof(where.value), generated_value) != SUCCESS) {
            free(generated_value);
            return FAILURE;
        }
        free(generated_value);

        if (table_linear_scan_by_field(table, &where, &matching_slots,
                                       &match_count) != SUCCESS) {
            return FAILURE;
        }

        if (match_count != 1) {
            free(matching_slots);
            fprintf(stderr, "Error: Benchmark linear scan returned %d rows.\n", match_count);
            return FAILURE;
        }

        free(matching_slots);
    }

    return SUCCESS;
}

/*
 * 벤치마크용 name 값을 하나 생성해 새 문자열로 반환한다.
 */
char *benchmark_generate_row_value(int index) {
    char buffer[MAX_VALUE_LEN];

    if (snprintf(buffer, sizeof(buffer), "user_%07d", index + 1) < 0) {
        return NULL;
    }

    return utils_strdup(buffer);
}

/*
 * 메모리 테이블과 B+ 트리 기준 벤치마크를 실행한다.
 */
int benchmark_run(int row_count) {
    TableRuntime plain_table;
    TableRuntime indexed_table;
    clock_t start;
    clock_t end;
    double insert_without_index;
    double insert_with_index;
    double select_by_id;
    double select_by_scan;
    int lookup_count;
    int status;

    if (row_count <= 0) {
        row_count = BENCHMARK_DEFAULT_ROWS;
    }

    lookup_count = row_count < BENCHMARK_LOOKUP_COUNT ? row_count : BENCHMARK_LOOKUP_COUNT;
    if (lookup_count <= 0) {
        lookup_count = 1;
    }

    if (table_init(&plain_table, "benchmark_plain") != SUCCESS ||
        table_init(&indexed_table, "benchmark_indexed") != SUCCESS) {
        return FAILURE;
    }

    start = clock();
    status = benchmark_insert_rows(&plain_table, row_count, 0);
    end = clock();
    if (status != SUCCESS) {
        table_free(&plain_table);
        table_free(&indexed_table);
        return FAILURE;
    }
    insert_without_index = benchmark_elapsed_seconds(start, end);

    start = clock();
    status = benchmark_insert_rows(&indexed_table, row_count, 1);
    end = clock();
    if (status != SUCCESS) {
        table_free(&plain_table);
        table_free(&indexed_table);
        return FAILURE;
    }
    insert_with_index = benchmark_elapsed_seconds(start, end);

    start = clock();
    status = benchmark_run_id_searches(&indexed_table, row_count, lookup_count);
    end = clock();
    if (status != SUCCESS) {
        table_free(&plain_table);
        table_free(&indexed_table);
        return FAILURE;
    }
    select_by_id = benchmark_elapsed_seconds(start, end);

    start = clock();
    status = benchmark_run_linear_scans(&indexed_table, row_count, lookup_count);
    end = clock();
    if (status != SUCCESS) {
        table_free(&plain_table);
        table_free(&indexed_table);
        return FAILURE;
    }
    select_by_scan = benchmark_elapsed_seconds(start, end);

    printf("benchmark_rows=%d\n", row_count);
    printf("insert_without_index=%.6f sec\n", insert_without_index);
    printf("insert_with_bptree_index=%.6f sec\n", insert_with_index);
    printf("select_by_id_with_index=%.6f sec (%d lookups)\n",
           select_by_id, lookup_count);
    printf("select_by_other_field_linear_scan=%.6f sec (%d lookups)\n",
           select_by_scan, lookup_count);
    printf("avg_select_by_id_with_index=%.9f sec\n",
           select_by_id / (double)lookup_count);
    printf("avg_select_by_other_field_linear_scan=%.9f sec\n",
           select_by_scan / (double)lookup_count);

    table_free(&plain_table);
    table_free(&indexed_table);
    return SUCCESS;
}
