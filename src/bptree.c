#include "bptree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 정렬된 키 배열에서 새 key가 들어갈 위치를 찾는다.
 */
static int bptree_find_insert_position(const long long keys[], int key_count,
                                       long long key) {
    int index;

    index = 0;
    while (index < key_count && keys[index] < key) {
        index++;
    }
    return index;
}

/*
 * 부모 노드의 children 배열에서 child 위치를 찾는다.
 */
static int bptree_find_child_index(BPTreeNode *parent, BPTreeNode *child) {
    int index;

    if (parent == NULL || child == NULL) {
        return FAILURE;
    }

    for (index = 0; index <= parent->key_count; index++) {
        if (parent->children[index] == child) {
            return index;
        }
    }

    return FAILURE;
}

/*
 * 공간이 남아 있는 내부 노드에 분기 키와 오른쪽 자식을 삽입한다.
 */
static int bptree_insert_into_internal(BPTreeNode *node, int insert_index,
                                       long long key,
                                       BPTreeNode *right_child) {
    int index;

    if (node == NULL || insert_index < 0 || insert_index > node->key_count) {
        return FAILURE;
    }

    for (index = node->key_count; index > insert_index; index--) {
        node->keys[index] = node->keys[index - 1];
    }

    for (index = node->key_count + 1; index > insert_index + 1; index--) {
        node->children[index] = node->children[index - 1];
    }

    node->keys[insert_index] = key;
    node->children[insert_index + 1] = right_child;
    node->key_count++;
    if (right_child != NULL) {
        right_child->parent = node;
    }

    return SUCCESS;
}

/*
 * 새 B+ 트리 노드 하나를 할당하고 초기화한다.
 */
BPTreeNode *bptree_create_node(int is_leaf) {
    BPTreeNode *node;

    node = (BPTreeNode *)calloc(1, sizeof(BPTreeNode));
    if (node == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return NULL;
    }

    node->is_leaf = is_leaf;
    return node;
}

/*
 * key가 들어갈 리프 노드를 찾는다.
 */
BPTreeNode *bptree_find_leaf(BPTreeNode *root, long long key) {
    BPTreeNode *current;
    int child_index;

    current = root;
    while (current != NULL && !current->is_leaf) {
        child_index = 0;
        while (child_index < current->key_count &&
               key >= current->keys[child_index]) {
            child_index++;
        }
        current = current->children[child_index];
    }

    return current;
}

/*
 * key를 검색해 대응하는 row_index를 찾는다.
 */
int bptree_search(BPTreeNode *root, long long key, int *row_index) {
    BPTreeNode *leaf;
    int index;

    if (row_index == NULL) {
        return FAILURE;
    }

    *row_index = -1;
    leaf = bptree_find_leaf(root, key);
    if (leaf == NULL) {
        return FAILURE;
    }

    for (index = 0; index < leaf->key_count; index++) {
        if (leaf->keys[index] == key) {
            *row_index = leaf->row_indices[index];
            return SUCCESS;
        }
    }

    return FAILURE;
}

/*
 * 공간이 남아 있는 리프 노드에 key와 row_index를 삽입한다.
 */
int bptree_insert_into_leaf(BPTreeNode *leaf, long long key, int row_index) {
    int insert_index;
    int index;

    if (leaf == NULL || !leaf->is_leaf) {
        return FAILURE;
    }

    insert_index = bptree_find_insert_position(leaf->keys, leaf->key_count, key);
    if (insert_index < leaf->key_count && leaf->keys[insert_index] == key) {
        fprintf(stderr, "Error: Duplicate key '%lld'.\n", key);
        return FAILURE;
    }

    for (index = leaf->key_count; index > insert_index; index--) {
        leaf->keys[index] = leaf->keys[index - 1];
        leaf->row_indices[index] = leaf->row_indices[index - 1];
    }

    leaf->keys[insert_index] = key;
    leaf->row_indices[insert_index] = row_index;
    leaf->key_count++;
    return SUCCESS;
}

/*
 * 가득 찬 리프 노드를 분할하면서 key와 row_index를 삽입한다.
 */
int bptree_split_leaf(BPTreeNode **root, BPTreeNode *leaf,
                      long long key, int row_index) {
    long long temp_keys[BPTREE_MAX_KEYS + 1];
    int temp_rows[BPTREE_MAX_KEYS + 1];
    BPTreeNode *new_leaf;
    int insert_index;
    int split_index;
    int total_keys;
    int index;
    int new_index;

    if (root == NULL || leaf == NULL) {
        return FAILURE;
    }

    total_keys = BPTREE_MAX_KEYS + 1;
    insert_index = bptree_find_insert_position(leaf->keys, leaf->key_count, key);
    if (insert_index < leaf->key_count && leaf->keys[insert_index] == key) {
        fprintf(stderr, "Error: Duplicate key '%lld'.\n", key);
        return FAILURE;
    }

    new_leaf = bptree_create_node(1);
    if (new_leaf == NULL) {
        return FAILURE;
    }

    for (index = 0, new_index = 0; index < total_keys; index++) {
        if (index == insert_index) {
            temp_keys[index] = key;
            temp_rows[index] = row_index;
            continue;
        }

        temp_keys[index] = leaf->keys[new_index];
        temp_rows[index] = leaf->row_indices[new_index];
        new_index++;
    }

    split_index = (total_keys + 1) / 2;
    memset(leaf->keys, 0, sizeof(leaf->keys));
    memset(leaf->row_indices, 0, sizeof(leaf->row_indices));
    leaf->key_count = 0;

    for (index = 0; index < split_index; index++) {
        leaf->keys[index] = temp_keys[index];
        leaf->row_indices[index] = temp_rows[index];
        leaf->key_count++;
    }

    for (index = split_index, new_index = 0; index < total_keys; index++, new_index++) {
        new_leaf->keys[new_index] = temp_keys[index];
        new_leaf->row_indices[new_index] = temp_rows[index];
        new_leaf->key_count++;
    }

    new_leaf->next = leaf->next;
    leaf->next = new_leaf;
    new_leaf->parent = leaf->parent;

    return bptree_insert_into_parent(root, leaf, new_leaf->keys[0], new_leaf);
}

