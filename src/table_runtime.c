#include "table_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TableRuntime active_table;
static int active_table_ready = 0;

/*
 * 고정 컬럼 배열에서 target 컬럼 위치를 찾는다.
 */
static int table_find_column_index(const char columns[][MAX_IDENTIFIER_LEN],
                                   int col_count, const char *target) {
    int index;

    for (index = 0; index < col_count; index++) {
        if (utils_equals_ignore_case(columns[index], target)) {
            return index;
        }
    }

    return FAILURE;
}

/*
 * 행 하나를 동적으로 해제한다.
 */
static void table_free_row(char **row, int col_count) {
    int index;

    if (row == NULL) {
        return;
    }

    for (index = 0; index < col_count; index++) {
        free(row[index]);
    }
    free(row);
}

/*
 * 비교 연산자 하나를 두 문자열 값에 적용한다.
 */
static int table_compare_with_operator(const char *lhs, const char *op,
                                       const char *rhs) {
    int comparison;

    comparison = utils_compare_values(lhs, rhs);
    if (strcmp(op, "=") == 0) {
        return comparison == 0;
    }
    if (strcmp(op, "!=") == 0) {
        return comparison != 0;
    }
    if (strcmp(op, ">") == 0) {
        return comparison > 0;
    }
    if (strcmp(op, "<") == 0) {
        return comparison < 0;
    }
    if (strcmp(op, ">=") == 0) {
        return comparison >= 0;
    }
    if (strcmp(op, "<=") == 0) {
        return comparison <= 0;
    }

    fprintf(stderr, "Error: Unsupported WHERE operator '%s'.\n", op);
    return FAILURE;
}

/*
 * 입력 컬럼 목록으로 빈 런타임 스키마를 확정한다.
 */
static int table_prepare_schema(TableRuntime *table,
                                const char columns[][MAX_IDENTIFIER_LEN],
                                int column_count) {
    int index;
    int inner_index;

    if (table == NULL || columns == NULL || column_count <= 0 ||
        column_count + 1 > MAX_COLUMNS) {
        return FAILURE;
    }

    if (utils_safe_strcpy(table->columns[0], sizeof(table->columns[0]), "id") != SUCCESS) {
        return FAILURE;
    }

    for (index = 0; index < column_count; index++) {
        if (utils_equals_ignore_case(columns[index], "id")) {
            fprintf(stderr, "Error: Manual id insertion is not allowed.\n");
            return FAILURE;
        }

        for (inner_index = index + 1; inner_index < column_count; inner_index++) {
            if (utils_equals_ignore_case(columns[index], columns[inner_index])) {
                fprintf(stderr, "Error: Duplicate column '%s'.\n", columns[index]);
                return FAILURE;
            }
        }

        if (utils_safe_strcpy(table->columns[index + 1],
                              sizeof(table->columns[index + 1]),
                              columns[index]) != SUCCESS) {
            fprintf(stderr, "Error: Column name is too long.\n");
            return FAILURE;
        }
    }

    table->col_count = column_count + 1;
    table->id_column_index = 0;
    table->next_id = 1;
    table->loaded = 1;
    return SUCCESS;
}

/*
 * 이미 확정된 스키마와 INSERT 입력 컬럼이 같은 집합인지 검사한다.
 */
