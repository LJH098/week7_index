#include "bptree.h"

#include <stdio.h>
#include <stdlib.h>

static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

int main(void) {
    BPTreeNode *root;
    int row_index;
    int key;

    root = NULL;
    for (key = 1; key <= 20; key++) {
        if (assert_true(bptree_insert(&root, key, key - 1) == SUCCESS,
                        "bptree_insert should accept sequential keys") != SUCCESS) {
            bptree_free(root);
            return EXIT_FAILURE;
        }
    }

    for (key = 1; key <= 20; key++) {
        if (assert_true(bptree_search(root, key, &row_index) == SUCCESS,
                        "bptree_search should find inserted key") != SUCCESS ||
            assert_true(row_index == key - 1,
                        "bptree_search should return stored row index") != SUCCESS) {
            bptree_free(root);
            return EXIT_FAILURE;
        }
    }

    if (assert_true(root != NULL && !root->is_leaf,
                    "many inserts should split the root") != SUCCESS ||
        assert_true(bptree_search(root, 999, &row_index) == FAILURE,
                    "bptree_search should fail for missing key") != SUCCESS ||
        assert_true(bptree_insert(&root, 10, 123) == FAILURE,
                    "bptree_insert should reject duplicate keys") != SUCCESS) {
        bptree_free(root);
        return EXIT_FAILURE;
    }

    bptree_free(root);
    puts("[PASS] bptree");
    return EXIT_SUCCESS;
}