/*
 * 자식 분할 결과를 부모 노드에 반영한다.
 */
int bptree_insert_into_parent(BPTreeNode **root, BPTreeNode *left,
                              long long key, BPTreeNode *right) {
    BPTreeNode *parent;
    BPTreeNode *new_root;
    int insert_index;

    if (root == NULL || left == NULL || right == NULL) {
        return FAILURE;
    }

    parent = left->parent;
    if (parent == NULL) {
        new_root = bptree_create_node(0);
        if (new_root == NULL) {
            return FAILURE;
        }

        new_root->keys[0] = key;
        new_root->children[0] = left;
        new_root->children[1] = right;
        new_root->key_count = 1;
        left->parent = new_root;
        right->parent = new_root;
        *root = new_root;
        return SUCCESS;
    }

    insert_index = bptree_find_child_index(parent, left);
    if (insert_index == FAILURE) {
        return FAILURE;
    }

    if (parent->key_count < BPTREE_MAX_KEYS) {
        return bptree_insert_into_internal(parent, insert_index, key, right);
    }

    return bptree_split_internal(root, parent, insert_index, key, right);
}

/*
 * 가득 찬 내부 노드에 새 분기 정보를 넣기 위해 내부 노드를 분할한다.
 */
int bptree_split_internal(BPTreeNode **root, BPTreeNode *node,
                          int insert_index, long long key,
                          BPTreeNode *right_child) {
    long long temp_keys[BPTREE_MAX_KEYS + 1];
    BPTreeNode *temp_children[BPTREE_MAX_KEYS + 2];
    BPTreeNode *new_internal;
    long long promote_key;
    int total_keys;
    int promote_index;
    int index;
    int new_index;

    if (root == NULL || node == NULL) {
        return FAILURE;
    }

    new_internal = bptree_create_node(0);
    if (new_internal == NULL) {
        return FAILURE;
    }

    total_keys = BPTREE_MAX_KEYS + 1;
    for (index = 0, new_index = 0; index < total_keys + 1; index++) {
        if (index == insert_index + 1) {
            temp_children[index] = right_child;
            continue;
        }

        temp_children[index] = node->children[new_index];
        new_index++;
    }

    for (index = 0, new_index = 0; index < total_keys; index++) {
        if (index == insert_index) {
            temp_keys[index] = key;
            continue;
        }

        temp_keys[index] = node->keys[new_index];
        new_index++;
    }

    promote_index = total_keys / 2;
    promote_key = temp_keys[promote_index];

    memset(node->keys, 0, sizeof(node->keys));
    memset(node->children, 0, sizeof(node->children));
    node->key_count = 0;

    for (index = 0; index < promote_index; index++) {
        node->keys[index] = temp_keys[index];
        node->children[index] = temp_children[index];
        if (node->children[index] != NULL) {
            node->children[index]->parent = node;
        }
        node->key_count++;
    }

    node->children[promote_index] = temp_children[promote_index];
    if (node->children[promote_index] != NULL) {
        node->children[promote_index]->parent = node;
    }

    for (index = promote_index + 1, new_index = 0; index < total_keys;
         index++, new_index++) {
        new_internal->keys[new_index] = temp_keys[index];
        new_internal->children[new_index] = temp_children[index];
        if (new_internal->children[new_index] != NULL) {
            new_internal->children[new_index]->parent = new_internal;
        }
        new_internal->key_count++;
    }

    new_internal->children[new_internal->key_count] = temp_children[total_keys];
    if (new_internal->children[new_internal->key_count] != NULL) {
        new_internal->children[new_internal->key_count]->parent = new_internal;
    }

    new_internal->parent = node->parent;
    return bptree_insert_into_parent(root, node, promote_key, new_internal);
}

/*
 * key와 row_index를 B+ 트리에 삽입한다.
 */
int bptree_insert(BPTreeNode **root, long long key, int row_index) {
    BPTreeNode *leaf;

    if (root == NULL) {
        return FAILURE;
    }

    if (*root == NULL) {
        *root = bptree_create_node(1);
        if (*root == NULL) {
            return FAILURE;
        }
        (*root)->keys[0] = key;
        (*root)->row_indices[0] = row_index;
        (*root)->key_count = 1;
        return SUCCESS;
    }

    leaf = bptree_find_leaf(*root, key);
    if (leaf == NULL) {
        return FAILURE;
    }

    if (leaf->key_count < BPTREE_MAX_KEYS) {
        return bptree_insert_into_leaf(leaf, key, row_index);
    }

    return bptree_split_leaf(root, leaf, key, row_index);
}

/*
 * 루트 이하의 모든 노드를 재귀적으로 해제한다.
 */
void bptree_free(BPTreeNode *root) {
    int index;

    if (root == NULL) {
        return;
    }

    if (!root->is_leaf) {
        for (index = 0; index <= root->key_count; index++) {
            bptree_free(root->children[index]);
        }
    }

    free(root);
}
