#include "benchmark.h"

#include "bptree.h"
#include "table_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char ***rows;
    int row_count;
    int capacity;
} BenchmarkRowStore;

/*
 * 벤치마크용 단순 행 저장소의 메모리를 모두 해제한다.
 */
static void benchmark_plain_store_free(BenchmarkRowStore *store) {
    int i;
    int j;

    if (store == NULL || store->rows == NULL) {
        return;
    }

    for (i = 0; i < store->row_count; i++) {
        if (store->rows[i] == NULL) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            free(store->rows[i][j]);
            store->rows[i][j] = NULL;
        }
        free(store->rows[i]);
    }

    free(store->rows);
    store->rows = NULL;
    store->row_count = 0;
    store->capacity = 0;
}

/*
 * 벤치마크용 단순 행 저장소에 용량을 확보한다.
 */
static int benchmark_plain_store_reserve(BenchmarkRowStore *store) {
    char ***new_rows;
    int new_capacity;

    if (store == NULL) {
        return FAILURE;
    }

    if (store->row_count < store->capacity) {
        return SUCCESS;
    }

    new_capacity = store->capacity == 0 ? INITIAL_ROW_CAPACITY : store->capacity * 2;
    new_rows = (char ***)realloc(store->rows, (size_t)new_capacity * sizeof(char **));
    if (new_rows == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    store->rows = new_rows;
    store->capacity = new_capacity;
    return SUCCESS;
}

/*
 * 벤치마크용 no-index 저장소에 행 하나를 추가한다.
 */
static int benchmark_plain_store_append(BenchmarkRowStore *store, int id,
                                        const char *name, const char *age) {
    char **row;
    char id_buffer[MAX_VALUE_LEN];
    int i;

    if (store == NULL || name == NULL || age == NULL) {
        return FAILURE;
    }

    if (benchmark_plain_store_reserve(store) != SUCCESS) {
        return FAILURE;
    }

    row = (char **)calloc(3, sizeof(char *));
    if (row == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    snprintf(id_buffer, sizeof(id_buffer), "%d", id);
    row[0] = utils_strdup(id_buffer);
    row[1] = utils_strdup(name);
    row[2] = utils_strdup(age);
    if (row[0] == NULL || row[1] == NULL || row[2] == NULL) {
        for (i = 0; i < 3; i++) {
            free(row[i]);
            row[i] = NULL;
        }
        free(row);
        return FAILURE;
    }

    store->rows[store->row_count++] = row;
    return SUCCESS;
}

/*
 * 각 synthetic row의 name/age 문자열을 채운다.
 */
static void benchmark_build_values(int index, char *name, size_t name_size,
                                   char *age, size_t age_size) {
    snprintf(name, name_size, "user_%d", index + 1);
    snprintf(age, age_size, "%d", 20 + (index % 50));
}

/*
 * runtime insert에 재사용할 INSERT 문 구조를 준비한다.
 */
static void benchmark_prepare_insert_stmt(InsertStatement *stmt) {
    memset(stmt, 0, sizeof(*stmt));
    snprintf(stmt->table_name, sizeof(stmt->table_name), "benchmark_users");
    stmt->column_count = 2;
    snprintf(stmt->columns[0], sizeof(stmt->columns[0]), "name");
    snprintf(stmt->columns[1], sizeof(stmt->columns[1]), "age");
}

/*
 * 인덱스가 있는 메모리 런타임 삽입 시간을 측정한다.
 */
static int benchmark_measure_indexed_insert(const BenchmarkConfig *config,
                                            TableRuntime *table,
                                            int **out_inserted_ids,
                                            double *elapsed_ms) {
    InsertStatement stmt;
    char name[MAX_VALUE_LEN];
    char age[MAX_VALUE_LEN];
    char **row;
    int *inserted_ids;
    int row_index;
    int id_key;
    int i;
    clock_t start;
    clock_t end;

    if (config == NULL || table == NULL || out_inserted_ids == NULL ||
        elapsed_ms == NULL) {
        return FAILURE;
    }

    benchmark_prepare_insert_stmt(&stmt);
    table_init(table);
    *out_inserted_ids = NULL;
    inserted_ids = (int *)malloc((size_t)config->row_count * sizeof(int));
    if (inserted_ids == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    start = clock();
    for (i = 0; i < config->row_count; i++) {
        benchmark_build_values(i, name, sizeof(name), age, sizeof(age));
        snprintf(stmt.values[0], sizeof(stmt.values[0]), "%s", name);
        snprintf(stmt.values[1], sizeof(stmt.values[1]), "%s", age);
        if (table_insert_row(table, &stmt, &row_index) != SUCCESS) {
            free(inserted_ids);
            table_free(table);
            return FAILURE;
        }

        row = table_get_row_by_slot(table, row_index);
        if (row == NULL || !utils_is_integer(row[0])) {
            free(inserted_ids);
            table_free(table);
            return FAILURE;
        }

        id_key = (int)utils_parse_integer(row[0]);
        inserted_ids[i] = id_key;
    }
    end = clock();

    *out_inserted_ids = inserted_ids;
    *elapsed_ms = 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
    return SUCCESS;
}

/*
 * 인덱스가 없는 단순 삽입 시간을 측정한다.
 */
static int benchmark_measure_plain_insert(const BenchmarkConfig *config,
                                          double *elapsed_ms) {
    BenchmarkRowStore store;
    char name[MAX_VALUE_LEN];
    char age[MAX_VALUE_LEN];
    int i;
    clock_t start;
    clock_t end;

    if (config == NULL || elapsed_ms == NULL) {
        return FAILURE;
    }

    memset(&store, 0, sizeof(store));
    start = clock();
    for (i = 0; i < config->row_count; i++) {
        benchmark_build_values(i, name, sizeof(name), age, sizeof(age));
        if (benchmark_plain_store_append(&store, i + 1, name, age) != SUCCESS) {
            benchmark_plain_store_free(&store);
            return FAILURE;
        }
    }
    end = clock();

    *elapsed_ms = 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
    benchmark_plain_store_free(&store);
    return SUCCESS;
}

/*
 * B+ 트리 id 조회 시간을 측정한다.
 */
static int benchmark_measure_id_lookup(const BenchmarkConfig *config,
                                       BPTreeNode *id_index_root,
                                       const int *inserted_ids,
                                       double *elapsed_ms) {
    int row_index;
    int i;
    int key;
    clock_t start;
    clock_t end;

    if (config == NULL || elapsed_ms == NULL || id_index_root == NULL ||
        inserted_ids == NULL) {
        return FAILURE;
    }

    start = clock();
    for (i = 0; i < config->query_count; i++) {
        key = inserted_ids[i % config->row_count];
        if (bptree_search(id_index_root, key, &row_index) != SUCCESS) {
            return FAILURE;
        }
    }
    end = clock();

    *elapsed_ms = 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
    return SUCCESS;
}

/*
 * 일반 필드 선형 탐색 시간을 측정한다.
 */
static int benchmark_measure_linear_scan(const BenchmarkConfig *config,
                                         const TableRuntime *table,
                                         double *elapsed_ms) {
    WhereClause where;
    int *row_indices;
    int row_count;
    int i;
    clock_t start;
    clock_t end;

    if (config == NULL || table == NULL || elapsed_ms == NULL) {
        return FAILURE;
    }

    memset(&where, 0, sizeof(where));
    snprintf(where.column, sizeof(where.column), "age");
    snprintf(where.op, sizeof(where.op), "=");

    start = clock();
    for (i = 0; i < config->query_count; i++) {
        snprintf(where.value, sizeof(where.value), "%d", 20 + (i % 50));
        row_indices = NULL;
        row_count = 0;
        if (table_linear_scan_by_field(table, &where, &row_indices, &row_count) != SUCCESS) {
            free(row_indices);
            return FAILURE;
        }
        free(row_indices);
    }
    end = clock();

    *elapsed_ms = 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
    return SUCCESS;
}

BenchmarkConfig benchmark_default_config(void) {
    BenchmarkConfig config;

    config.row_count = 1000000;
    config.query_count = 10000;
    return config;
}

int benchmark_run(const BenchmarkConfig *config) {
    BenchmarkConfig active_config;
    TableRuntime indexed_table;
    int *inserted_ids;
    double indexed_insert_ms;
    double plain_insert_ms;
    double id_lookup_ms;
    double linear_scan_ms;

    active_config = config == NULL ? benchmark_default_config() : *config;
    if (active_config.row_count <= 0 || active_config.query_count <= 0) {
        fprintf(stderr, "Error: Benchmark config must be positive.\n");
        return FAILURE;
    }

    indexed_insert_ms = 0.0;
    plain_insert_ms = 0.0;
    id_lookup_ms = 0.0;
    linear_scan_ms = 0.0;
    inserted_ids = NULL;
    memset(&indexed_table, 0, sizeof(indexed_table));

    if (benchmark_measure_indexed_insert(&active_config, &indexed_table, &inserted_ids,
                                         &indexed_insert_ms) != SUCCESS) {
        return FAILURE;
    }
    if (benchmark_measure_plain_insert(&active_config, &plain_insert_ms) != SUCCESS) {
        free(inserted_ids);
        table_free(&indexed_table);
        return FAILURE;
    }
    if (benchmark_measure_id_lookup(&active_config, indexed_table.id_index_root, inserted_ids,
                                    &id_lookup_ms) != SUCCESS) {
        free(inserted_ids);
        table_free(&indexed_table);
        return FAILURE;
    }
    if (benchmark_measure_linear_scan(&active_config, &indexed_table,
                                      &linear_scan_ms) != SUCCESS) {
        free(inserted_ids);
        table_free(&indexed_table);
        return FAILURE;
    }

    printf("[Benchmark]\n");
    printf("Rows: %d\n", active_config.row_count);
    printf("Queries: %d\n", active_config.query_count);
    printf("Insert with id index: %.3f ms\n", indexed_insert_ms);
    printf("Insert without id index: %.3f ms\n", plain_insert_ms);
    printf("id lookup via B+ tree: %.3f ms\n", id_lookup_ms);
    printf("field lookup via linear scan: %.3f ms\n", linear_scan_ms);

    free(inserted_ids);
    table_free(&indexed_table);
    return SUCCESS;
}
