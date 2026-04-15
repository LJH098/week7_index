#include "executor.h"
#include "bptree.h"
#include "table_runtime.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * 테스트 조건을 검사하고 실패 시 사람이 읽기 쉬운 메시지를 출력하는 함수다.
 * 각 검증 단계의 실패 이유를 바로 확인할 수 있게 공통 포맷으로 사용한다.
 */
static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

/*
 * id를 제외한 일반 INSERT 문 구조체를 준비하는 함수다.
 * executor의 메모리 런타임 삽입 경로를 검증할 때 기본 입력으로 사용한다.
 */
static void prepare_insert_without_id(SqlStatement *statement, const char *table_name,
                                      const char *name, const char *age) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_INSERT;
    snprintf(statement->insert.table_name, sizeof(statement->insert.table_name),
             "%s", table_name);
    statement->insert.column_count = 2;
    snprintf(statement->insert.columns[0], sizeof(statement->insert.columns[0]), "name");
    snprintf(statement->insert.columns[1], sizeof(statement->insert.columns[1]), "age");
    snprintf(statement->insert.values[0], sizeof(statement->insert.values[0]), "%s", name);
    snprintf(statement->insert.values[1], sizeof(statement->insert.values[1]), "%s", age);
}

/*
 * 사용자가 id 컬럼을 직접 넣으려는 INSERT 문 구조체를 준비하는 함수다.
 * 메모리 런타임 정책상 거부되어야 하는 경로를 executor 레벨에서 검증할 때 사용한다.
 */
static void prepare_insert_with_id(SqlStatement *statement, const char *table_name,
                                   const char *id, const char *name,
                                   const char *age) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_INSERT;
    snprintf(statement->insert.table_name, sizeof(statement->insert.table_name),
             "%s", table_name);
    statement->insert.column_count = 3;
    snprintf(statement->insert.columns[0], sizeof(statement->insert.columns[0]), "id");
    snprintf(statement->insert.columns[1], sizeof(statement->insert.columns[1]), "name");
    snprintf(statement->insert.columns[2], sizeof(statement->insert.columns[2]), "age");
    snprintf(statement->insert.values[0], sizeof(statement->insert.values[0]), "%s", id);
    snprintf(statement->insert.values[1], sizeof(statement->insert.values[1]), "%s", name);
    snprintf(statement->insert.values[2], sizeof(statement->insert.values[2]), "%s", age);
}

/*
 * WHERE 없는 SELECT 문 구조체를 준비하는 함수다.
 * 전체 행 조회가 메모리 런타임 기준으로 동작하는지 확인할 때 사용한다.
 */
static void prepare_select_all(SqlStatement *statement, const char *table_name) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_SELECT;
    snprintf(statement->select.table_name, sizeof(statement->select.table_name),
             "%s", table_name);
    statement->select.column_count = 0;
    statement->select.has_where = 0;
}

/*
 * WHERE가 있는 SELECT 문 구조체를 준비하는 함수다.
 * id 인덱스 경로와 선형 탐색 경로를 각각 테스트할 때 공통으로 사용한다.
 */
static void prepare_select_where(SqlStatement *statement, const char *table_name,
                                 const char *column, const char *op,
                                 const char *value) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_SELECT;
    snprintf(statement->select.table_name, sizeof(statement->select.table_name),
             "%s", table_name);
    statement->select.column_count = 0;
    statement->select.has_where = 1;
    snprintf(statement->select.where.column, sizeof(statement->select.where.column),
             "%s", column);
    snprintf(statement->select.where.op, sizeof(statement->select.where.op),
             "%s", op);
    snprintf(statement->select.where.value, sizeof(statement->select.where.value),
             "%s", value);
}

/*
 * DELETE 문 구조체를 준비하는 함수다.
 * 현재 단계에서 DELETE가 명시적으로 비지원인지 확인할 때 사용한다.
 */
static void prepare_delete(SqlStatement *statement, const char *table_name,
                           const char *column, const char *op,
                           const char *value) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_DELETE;
    snprintf(statement->delete_stmt.table_name,
             sizeof(statement->delete_stmt.table_name), "%s", table_name);
    statement->delete_stmt.has_where = 1;
    snprintf(statement->delete_stmt.where.column,
             sizeof(statement->delete_stmt.where.column), "%s", column);
    snprintf(statement->delete_stmt.where.op,
             sizeof(statement->delete_stmt.where.op), "%s", op);
    snprintf(statement->delete_stmt.where.value,
             sizeof(statement->delete_stmt.where.value), "%s", value);
}

/*
 * executor_execute()가 표준 출력에 찍는 내용을 임시 파일로 캡처하는 함수다.
 * SELECT 결과에 특정 행이 포함됐는지 확인해 인덱스 경로와 선형 탐색 경로를 간접 검증한다.
 */
