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

static int insert_range(BPTreeNode **root, int start, int end) {
    int key;

    for (key = start; key <= end; key++) {
        if (bptree_insert(root, key, key * 10) != SUCCESS) {
            return FAILURE;
        }
    }

    return SUCCESS;
}

int main(void) {
    BPTreeNode *root;
    int row_index;
    int key;

    root = NULL;
    if (assert_true(bptree_insert(&root, 10, 100) == SUCCESS,
                    "first insert should create root leaf") != SUCCESS ||
        assert_true(bptree_insert(&root, 20, 200) == SUCCESS,
                    "second insert should succeed") != SUCCESS ||
        assert_true(bptree_search(root, 10, &row_index) == SUCCESS,
                    "search should find first key") != SUCCESS ||
        assert_true(row_index == 100, "search should return stored row_index") != SUCCESS ||
        assert_true(bptree_insert(&root, 10, 999) == FAILURE,
                    "duplicate keys should be rejected") != SUCCESS) {
        bptree_free(root);
        return EXIT_FAILURE;
    }
    bptree_free(root);

    root = NULL;
    if (assert_true(insert_range(&root, 1, 4) == SUCCESS,
                    "four inserts should trigger leaf split") != SUCCESS ||
        assert_true(root != NULL && !root->is_leaf,
                    "root should become internal after leaf split") != SUCCESS ||
        assert_true(root->key_count == 1,
                    "root should contain one separator after first split") != SUCCESS ||
        assert_true(root->children[0] != NULL && root->children[1] != NULL,
                    "split root should point to two leaves") != SUCCESS) {
        bptree_free(root);
        return EXIT_FAILURE;
    }
    bptree_free(root);

    root = NULL;
    if (assert_true(insert_range(&root, 1, 13) == SUCCESS,
                    "thirteen inserts should trigger internal split") != SUCCESS ||
        assert_true(root != NULL && !root->is_leaf,
                    "root should remain internal after many inserts") != SUCCESS ||
        assert_true(root->children[0] != NULL && !root->children[0]->is_leaf,
                    "height should grow after internal split") != SUCCESS) {
        bptree_free(root);
        return EXIT_FAILURE;
    }

    for (key = 1; key <= 13; key++) {
        if (assert_true(bptree_search(root, key, &row_index) == SUCCESS,
                        "all inserted keys should remain searchable") != SUCCESS ||
            assert_true(row_index == key * 10,
                        "stored row_index should match inserted value") != SUCCESS) {
            bptree_free(root);
            return EXIT_FAILURE;
        }
    }
    bptree_free(root);

    root = NULL;
    if (assert_true(insert_range(&root, 1, 50) == SUCCESS,
                    "bulk insert should succeed") != SUCCESS) {
        bptree_free(root);
        return EXIT_FAILURE;
    }

    for (key = 1; key <= 50; key++) {
        if (assert_true(bptree_search(root, key, &row_index) == SUCCESS,
                        "bulk search should find every key") != SUCCESS ||
            assert_true(row_index == key * 10,
                        "bulk search should return correct row_index") != SUCCESS) {
            bptree_free(root);
            return EXIT_FAILURE;
        }
    }

    if (assert_true(bptree_search(root, 999, &row_index) == FAILURE,
                    "search should fail for missing keys") != SUCCESS) {
        bptree_free(root);
        return EXIT_FAILURE;
    }

    bptree_free(root);
    puts("[PASS] bptree");
    return EXIT_SUCCESS;
}
