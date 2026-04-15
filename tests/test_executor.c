#include "bptree.h"
#include "executor.h"
#include "table_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

static void prepare_insert(SqlStatement *statement, const char *table_name,
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

static void prepare_select_where(SqlStatement *statement, const char *table_name,
                                 int column_count, const char *selected_column,
                                 const char *where_column, const char *op,
                                 const char *value) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_SELECT;
    snprintf(statement->select.table_name, sizeof(statement->select.table_name),
             "%s", table_name);
    statement->select.column_count = column_count;
    if (column_count == 1) {
        snprintf(statement->select.columns[0],
                 sizeof(statement->select.columns[0]), "%s", selected_column);
    }
    statement->select.has_where = 1;
    snprintf(statement->select.where.column, sizeof(statement->select.where.column),
             "%s", where_column);
    snprintf(statement->select.where.op, sizeof(statement->select.where.op), "%s", op);
    snprintf(statement->select.where.value, sizeof(statement->select.where.value),
             "%s", value);
}

static void prepare_delete(SqlStatement *statement, const char *table_name,
                           const char *name) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_DELETE;
    snprintf(statement->delete_stmt.table_name,
             sizeof(statement->delete_stmt.table_name), "%s", table_name);
    statement->delete_stmt.has_where = 1;
    snprintf(statement->delete_stmt.where.column,
             sizeof(statement->delete_stmt.where.column), "name");
    snprintf(statement->delete_stmt.where.op,
             sizeof(statement->delete_stmt.where.op), "=");
    snprintf(statement->delete_stmt.where.value,
             sizeof(statement->delete_stmt.where.value), "%s", name);
}

int main(void) {
    SqlStatement statement;
    TableRuntime *table;
    int row_index;

    table_runtime_cleanup();

    prepare_insert(&statement, "executor_users", "Alice", "30");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert first row with auto id") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_insert(&statement, "executor_users", "Bob", "25");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert second row with auto id") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_insert_with_id(&statement, "executor_users", "7", "Charlie", "40");
    if (assert_true(executor_execute(&statement) == FAILURE,
                    "executor should reject explicit id values") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_select_where(&statement, "executor_users", 1, "name", "id", "=", "1");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should select by id using B+ tree") != SUCCESS) {
        return EXIT_FAILURE;
    }

    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should repeat id select consistently") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_select_where(&statement, "executor_users", 1, "name", "name", "=",
                         "Bob");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should select by non-id field with linear scan") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_select_where(&statement, "executor_users", 1, "name", "age", ">=",
                         "27");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should handle non-id range scan linearly") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_delete(&statement, "executor_users", "Bob");
    if (assert_true(executor_execute(&statement) == FAILURE,
                    "executor should reject DELETE in memory runtime mode") != SUCCESS) {
        return EXIT_FAILURE;
    }

    table = table_get_or_load("executor_users");
    if (assert_true(table != NULL, "executor runtime table should remain available") != SUCCESS ||
        assert_true(table->row_count == 2, "runtime should keep two inserted rows") != SUCCESS ||
        assert_true(bptree_search(table->id_index_root, 2, &row_index) == SUCCESS,
                    "B+ tree should contain id 2") != SUCCESS ||
        assert_true(row_index == 1, "id 2 should point to second row") != SUCCESS ||
        assert_true(strcmp(table->rows[0][1], "Alice") == 0,
                    "first runtime row should keep Alice") != SUCCESS ||
        assert_true(strcmp(table->rows[1][1], "Bob") == 0,
                    "second runtime row should keep Bob") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    table_runtime_cleanup();
    puts("[PASS] executor");
    return EXIT_SUCCESS;
}
