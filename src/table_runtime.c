#include "table_runtime.h"

#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TableRuntime *table_active_runtime = NULL;

/*
 * 런타임 테이블의 한 행이 가진 각 셀 문자열을 모두 해제하는 함수다.
 * row는 table->col_count 길이로 할당되었다는 전제에서 사용한다.
 */
static void table_free_row_cells(TableRuntime *table, char **row) {
    int i;

    if (table == NULL || row == NULL) {
        return;
    }

    for (i = 0; i < table->col_count; i++) {
        free(row[i]);
        row[i] = NULL;
    }

    free(row);
}

/*
 * INSERT 문에 사용자가 id 컬럼을 직접 포함했는지 검사하는 함수다.
 * 메모리 런타임이 auto-id를 강제하기 때문에 id 컬럼은 허용하지 않는다.
 */
static int table_statement_has_id_column(const InsertStatement *stmt) {
    int i;

    if (stmt == NULL) {
        return 0;
    }

    for (i = 0; i < stmt->column_count; i++) {
        if (utils_equals_ignore_case(stmt->columns[i], "id")) {
            return 1;
        }
    }

    return 0;
}

/*
 * 첫 INSERT 문 기준으로 메모리 런타임의 컬럼 스키마를 확정하는 함수다.
 * 런타임 맨 앞에 id 컬럼을 자동으로 추가하고 나머지 컬럼을 그대로 복사한다.
 */