static int capture_executor_stdout(const SqlStatement *statement,
                                   char *buffer, size_t buffer_size) {
    FILE *temp;
    int saved_stdout;
    size_t read_size;
    int status;

    if (statement == NULL || buffer == NULL || buffer_size == 0) {
        return FAILURE;
    }

    fflush(stdout);
    saved_stdout = dup(fileno(stdout));
    if (saved_stdout < 0) {
        return FAILURE;
    }

    temp = tmpfile();
    if (temp == NULL) {
        close(saved_stdout);
        return FAILURE;
    }

    if (dup2(fileno(temp), fileno(stdout)) < 0) {
        fclose(temp);
        close(saved_stdout);
        return FAILURE;
    }

    status = executor_execute(statement);
    fflush(stdout);
    rewind(temp);

    read_size = fread(buffer, 1, buffer_size - 1, temp);
    buffer[read_size] = '\0';

    dup2(saved_stdout, fileno(stdout));
    close(saved_stdout);
    fclose(temp);
    return status;
}

/*
 * executor의 메모리 런타임 INSERT/SELECT 분기와 DELETE 비지원 정책을 검증하는 함수다.
 * id 검색은 B+ 트리, 비-id 조건은 선형 탐색으로 흐르는지 결과 출력까지 함께 확인한다.
 */
int main(void) {
    SqlStatement statement;
    TableRuntime *table;
    char output[4096];
    int indexed_row_index;
    char *replacement_id;

    table_runtime_cleanup();

    prepare_insert_without_id(&statement, "executor_users", "Alice", "30");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert first row into runtime table") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_insert_without_id(&statement, "executor_users", "Bob", "25");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert second row into runtime table") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    if (assert_true(table_get_or_load("executor_users", &table) == SUCCESS,
                    "executor test should access active runtime table") != SUCCESS ||
        assert_true(table->row_count == 2,
                    "runtime table should contain two inserted rows") != SUCCESS ||
        assert_true(bptree_search(table->id_index_root, 1, &indexed_row_index) == SUCCESS &&
                        indexed_row_index == 0,
                    "first generated id should be indexed at row 0") != SUCCESS ||
        assert_true(bptree_search(table->id_index_root, 2, &indexed_row_index) == SUCCESS &&
                        indexed_row_index == 1,
                    "second generated id should be indexed at row 1") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_insert_with_id(&statement, "executor_users", "99", "Mallory", "44");
    if (assert_true(executor_execute(&statement) == FAILURE,
                    "executor should reject explicit id input in memory runtime mode") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    replacement_id = utils_strdup("999");
    if (assert_true(replacement_id != NULL,
                    "test should allocate replacement id string") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }
    free(table->rows[1][0]);
    table->rows[1][0] = replacement_id;

    prepare_select_where(&statement, "executor_users", "id", "=", "2");
    if (assert_true(capture_executor_stdout(&statement, output, sizeof(output)) == SUCCESS,
                    "executor should execute WHERE id = value select") != SUCCESS ||
        assert_true(strstr(output, "Bob") != NULL,
                    "id select should still return Bob through B+ tree lookup") != SUCCESS ||
        assert_true(strstr(output, "1 row selected.") != NULL,
                    "id select should report one selected row") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_select_where(&statement, "executor_users", "age", ">=", "27");
    if (assert_true(capture_executor_stdout(&statement, output, sizeof(output)) == SUCCESS,
                    "executor should execute scan-based WHERE select") != SUCCESS ||
        assert_true(strstr(output, "Alice") != NULL,
                    "scan select should include Alice when age >= 27") != SUCCESS ||
        assert_true(strstr(output, "Bob") == NULL,
                    "scan select should exclude Bob when age >= 27") != SUCCESS ||
        assert_true(strstr(output, "1 row selected.") != NULL,
                    "scan select should report one selected row") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_select_all(&statement, "executor_users");
    if (assert_true(capture_executor_stdout(&statement, output, sizeof(output)) == SUCCESS,
                    "executor should execute full table select from runtime") != SUCCESS ||
        assert_true(strstr(output, "Alice") != NULL && strstr(output, "Bob") != NULL,
                    "full select should include both inserted names") != SUCCESS ||
        assert_true(strstr(output, "2 rows selected.") != NULL,
                    "full select should report two selected rows") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_delete(&statement, "executor_users", "name", "=", "Bob");
    if (assert_true(executor_execute(&statement) == FAILURE,
                    "executor should reject DELETE in memory runtime mode") != SUCCESS ||
        assert_true(table->row_count == 2,
                    "DELETE rejection should leave runtime rows unchanged") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    table_runtime_cleanup();
    puts("[PASS] executor");
    return EXIT_SUCCESS;
}
