#include "executor.h"
#include "bptree.h"
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

static void prepare_select(SqlStatement *statement, const char *table_name) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_SELECT;
    snprintf(statement->select.table_name, sizeof(statement->select.table_name),
             "%s", table_name);
    statement->select.column_count = 1;
    snprintf(statement->select.columns[0], sizeof(statement->select.columns[0]), "name");
    statement->select.has_where = 1;
    snprintf(statement->select.where.column, sizeof(statement->select.where.column), "age");
    snprintf(statement->select.where.op, sizeof(statement->select.where.op), ">=");
    snprintf(statement->select.where.value, sizeof(statement->select.where.value), "27");
}

static void prepare_select_by_id(SqlStatement *statement, const char *table_name,
                                 const char *id_value) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_SELECT;
    snprintf(statement->select.table_name, sizeof(statement->select.table_name),
             "%s", table_name);
    statement->select.column_count = 1;
    snprintf(statement->select.columns[0], sizeof(statement->select.columns[0]), "name");
    statement->select.has_where = 1;
    snprintf(statement->select.where.column, sizeof(statement->select.where.column), "id");
    snprintf(statement->select.where.op, sizeof(statement->select.where.op), "=");
    snprintf(statement->select.where.value, sizeof(statement->select.where.value), "%s",
             id_value);
}

static void prepare_insert_with_id(SqlStatement *statement, const char *table_name,
                                   const char *id, const char *name) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_INSERT;
    snprintf(statement->insert.table_name, sizeof(statement->insert.table_name),
             "%s", table_name);
    statement->insert.column_count = 2;
    snprintf(statement->insert.columns[0], sizeof(statement->insert.columns[0]), "id");
    snprintf(statement->insert.columns[1], sizeof(statement->insert.columns[1]), "name");
    snprintf(statement->insert.values[0], sizeof(statement->insert.values[0]), "%s", id);
    snprintf(statement->insert.values[1], sizeof(statement->insert.values[1]), "%s", name);
}

int main(void) {
    SqlStatement statement;
    TableRuntime *table;
    int row_index;

    executor_cleanup();

    prepare_insert(&statement, "executor_users", "Alice", "30");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert row with auto id") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_insert(&statement, "executor_users", "Bob", "25");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert second row with auto id") != SUCCESS) {
        return EXIT_FAILURE;
    }

    table = table_get_or_load("executor_users");
    if (assert_true(table != NULL && table->loaded,
                    "table_get_or_load should expose loaded executor runtime") != SUCCESS ||
        assert_true(table->row_count == 2,
                    "executor runtime should contain two rows") != SUCCESS ||
        assert_true(table->next_id == 3,
                    "executor runtime should advance next id") != SUCCESS ||
        assert_true(strcmp(table->rows[0][1], "Alice") == 0,
                    "first runtime row should contain Alice") != SUCCESS ||
        assert_true(bptree_search(table->id_index_root, 2, &row_index) == SUCCESS,
                    "executor should register inserted ids in B+ tree") != SUCCESS ||
        assert_true(row_index == 1, "id 2 should point to second row") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_select_by_id(&statement, "executor_users", "1");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should execute id-indexed SELECT") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_select(&statement, "executor_users");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should execute linear scan SELECT") != SUCCESS) {
        return EXIT_FAILURE;
    }

    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should execute repeated SELECT consistently") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_insert_with_id(&statement, "executor_users", "99", "Manual");
    if (assert_true(executor_execute(&statement) == FAILURE,
                    "executor should reject manual id insertion") != SUCCESS) {
        return EXIT_FAILURE;
    }

    executor_cleanup();
    puts("[PASS] executor");
    return EXIT_SUCCESS;
}
