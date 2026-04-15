#include "executor.h"

#include "bptree.h"
#include "table_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 컬럼 이름을 대소문자 무시로 찾는다.
 */
static int executor_find_column_index(const char columns[][MAX_IDENTIFIER_LEN],
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
 * 결과 셀 문자열 하나를 복제한다.
 */
static char *executor_duplicate_cell(const char *value) {
    return utils_strdup(value == NULL ? "" : value);
}

/*
 * SELECT 결과를 담을 바깥쪽 행 배열을 할당한다.
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
 * 원본 행에서 선택된 컬럼만 복사해 결과 행으로 만든다.
 */
static int executor_copy_projected_row(char ***result_rows, int result_index,
                                       char **source_row,
                                       const int *selected_indices,
                                       int selected_count) {
    int index;
    int cleanup_index;

    result_rows[result_index] =
        (char **)malloc((size_t)selected_count * sizeof(char *));
    if (result_rows[result_index] == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    for (index = 0; index < selected_count; index++) {
        result_rows[result_index][index] =
            executor_duplicate_cell(source_row[selected_indices[index]]);
        if (result_rows[result_index][index] == NULL) {
            for (cleanup_index = 0; cleanup_index < index; cleanup_index++) {
                free(result_rows[result_index][cleanup_index]);
            }
            free(result_rows[result_index]);
            result_rows[result_index] = NULL;
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * executor 내부 헬퍼가 만든 조회 결과 테이블을 해제한다.
 */
static void executor_free_result_rows(char ***rows, int row_count, int col_count) {
    int row_index;
    int col_index;

    if (rows == NULL) {
        return;
    }

    for (row_index = 0; row_index < row_count; row_index++) {
        if (rows[row_index] == NULL) {
            continue;
        }
        for (col_index = 0; col_index < col_count; col_index++) {
            free(rows[row_index][col_index]);
        }
        free(rows[row_index]);
    }

    free(rows);
}

/*
 * SELECT 표 출력용 가로 경계선을 한 줄 출력한다.
 */
static void executor_print_border(const int *widths, int col_count) {
    int row_index;
    int col_index;

    for (row_index = 0; row_index < col_count; row_index++) {
        putchar('+');
        for (col_index = 0; col_index < widths[row_index] + 2; col_index++) {
            putchar('-');
        }
    }
    puts("+");
}

/*
 * 표시 폭을 고려해 표 형태로 조회 결과를 출력한다.
 */
static void executor_print_table(char headers[][MAX_IDENTIFIER_LEN], int header_count,
                                 char ***rows, int row_count) {
    int widths[MAX_COLUMNS];
    int row_index;
    int col_index;
    int cell_width;

    for (col_index = 0; col_index < header_count; col_index++) {
        widths[col_index] = utils_display_width(headers[col_index]);
    }

    for (row_index = 0; row_index < row_count; row_index++) {
        for (col_index = 0; col_index < header_count; col_index++) {
            cell_width = utils_display_width(rows[row_index][col_index]);
            if (cell_width > widths[col_index]) {
                widths[col_index] = cell_width;
            }
        }
    }

    executor_print_border(widths, header_count);
    for (col_index = 0; col_index < header_count; col_index++) {
        printf("| ");
        utils_print_padded(stdout, headers[col_index], widths[col_index]);
        putchar(' ');
    }
    puts("|");
    executor_print_border(widths, header_count);

    for (row_index = 0; row_index < row_count; row_index++) {
        for (col_index = 0; col_index < header_count; col_index++) {
            printf("| ");
            utils_print_padded(stdout, rows[row_index][col_index], widths[col_index]);
            putchar(' ');
        }
        puts("|");
    }

    executor_print_border(widths, header_count);
}

/*
 * SELECT 대상 컬럼을 원본 테이블 인덱스와 출력 헤더로 변환한다.
 */
static int executor_prepare_projection(const SelectStatement *stmt,
                                       const TableRuntime *table,
                                       int selected_indices[],
                                       char headers[][MAX_IDENTIFIER_LEN],
                                       int *selected_count) {
    int index;
    int column_index;

    if (stmt == NULL || table == NULL || selected_indices == NULL ||
        headers == NULL || selected_count == NULL) {
        return FAILURE;
    }

    if (stmt->column_count == 0) {
        for (index = 0; index < table->col_count; index++) {
            selected_indices[index] = index;
            if (utils_safe_strcpy(headers[index], sizeof(headers[index]),
                                  table->columns[index]) != SUCCESS) {
                fprintf(stderr, "Error: Column name is too long.\n");
                return FAILURE;
            }
        }
        *selected_count = table->col_count;
        return SUCCESS;
    }

    for (index = 0; index < stmt->column_count; index++) {
        column_index = executor_find_column_index(table->columns, table->col_count,
                                                  stmt->columns[index]);
        if (column_index == FAILURE) {
            fprintf(stderr, "Error: Column '%s' not found.\n", stmt->columns[index]);
            return FAILURE;
        }

        selected_indices[index] = column_index;
        if (utils_safe_strcpy(headers[index], sizeof(headers[index]),
                              table->columns[column_index]) != SUCCESS) {
            fprintf(stderr, "Error: Column name is too long.\n");
            return FAILURE;
        }
    }

    *selected_count = stmt->column_count;
    return SUCCESS;
}

/*
 * 지정된 row_index 집합에서 projection 결과 행 배열을 만든다.
 */
static int executor_collect_rows_from_slots(const TableRuntime *table,
                                            const int *selected_indices,
                                            int selected_count,
                                            const int *slots, int slot_count,
                                            char ****out_rows, int *out_row_count) {
    char ***result_rows;
    char **source_row;
    int index;

    if (table == NULL || selected_indices == NULL || slots == NULL ||
        out_rows == NULL || out_row_count == NULL) {
        return FAILURE;
    }

    if (executor_allocate_result_rows(&result_rows, slot_count) != SUCCESS) {
        return FAILURE;
    }

    for (index = 0; index < slot_count; index++) {
        source_row = table_get_row_by_slot(table, slots[index]);
        if (source_row == NULL) {
            executor_free_result_rows(result_rows, index, selected_count);
            return FAILURE;
        }

        if (executor_copy_projected_row(result_rows, index, source_row,
                                        selected_indices, selected_count) != SUCCESS) {
            executor_free_result_rows(result_rows, index, selected_count);
            return FAILURE;
        }
    }

    *out_rows = result_rows;
    *out_row_count = slot_count;
    return SUCCESS;
}

/*
 * WHERE가 없는 SELECT를 위해 모든 행을 projection 결과로 복사한다.
 */
static int executor_collect_all_rows(const TableRuntime *table,
                                     const int *selected_indices,
                                     int selected_count,
                                     char ****out_rows, int *out_row_count) {
    int *slots;
    int index;
    int status;

    if (table == NULL || selected_indices == NULL || out_rows == NULL ||
        out_row_count == NULL) {
        return FAILURE;
    }

    if (table->row_count == 0) {
        *out_rows = NULL;
        *out_row_count = 0;
        return SUCCESS;
    }

    slots = (int *)malloc((size_t)table->row_count * sizeof(int));
    if (slots == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    for (index = 0; index < table->row_count; index++) {
        slots[index] = index;
    }

    status = executor_collect_rows_from_slots(table, selected_indices,
                                              selected_count, slots,
                                              table->row_count,
                                              out_rows, out_row_count);
    free(slots);
    return status;
}

/*
 * WHERE 절이 `id = 정수` 형태면 B+ 트리를 사용할 수 있다.
 */
static int executor_can_use_id_index(const SelectStatement *stmt) {
    if (stmt == NULL || !stmt->has_where) {
        return 0;
    }

    return utils_equals_ignore_case(stmt->where.column, "id") &&
           strcmp(stmt->where.op, "=") == 0 &&
           utils_is_integer(stmt->where.value);
}

/*
 * `WHERE id = 정수` 경로를 B+ 트리 검색으로 실행한다.
 */
static int executor_select_by_id(const SelectStatement *stmt,
                                 const TableRuntime *table,
                                 const int *selected_indices,
                                 int selected_count,
                                 char ****out_rows, int *out_row_count) {
    int slots[1];
    int row_index;
    long long lookup_id;

    if (stmt == NULL || table == NULL || selected_indices == NULL ||
        out_rows == NULL || out_row_count == NULL) {
        return FAILURE;
    }

    lookup_id = utils_parse_integer(stmt->where.value);
    if (bptree_search(table->id_index_root, lookup_id, &row_index) != SUCCESS) {
        *out_rows = NULL;
        *out_row_count = 0;
        return SUCCESS;
    }

    slots[0] = row_index;
    return executor_collect_rows_from_slots(table, selected_indices,
                                            selected_count, slots, 1,
                                            out_rows, out_row_count);
}

/*
 * `WHERE`가 없거나 id 이외 조건인 SELECT를 선형 탐색으로 실행한다.
 */
static int executor_select_by_scan(const SelectStatement *stmt,
                                   const TableRuntime *table,
                                   const int *selected_indices,
                                   int selected_count,
                                   char ****out_rows, int *out_row_count) {
    int *matching_slots;
    int match_count;
    int status;

    if (stmt == NULL || table == NULL || selected_indices == NULL ||
        out_rows == NULL || out_row_count == NULL) {
        return FAILURE;
    }

    if (!stmt->has_where) {
        return executor_collect_all_rows(table, selected_indices, selected_count,
                                         out_rows, out_row_count);
    }

    matching_slots = NULL;
    match_count = 0;
    status = table_linear_scan_by_field(table, &stmt->where,
                                        &matching_slots, &match_count);
    if (status != SUCCESS) {
        return FAILURE;
    }

    if (match_count == 0) {
        *out_rows = NULL;
        *out_row_count = 0;
        return SUCCESS;
    }

    status = executor_collect_rows_from_slots(table, selected_indices,
                                              selected_count, matching_slots,
                                              match_count, out_rows,
                                              out_row_count);
    free(matching_slots);
    return status;
}

/*
 * INSERT 문 하나를 메모리 런타임과 B+ 트리에 반영한다.
 */
static int executor_execute_insert(const InsertStatement *stmt) {
    TableRuntime *table;
    int column_index;

    if (stmt == NULL) {
        return FAILURE;
    }

    for (column_index = 0; column_index < stmt->column_count; column_index++) {
        if (utils_equals_ignore_case(stmt->columns[column_index], "id")) {
            fprintf(stderr, "Error: Manual id insertion is not allowed.\n");
            return FAILURE;
        }
    }

    table = table_get_or_load(stmt->table_name);
    if (table == NULL) {
        return FAILURE;
    }

    if (table_insert_row(table, stmt->columns, stmt->values,
                         stmt->column_count, 1, NULL) != SUCCESS) {
        return FAILURE;
    }

    printf("1 row inserted into %s.\n", stmt->table_name);
    return SUCCESS;
}

/*
 * SELECT 문 하나를 실행하고 표 형태로 출력한다.
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

    table = table_get_or_load(stmt->table_name);
    if (table == NULL) {
        return FAILURE;
    }

    if (!table->loaded) {
        fprintf(stderr, "Error: Table '%s' is not loaded in memory.\n",
                stmt->table_name);
        return FAILURE;
    }

    status = executor_prepare_projection(stmt, table, selected_indices, headers,
                                         &selected_count);
    if (status != SUCCESS) {
        return FAILURE;
    }

    result_rows = NULL;
    result_row_count = 0;
    if (executor_can_use_id_index(stmt)) {
        status = executor_select_by_id(stmt, table, selected_indices,
                                       selected_count, &result_rows,
                                       &result_row_count);
    } else {
        status = executor_select_by_scan(stmt, table, selected_indices,
                                         selected_count, &result_rows,
                                         &result_row_count);
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
 * DELETE는 이번 메모리 런타임 통합 범위에서 지원하지 않는다.
 */
static int executor_execute_delete(const DeleteStatement *stmt) {
    if (stmt == NULL) {
        return FAILURE;
    }

    fprintf(stderr, "Error: DELETE is not supported in the in-memory runtime.\n");
    return FAILURE;
}

/*
 * 파싱된 SQL 문을 받아 statement.type에 따라 INSERT, SELECT, DELETE로 분기한다.
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

/*
 * executor가 유지하는 인메모리 런타임을 정리한다.
 */
void executor_cleanup(void) {
    table_cleanup_active();
}