static int table_validate_insert_columns(TableRuntime *table,
                                         const char columns[][MAX_IDENTIFIER_LEN],
                                         int column_count) {
    int index;
    int inner_index;

    if (table == NULL || columns == NULL || column_count != table->col_count - 1) {
        fprintf(stderr, "Error: INSERT columns do not match table schema.\n");
        return FAILURE;
    }

    for (index = 0; index < column_count; index++) {
        if (utils_equals_ignore_case(columns[index], "id")) {
            fprintf(stderr, "Error: Manual id insertion is not allowed.\n");
            return FAILURE;
        }

        for (inner_index = index + 1; inner_index < column_count; inner_index++) {
            if (utils_equals_ignore_case(columns[index], columns[inner_index])) {
                fprintf(stderr, "Error: Duplicate column '%s'.\n", columns[index]);
                return FAILURE;
            }
        }

        if (table_find_column_index(table->columns, table->col_count,
                                    columns[index]) == FAILURE) {
            fprintf(stderr, "Error: Column '%s' not found.\n", columns[index]);
            return FAILURE;
        }
    }

    for (index = 1; index < table->col_count; index++) {
        if (table_find_column_index(columns, column_count,
                                    table->columns[index]) == FAILURE) {
            fprintf(stderr, "Error: INSERT columns do not match table schema.\n");
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * 행 하나가 WHERE 조건을 만족하는지 검사한다.
 */
static int table_row_matches_where(const TableRuntime *table, char **row,
                                   const WhereClause *where) {
    int column_index;

    if (table == NULL || row == NULL || where == NULL) {
        return FAILURE;
    }

    column_index = table_find_column_index(table->columns, table->col_count,
                                           where->column);
    if (column_index == FAILURE) {
        fprintf(stderr, "Error: Column '%s' not found.\n", where->column);
        return FAILURE;
    }

    return table_compare_with_operator(row[column_index], where->op, where->value);
}

/*
 * 런타임 테이블 구조체를 빈 상태로 초기화한다.
 */
int table_init(TableRuntime *table, const char *table_name) {
    if (table == NULL || table_name == NULL) {
        return FAILURE;
    }

    memset(table, 0, sizeof(*table));
    if (utils_safe_strcpy(table->table_name, sizeof(table->table_name), table_name) != SUCCESS) {
        fprintf(stderr, "Error: Table name is too long.\n");
        return FAILURE;
    }

    table->id_column_index = 0;
    table->next_id = 1;
    return SUCCESS;
}

/*
 * 런타임 테이블이 소유한 메모리를 모두 해제한다.
 */
void table_free(TableRuntime *table) {
    int index;

    if (table == NULL) {
        return;
    }

    for (index = 0; index < table->row_count; index++) {
        table_free_row(table->rows[index], table->col_count);
    }

    free(table->rows);
    bptree_free(table->id_index_root);
    memset(table, 0, sizeof(*table));
}

/*
 * 행 배열 용량이 부족하면 한 단계 늘린다.
 */
int table_reserve_if_needed(TableRuntime *table) {
    char ***new_rows;
    int new_capacity;

    if (table == NULL) {
        return FAILURE;
    }

    if (table->row_count < table->capacity) {
        return SUCCESS;
    }

    new_capacity = table->capacity == 0 ? INITIAL_ROW_CAPACITY : table->capacity * 2;
    new_rows = (char ***)realloc(table->rows, (size_t)new_capacity * sizeof(char **));
    if (new_rows == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    table->rows = new_rows;
    table->capacity = new_capacity;
    return SUCCESS;
}

/*
 * 현재 활성 테이블을 가져오거나, 다른 테이블이면 새 빈 런타임으로 바꾼다.
 */
TableRuntime *table_get_or_load(const char *table_name) {
    if (table_name == NULL) {
        return NULL;
    }

    if (!active_table_ready) {
        if (table_init(&active_table, table_name) != SUCCESS) {
            return NULL;
        }
        active_table_ready = 1;
        return &active_table;
    }

    if (utils_equals_ignore_case(active_table.table_name, table_name)) {
        return &active_table;
    }

    table_free(&active_table);
    if (table_init(&active_table, table_name) != SUCCESS) {
        active_table_ready = 0;
        return NULL;
    }

    active_table_ready = 1;
    return &active_table;
}

/*
 * 현재 활성 런타임 테이블을 정리한다.
 */
void table_cleanup_active(void) {
    if (!active_table_ready) {
        return;
    }

    table_free(&active_table);
    active_table_ready = 0;
}

/*
 * 입력 컬럼/값 쌍을 메모리 테이블에 행 하나로 추가한다.
 */
int table_insert_row(TableRuntime *table,
                     const char columns[][MAX_IDENTIFIER_LEN],
                     const char values[][MAX_VALUE_LEN],
                     int column_count, int use_index,
                     long long *inserted_id) {
    char **row;
    char id_buffer[MAX_VALUE_LEN];
    int input_index;
    int row_index;
    int column_index;
    long long new_id;

    if (table == NULL || columns == NULL || values == NULL || column_count <= 0) {
        return FAILURE;
    }

    if (!table->loaded) {
        if (table_prepare_schema(table, columns, column_count) != SUCCESS) {
            return FAILURE;
        }
    } else if (table_validate_insert_columns(table, columns, column_count) != SUCCESS) {
        return FAILURE;
    }

    if (table_reserve_if_needed(table) != SUCCESS) {
        return FAILURE;
    }

    row = (char **)calloc((size_t)table->col_count, sizeof(char *));
    if (row == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    new_id = table->next_id;
    if (snprintf(id_buffer, sizeof(id_buffer), "%lld", new_id) < 0) {
        table_free_row(row, table->col_count);
        return FAILURE;
    }

    row[0] = utils_strdup(id_buffer);
    if (row[0] == NULL) {
        table_free_row(row, table->col_count);
        return FAILURE;
    }

    for (column_index = 1; column_index < table->col_count; column_index++) {
        input_index = table_find_column_index(columns, column_count,
                                              table->columns[column_index]);
        if (input_index == FAILURE) {
            fprintf(stderr, "Error: INSERT columns do not match table schema.\n");
            table_free_row(row, table->col_count);
            return FAILURE;
        }

        row[column_index] = utils_strdup(values[input_index]);
        if (row[column_index] == NULL) {
            table_free_row(row, table->col_count);
            return FAILURE;
        }
    }

    row_index = table->row_count;
    if (use_index &&
        bptree_insert(&table->id_index_root, new_id, row_index) != SUCCESS) {
        table_free_row(row, table->col_count);
        return FAILURE;
    }

    table->rows[row_index] = row;
    table->row_count++;
    table->next_id = new_id + 1;
    if (inserted_id != NULL) {
        *inserted_id = new_id;
    }

    return SUCCESS;
}

/*
 * row_index 위치의 행을 반환한다.
 */
char **table_get_row_by_slot(const TableRuntime *table, int row_index) {
    if (table == NULL || row_index < 0 || row_index >= table->row_count) {
        return NULL;
    }

    return table->rows[row_index];
}

/*
 * WHERE 조건을 선형 탐색해 일치하는 row_index 목록을 만든다.
 */
int table_linear_scan_by_field(const TableRuntime *table, const WhereClause *where,
                               int **matching_slots, int *match_count) {
    int *slots;
    int count;
    int index;
    int matches;

    if (table == NULL || matching_slots == NULL || match_count == NULL) {
        return FAILURE;
    }

    *matching_slots = NULL;
    *match_count = 0;
    if (!table->loaded || table->row_count == 0) {
        return SUCCESS;
    }

    slots = (int *)malloc((size_t)table->row_count * sizeof(int));
    if (slots == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    count = 0;
    for (index = 0; index < table->row_count; index++) {
        matches = where == NULL ? 1 : table_row_matches_where(table, table->rows[index], where);
        if (matches == FAILURE) {
            free(slots);
            return FAILURE;
        }
        if (matches) {
            slots[count++] = index;
        }
    }

    if (count == 0) {
        free(slots);
        return SUCCESS;
    }

    *matching_slots = slots;
    *match_count = count;
    return SUCCESS;
}