static int table_configure_schema_from_insert(TableRuntime *table,
                                              const InsertStatement *stmt) {
    int i;

    if (table == NULL || stmt == NULL) {
        return FAILURE;
    }

    if (stmt->column_count <= 0 || stmt->column_count + 1 > MAX_COLUMNS) {
        fprintf(stderr, "Error: Invalid INSERT column count for runtime table.\n");
        return FAILURE;
    }

    table->col_count = stmt->column_count + 1;
    table->id_column_index = 0;

    if (utils_safe_strcpy(table->columns[0], sizeof(table->columns[0]), "id") != SUCCESS) {
        fprintf(stderr, "Error: Failed to set runtime id column.\n");
        return FAILURE;
    }

    for (i = 0; i < stmt->column_count; i++) {
        if (utils_safe_strcpy(table->columns[i + 1], sizeof(table->columns[i + 1]),
                              stmt->columns[i]) != SUCCESS) {
            fprintf(stderr, "Error: Column name is too long for runtime table.\n");
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * 이후 INSERT 문이 이미 확정된 런타임 스키마와 일치하는지 검사하는 함수다.
 * 현재 단계에서는 컬럼 개수와 컬럼 순서가 같아야 같은 스키마로 본다.
 */
static int table_validate_insert_schema(const TableRuntime *table,
                                        const InsertStatement *stmt) {
    int i;

    if (table == NULL || stmt == NULL) {
        return FAILURE;
    }

    if (table->col_count != stmt->column_count + 1) {
        fprintf(stderr, "Error: INSERT schema does not match active runtime table.\n");
        return FAILURE;
    }

    for (i = 0; i < stmt->column_count; i++) {
        if (!utils_equals_ignore_case(table->columns[i + 1], stmt->columns[i])) {
            fprintf(stderr, "Error: INSERT schema does not match active runtime table.\n");
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * 정수 id 값을 문자열로 바꿔 새 메모리에 복제하는 함수다.
 * 반환된 문자열은 행 셀로 들어가므로 호출자가 해제 책임을 가진다.
 */
static char *table_duplicate_id_string(long long id_value) {
    char buffer[MAX_VALUE_LEN];
    int written;

    written = snprintf(buffer, sizeof(buffer), "%lld", id_value);
    if (written < 0 || written >= (int)sizeof(buffer)) {
        fprintf(stderr, "Error: Failed to format runtime id value.\n");
        return NULL;
    }

    return utils_strdup(buffer);
}

/*
 * INSERT 문의 값 배열을 런타임 한 행으로 복제하는 함수다.
 * 첫 번째 셀에는 자동 생성된 id 문자열을 넣고, 나머지 셀은 INSERT 값으로 채운다.
 */
static char **table_build_row_copy(TableRuntime *table, const InsertStatement *stmt,
                                   long long generated_id) {
    char **row;
    int i;

    if (table == NULL || stmt == NULL) {
        return NULL;
    }

    row = (char **)calloc((size_t)table->col_count, sizeof(char *));
    if (row == NULL) {
        fprintf(stderr, "Error: Failed to allocate runtime row.\n");
        return NULL;
    }

    row[0] = table_duplicate_id_string(generated_id);
    if (row[0] == NULL) {
        table_free_row_cells(table, row);
        return NULL;
    }

    for (i = 0; i < stmt->column_count; i++) {
        row[i + 1] = utils_strdup(stmt->values[i]);
        if (row[i + 1] == NULL) {
            table_free_row_cells(table, row);
            return NULL;
        }
    }

    return row;
}

/*
 * 컬럼 이름으로 런타임 스키마 안의 실제 컬럼 인덱스를 찾는 함수다.
 * 찾지 못하면 FAILURE를 반환한다.
 */
static int table_find_column_index(const TableRuntime *table, const char *column_name) {
    int i;

    if (table == NULL || column_name == NULL) {
        return FAILURE;
    }

    for (i = 0; i < table->col_count; i++) {
        if (utils_equals_ignore_case(table->columns[i], column_name)) {
            return i;
        }
    }

    return FAILURE;
}

/*
 * 선형 탐색에서 허용하는 비교 연산자인지 미리 검사하는 함수다.
 * 아직 executor와 연결되기 전 단계라 지원 범위를 여기서 명확하게 고정한다.
 */
static int table_is_supported_scan_operator(const char *op) {
    if (op == NULL || op[0] == '\0') {
        return 1;
    }

    return strcmp(op, "=") == 0 || strcmp(op, "!=") == 0 ||
           strcmp(op, ">") == 0 || strcmp(op, "<") == 0 ||
           strcmp(op, ">=") == 0 || strcmp(op, "<=") == 0;
}

/*
 * 연산자와 비교 값을 기준으로 셀 문자열 하나가 WHERE 조건을 만족하는지 판단하는 함수다.
 * 비교 자체는 기존 유틸리티의 숫자/문자 혼합 비교 규칙을 그대로 재사용한다.
 */
static int table_row_matches_condition(const char *cell_value, const char *op,
                                       const char *target_value) {
    int compare_result;

    if (op == NULL || op[0] == '\0') {
        return 1;
    }

    compare_result = utils_compare_values(cell_value == NULL ? "" : cell_value,
                                          target_value == NULL ? "" : target_value);

    if (strcmp(op, "=") == 0) {
        return compare_result == 0;
    }
    if (strcmp(op, "!=") == 0) {
        return compare_result != 0;
    }
    if (strcmp(op, ">") == 0) {
        return compare_result > 0;
    }
    if (strcmp(op, "<") == 0) {
        return compare_result < 0;
    }
    if (strcmp(op, ">=") == 0) {
        return compare_result >= 0;
    }
    if (strcmp(op, "<=") == 0) {
        return compare_result <= 0;
    }

    return 0;
}

/*
 * table_linear_scan_by_field()가 사용할 결과 row_index 버퍼를 필요한 크기로 만드는 함수다.
 * 최대 행 수만큼 미리 잡아 단순한 선형 탐색 구현을 유지한다.
 */
static int table_allocate_match_buffer(const TableRuntime *table, int **out_row_indices) {
    if (table == NULL || out_row_indices == NULL) {
        return FAILURE;
    }

    if (table->row_count == 0) {
        *out_row_indices = NULL;
        return SUCCESS;
    }

    *out_row_indices = (int *)malloc((size_t)table->row_count * sizeof(int));
    if (*out_row_indices == NULL) {
        fprintf(stderr, "Error: Failed to allocate runtime scan result buffer.\n");
        return FAILURE;
    }

    return SUCCESS;
}

/*
 * 런타임 테이블 구조체를 깨끗한 초기 상태로 만드는 함수다.
 * 동적 메모리 포인터와 메타데이터를 모두 기본값으로 맞춘다.
 */
void table_init(TableRuntime *table) {
    if (table == NULL) {
        return;
    }

    memset(table->table_name, 0, sizeof(table->table_name));
    memset(table->columns, 0, sizeof(table->columns));
    table->rows = NULL;
    table->row_count = 0;
    table->capacity = 0;
    table->col_count = 0;
    table->id_column_index = 0;
    table->next_id = 1;
    table->id_index_root = NULL;
    table->loaded = 0;
}

/*
 * 런타임 테이블이 가지고 있는 모든 행 메모리를 해제하는 함수다.
 * 이 단계에서는 B+ 트리 연결 전이므로 id_index_root는 항상 NULL이라고 가정한다.
 */
void table_free(TableRuntime *table) {
    int i;

    if (table == NULL) {
        return;
    }

    for (i = 0; i < table->row_count; i++) {
        table_free_row_cells(table, table->rows[i]);
        table->rows[i] = NULL;
    }

    free(table->rows);
    table->rows = NULL;

    table_init(table);
}

/*
 * 현재 활성 테이블을 반환하거나, 다른 테이블 이름이면 새 활성 테이블로 교체하는 함수다.
 * 이 단계에서는 디스크 로드 없이 항상 빈 메모리 런타임으로 시작한다.
 */
int table_get_or_load(const char *table_name, TableRuntime **out_table) {
    TableRuntime *new_table;

    if (table_name == NULL || out_table == NULL) {
        return FAILURE;
    }

    if (table_active_runtime != NULL &&
        utils_equals_ignore_case(table_active_runtime->table_name, table_name)) {
        *out_table = table_active_runtime;
        return SUCCESS;
    }

    if (table_active_runtime != NULL) {
        table_free(table_active_runtime);
        free(table_active_runtime);
        table_active_runtime = NULL;
    }

    new_table = (TableRuntime *)malloc(sizeof(TableRuntime));
    if (new_table == NULL) {
        fprintf(stderr, "Error: Failed to allocate active runtime table.\n");
        return FAILURE;
    }

    table_init(new_table);
    if (utils_safe_strcpy(new_table->table_name, sizeof(new_table->table_name),
                          table_name) != SUCCESS) {
        fprintf(stderr, "Error: Table name is too long for runtime table.\n");
        free(new_table);
        return FAILURE;
    }

    new_table->loaded = 1;
    table_active_runtime = new_table;
    *out_table = table_active_runtime;
    return SUCCESS;
}

/*
 * 행 하나를 더 넣기 전에 rows 포인터 배열의 용량을 보장하는 함수다.
 * 부족하면 2배 확장 전략으로 재할당한다.
 */
int table_reserve_if_needed(TableRuntime *table) {
    int new_capacity;
    char ***new_rows;

    if (table == NULL) {
        return FAILURE;
    }

    if (table->row_count < table->capacity) {
        return SUCCESS;
    }

    new_capacity = table->capacity == 0 ? INITIAL_ROW_CAPACITY : table->capacity * 2;
    new_rows = (char ***)realloc(table->rows, (size_t)new_capacity * sizeof(char **));
    if (new_rows == NULL) {
        fprintf(stderr, "Error: Failed to grow runtime row buffer.\n");
        return FAILURE;
    }

    table->rows = new_rows;
    table->capacity = new_capacity;
    return SUCCESS;
}

/*
 * INSERT 문을 현재 활성 런타임 테이블의 새 메모리 행으로 추가하는 함수다.
 * 첫 INSERT에서는 스키마를 확정하고, 이후에는 같은 스키마만 허용한다.
 */
int table_insert_row(TableRuntime *table, const InsertStatement *stmt,
                     int *out_row_index, long long *out_id) {
    char **new_row;
    int row_index;
    long long generated_id;

    if (table == NULL || stmt == NULL) {
        return FAILURE;
    }

    if (!table->loaded) {
        fprintf(stderr, "Error: Runtime table is not initialized.\n");
        return FAILURE;
    }

    if (!utils_equals_ignore_case(table->table_name, stmt->table_name)) {
        fprintf(stderr, "Error: INSERT targets a different active runtime table.\n");
        return FAILURE;
    }

    if (table_statement_has_id_column(stmt)) {
        fprintf(stderr, "Error: Runtime table does not allow explicit id values.\n");
        return FAILURE;
    }

    if (table->col_count == 0) {
        if (table_configure_schema_from_insert(table, stmt) != SUCCESS) {
            return FAILURE;
        }
    } else if (table_validate_insert_schema(table, stmt) != SUCCESS) {
        return FAILURE;
    }

    if (table_reserve_if_needed(table) != SUCCESS) {
        return FAILURE;
    }

    generated_id = table->next_id;
    new_row = table_build_row_copy(table, stmt, generated_id);
    if (new_row == NULL) {
        return FAILURE;
    }

    row_index = table->row_count;
    table->rows[row_index] = new_row;
    table->row_count++;
    table->next_id++;

    if (out_row_index != NULL) {
        *out_row_index = row_index;
    }
    if (out_id != NULL) {
        *out_id = generated_id;
    }

    return SUCCESS;
}

/*
 * row_index가 가리키는 런타임 행 포인터를 그대로 반환하는 함수다.
 * 범위를 벗어나면 NULL을 반환해 호출자가 실패를 감지할 수 있게 한다.
 */
char **table_get_row_by_slot(const TableRuntime *table, int row_index) {
    if (table == NULL || row_index < 0 || row_index >= table->row_count) {
        return NULL;
    }

    return table->rows[row_index];
}

/*
 * 주어진 컬럼 조건으로 모든 행을 처음부터 끝까지 검사하는 선형 탐색 함수다.
 * column_name이 비어 있으면 WHERE 없는 SELECT 용도로 모든 row_index를 반환한다.
 */
int table_linear_scan_by_field(const TableRuntime *table, const char *column_name,
                               const char *op, const char *value,
                               int **out_row_indices, int *out_match_count) {
    int column_index;
    int *matches;
    int match_count;
    int i;

    if (table == NULL || out_row_indices == NULL || out_match_count == NULL) {
        return FAILURE;
    }

    if (column_name == NULL || column_name[0] == '\0') {
        column_index = FAILURE;
    } else {
        column_index = table_find_column_index(table, column_name);
        if (column_index == FAILURE) {
            fprintf(stderr, "Error: Column '%s' not found in runtime table.\n",
                    column_name);
            return FAILURE;
        }
    }

    if (!table_is_supported_scan_operator(op)) {
        fprintf(stderr, "Error: Unsupported scan operator '%s'.\n", op);
        return FAILURE;
    }

    if (table_allocate_match_buffer(table, &matches) != SUCCESS) {
        return FAILURE;
    }

    match_count = 0;
    for (i = 0; i < table->row_count; i++) {
        if (column_index == FAILURE ||
            table_row_matches_condition(table->rows[i][column_index], op, value)) {
            matches[match_count++] = i;
        }
    }

    if (match_count == 0) {
        free(matches);
        matches = NULL;
    }

    *out_row_indices = matches;
    *out_match_count = match_count;
    return SUCCESS;
}

/*
 * 모듈 전역으로 유지 중인 활성 런타임 테이블을 완전히 정리하는 함수다.
 * REPL 종료나 테스트 종료 시 남은 메모리를 회수할 때 사용한다.
 */
void table_runtime_cleanup(void) {
    if (table_active_runtime == NULL) {
        return;
    }

    table_free(table_active_runtime);
    free(table_active_runtime);
    table_active_runtime = NULL;
}
