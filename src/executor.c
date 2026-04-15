#include "executor.h"

#include "bptree.h"
#include "table_runtime.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 메모리 런타임 스키마에서 컬럼 이름을 대소문자 무시로 찾는 함수다.
 * 찾은 컬럼의 실제 인덱스를 반환하고, 없으면 FAILURE를 반환한다.
 */
static int executor_find_column_index(const char columns[][MAX_IDENTIFIER_LEN],
                                      int col_count, const char *target) {
    int i;

    for (i = 0; i < col_count; i++) {
        if (utils_equals_ignore_case(columns[i], target)) {
            return i;
        }
    }

    return FAILURE;
}

/*
 * 결과 셀 문자열 하나를 새 메모리에 복제하는 함수다.
 * NULL 값은 빈 문자열로 처리해 표 출력 코드가 항상 안전하게 동작하게 한다.
 */
static char *executor_duplicate_cell(const char *value) {
    return utils_strdup(value == NULL ? "" : value);
}

/*
 * SELECT 결과를 담을 바깥쪽 행 배열을 할당하는 함수다.
 * row_count가 0이면 NULL을 반환해 빈 결과도 같은 인터페이스로 처리한다.
 */
static int executor_allocate_result_rows(char ****rows, int row_count) {
    if (rows == NULL) {
        return FAILURE;
    }

    if (row_count <= 0) {
        *rows = NULL;
        return SUCCESS;
    }

    *rows = (char ***)malloc((size_t)row_count * sizeof(char **));
    if (*rows == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    return SUCCESS;
}

/*
 * executor가 만든 결과 행 배열 전체를 해제하는 함수다.
 * 런타임 행과는 별개의 복제본만 담고 있으므로 executor 안에서 바로 정리할 수 있다.
 */
static void executor_free_result_rows(char ***rows, int row_count, int col_count) {
    int i;
    int j;

    if (rows == NULL) {
        return;
    }

    for (i = 0; i < row_count; i++) {
        if (rows[i] == NULL) {
            continue;
        }

        for (j = 0; j < col_count; j++) {
            free(rows[i][j]);
            rows[i][j] = NULL;
        }

        free(rows[i]);
        rows[i] = NULL;
    }

    free(rows);
}

/*
 * 원본 행에서 선택된 컬럼만 복사해 결과 행 하나를 만드는 함수다.
 * projection 단계에서 필요한 셀만 복제해 표 출력과 결과 정리를 단순하게 유지한다.
 */
static int executor_copy_projected_row(char ***result_rows, int result_index,
                                       char **source_row,
                                       const int *selected_indices,
                                       int selected_count) {
    int i;
    int j;

    result_rows[result_index] = (char **)malloc((size_t)selected_count * sizeof(char *));
    if (result_rows[result_index] == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    for (i = 0; i < selected_count; i++) {
        result_rows[result_index][i] =
            executor_duplicate_cell(source_row[selected_indices[i]]);
        if (result_rows[result_index][i] == NULL) {
            for (j = 0; j < i; j++) {
                free(result_rows[result_index][j]);
                result_rows[result_index][j] = NULL;
            }
            free(result_rows[result_index]);
            result_rows[result_index] = NULL;
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * SELECT 표 출력용 가로 경계선을 한 줄 출력하는 함수다.
 * 계산된 표시 폭을 기준으로 MySQL 스타일 표 경계를 만든다.
 */
static void executor_print_border(const int *widths, int col_count) {
    int i;
    int j;

    for (i = 0; i < col_count; i++) {
        putchar('+');
        for (j = 0; j < widths[i] + 2; j++) {
            putchar('-');
        }
    }
    puts("+");
}

/*
 * 표시 폭을 고려해 조회 결과를 표 형태로 출력하는 함수다.
 * CJK 문자의 폭까지 반영해 컬럼 정렬이 깨지지 않게 유지한다.
 */
static void executor_print_table(char headers[][MAX_IDENTIFIER_LEN], int header_count,
                                 char ***rows, int row_count) {
    int widths[MAX_COLUMNS];
    int i;
    int j;
    int cell_width;

    for (i = 0; i < header_count; i++) {
        widths[i] = utils_display_width(headers[i]);
    }

    for (i = 0; i < row_count; i++) {
        for (j = 0; j < header_count; j++) {
            cell_width = utils_display_width(rows[i][j]);
            if (cell_width > widths[j]) {
                widths[j] = cell_width;
            }
        }
    }

    executor_print_border(widths, header_count);
    for (i = 0; i < header_count; i++) {
        printf("| ");
        utils_print_padded(stdout, headers[i], widths[i]);
        putchar(' ');
    }
    puts("|");
    executor_print_border(widths, header_count);

    for (i = 0; i < row_count; i++) {
        for (j = 0; j < header_count; j++) {
            printf("| ");
            utils_print_padded(stdout, rows[i][j], widths[j]);
            putchar(' ');
        }
        puts("|");
    }

    executor_print_border(widths, header_count);
}

/*
 * SELECT 대상 컬럼을 런타임 스키마 인덱스와 출력 헤더로 변환하는 함수다.
 * `SELECT *`면 전체 컬럼을 그대로 선택하고, 명시 컬럼이면 존재 여부를 검증한다.
 */
static int executor_prepare_projection(const SelectStatement *stmt,
                                       const TableRuntime *table,
                                       int selected_indices[],
                                       char headers[][MAX_IDENTIFIER_LEN],
                                       int *selected_count) {
    int i;
    int column_index;

    if (stmt == NULL || table == NULL || selected_indices == NULL ||
        headers == NULL || selected_count == NULL) {
        return FAILURE;
    }

    if (stmt->column_count == 0) {
        for (i = 0; i < table->col_count; i++) {
            selected_indices[i] = i;
            if (utils_safe_strcpy(headers[i], sizeof(headers[i]),
                                  table->columns[i]) != SUCCESS) {
                fprintf(stderr, "Error: Column name is too long.\n");
                return FAILURE;
            }
        }
        *selected_count = table->col_count;
        return SUCCESS;
    }

    for (i = 0; i < stmt->column_count; i++) {
        column_index = executor_find_column_index(table->columns, table->col_count,
                                                  stmt->columns[i]);
        if (column_index == FAILURE) {
            fprintf(stderr, "Error: Column '%s' not found.\n", stmt->columns[i]);
            return FAILURE;
        }

        selected_indices[i] = column_index;
        if (utils_safe_strcpy(headers[i], sizeof(headers[i]),
                              table->columns[column_index]) != SUCCESS) {
            fprintf(stderr, "Error: Column name is too long.\n");
            return FAILURE;
        }
    }

    *selected_count = stmt->column_count;
    return SUCCESS;
}

/*
 * `WHERE id = 정수` 형태인지 검사해 B+ 트리 최적 경로를 쓸 수 있는지 판단하는 함수다.
 * 과제 명세상 이 경우만 인덱스를 허용하고, 나머지는 모두 선형 탐색으로 보낸다.
 */
static int executor_can_use_id_index(const TableRuntime *table,
                                     const SelectStatement *stmt) {
    if (table == NULL || stmt == NULL || !stmt->has_where) {
        return 0;
    }

    if (!utils_equals_ignore_case(stmt->where.column, "id")) {
        return 0;
    }

    if (strcmp(stmt->where.op, "=") != 0) {
        return 0;
    }

    return utils_is_integer(stmt->where.value);
}

/*
 * row_index 목록을 실제 결과 행 복제본으로 바꾸는 함수다.
 * 인덱스 검색과 선형 탐색 모두 이 헬퍼를 거쳐 동일한 결과 생성 경로를 사용한다.
 */
static int executor_build_result_from_row_indices(const TableRuntime *table,
                                                  const int *selected_indices,
                                                  int selected_count,
                                                  const int *row_indices,
                                                  int row_count,
                                                  char ****out_rows) {
    char ***result_rows;
    char **source_row;
    int i;

    if (table == NULL || selected_indices == NULL || out_rows == NULL) {
        return FAILURE;
    }

    if (executor_allocate_result_rows(&result_rows, row_count) != SUCCESS) {
        return FAILURE;
    }

    for (i = 0; i < row_count; i++) {
        source_row = table_get_row_by_slot(table, row_indices[i]);
        if (source_row == NULL) {
            executor_free_result_rows(result_rows, i, selected_count);
            return FAILURE;
        }

        if (executor_copy_projected_row(result_rows, i, source_row,
                                        selected_indices, selected_count) != SUCCESS) {
            executor_free_result_rows(result_rows, i, selected_count);
            return FAILURE;
        }
    }

    *out_rows = result_rows;
    return SUCCESS;
}

/*
 * `WHERE id = 값` SELECT를 B+ 트리로 처리하는 함수다.
 * id를 row_index로 바로 찾아 한 행만 projection해 결과를 만든다.
 */
static int executor_select_by_id(const TableRuntime *table,
                                 const SelectStatement *stmt,
                                 const int *selected_indices,
                                 int selected_count,
                                 char ****out_rows, int *out_row_count) {
    int row_index;
    long long target_id;

    if (table == NULL || stmt == NULL || selected_indices == NULL ||
        out_rows == NULL || out_row_count == NULL) {
        return FAILURE;
    }

    target_id = utils_parse_integer(stmt->where.value);
    if (bptree_search(table->id_index_root, target_id, &row_index) != SUCCESS) {
        *out_rows = NULL;
        *out_row_count = 0;
        return SUCCESS;
    }

    if (executor_build_result_from_row_indices(table, selected_indices, selected_count,
                                               &row_index, 1, out_rows) != SUCCESS) {
        return FAILURE;
    }

    *out_row_count = 1;
    return SUCCESS;
}

/*
 * WHERE가 없거나 비-id 조건인 SELECT를 선형 탐색으로 처리하는 함수다.
 * 명세대로 인덱스를 쓰지 않아야 하는 경로를 모두 여기로 모은다.
 */
static int executor_select_by_scan(const TableRuntime *table,
                                   const SelectStatement *stmt,
                                   const int *selected_indices,
                                   int selected_count,
                                   char ****out_rows, int *out_row_count) {
    int *row_indices;
    int match_count;
    const char *column_name;
    const char *op;
    const char *value;
    int status;

    if (table == NULL || stmt == NULL || selected_indices == NULL ||
        out_rows == NULL || out_row_count == NULL) {
        return FAILURE;
    }

    column_name = stmt->has_where ? stmt->where.column : NULL;
    op = stmt->has_where ? stmt->where.op : NULL;
    value = stmt->has_where ? stmt->where.value : NULL;
    row_indices = NULL;
    match_count = 0;

    if (table_linear_scan_by_field(table, column_name, op, value,
                                   &row_indices, &match_count) != SUCCESS) {
        return FAILURE;
    }

    status = executor_build_result_from_row_indices(table, selected_indices,
                                                    selected_count, row_indices,
                                                    match_count, out_rows);
    free(row_indices);
    if (status != SUCCESS) {
        return FAILURE;
    }

    *out_row_count = match_count;
    return SUCCESS;
}

/*
 * INSERT 문 하나를 메모리 런타임에 반영하는 함수다.
 * 활성 테이블을 가져온 뒤 auto-id append와 B+ 트리 등록을 한 번에 수행한다.
 */
static int executor_execute_insert(const InsertStatement *stmt) {
    TableRuntime *table;

    if (stmt == NULL) {
        return FAILURE;
    }

    if (table_get_or_load(stmt->table_name, &table) != SUCCESS) {
        return FAILURE;
    }

    if (table_insert_row(table, stmt, NULL, NULL) != SUCCESS) {
        return FAILURE;
    }

    printf("1 row inserted into %s.\n", stmt->table_name);
    return SUCCESS;
}

/*
 * SELECT 문 하나를 메모리 런타임 기준으로 실행하는 함수다.
 * `WHERE id = 상수`는 B+ 트리, 그 외는 선형 탐색으로 분기한 뒤 표 형태로 출력한다.
 */
static int executor_execute_select(const SelectStatement *stmt) {
    TableRuntime *table;
    int selected_indices[MAX_COLUMNS];
    char headers[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    int selected_count;
    char ***result_rows;
    int result_row_count;
    int status;

    if (stmt == NULL) {
        return FAILURE;
    }

    if (table_get_or_load(stmt->table_name, &table) != SUCCESS) {
        return FAILURE;
    }

    if (table->col_count == 0) {
        if (stmt->column_count != 0) {
            fprintf(stderr, "Error: Runtime table '%s' has no schema yet.\n",
                    stmt->table_name);
            return FAILURE;
        }

        printf("0 rows selected.\n");
        return SUCCESS;
    }

    status = executor_prepare_projection(stmt, table, selected_indices, headers,
                                         &selected_count);
    if (status != SUCCESS) {
        return FAILURE;
    }

    result_rows = NULL;
    result_row_count = 0;
    if (executor_can_use_id_index(table, stmt)) {
        status = executor_select_by_id(table, stmt, selected_indices, selected_count,
                                       &result_rows, &result_row_count);
    } else {
        status = executor_select_by_scan(table, stmt, selected_indices, selected_count,
                                         &result_rows, &result_row_count);
    }

    if (status != SUCCESS) {
        return FAILURE;
    }

    executor_print_table(headers, selected_count, result_rows, result_row_count);
    printf("%d row%s selected.\n", result_row_count,
           result_row_count == 1 ? "" : "s");

    executor_free_result_rows(result_rows, result_row_count, selected_count);
    return SUCCESS;
}

/*
 * DELETE 문은 이번 메모리 런타임 과제 범위에서 제외됐음을 명시적으로 알리는 함수다.
 * 기존 CSV 경로와 섞어 쓰지 않도록 실행 시 바로 실패를 반환한다.
 */
static int executor_execute_delete(const DeleteStatement *stmt) {
    if (stmt == NULL) {
        return FAILURE;
    }

    fprintf(stderr, "Error: DELETE is not supported in memory runtime mode.\n");
    return FAILURE;
}

/*
 * 파싱된 SQL 문을 받아 statement.type에 따라 INSERT, SELECT, DELETE로 분기하는 함수다.
 * 실제 실행 경로는 메모리 런타임과 B+ 트리 정책에 맞는 하위 함수로 위임한다.
 */
int executor_execute(const SqlStatement *statement) {
    if (statement == NULL) {
        return FAILURE;
    }

    switch (statement->type) {
        case SQL_INSERT:
            return executor_execute_insert(&statement->insert);
        case SQL_SELECT:
            return executor_execute_select(&statement->select);
        case SQL_DELETE:
            return executor_execute_delete(&statement->delete_stmt);
        default:
            fprintf(stderr, "Error: Unsupported SQL statement type.\n");
            return FAILURE;
    }
}
