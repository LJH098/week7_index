#include "bptree.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * 리프 내부 삽입 위치나 내부 노드 자식 분기 위치를 계산할 때 사용할 lower bound 함수다.
 * key 이상이 처음 나오는 위치를 반환해 정렬 상태를 유지하게 해준다.
 */
static int bptree_find_insert_index(const BPTreeNode *node, long long key) {
    int index;

    if (node == NULL) {
        return 0;
    }

    index = 0;
    while (index < node->key_count && node->keys[index] < key) {
        index++;
    }

    return index;
}

/*
 * 내부 노드에서 다음에 내려갈 자식 슬롯을 계산하는 함수다.
 * separator key 이상이면 오른쪽 자식으로 이동하는 B+ 트리 규칙을 따른다.
 * 탐색용, key로 내려갈 방향 찾기
 */
static int bptree_find_child_slot(const BPTreeNode *node, long long key) {
    int child_index;

    if (node == NULL) {
        return 0;
    }

    child_index = 0;
    while (child_index < node->key_count && key >= node->keys[child_index]) {
        child_index++;
    }

    return child_index;
}

/*
 * 부모 노드의 children 배열 안에서 특정 자식 포인터가 들어 있는 인덱스를 찾는 함수다.
 * 찾지 못하면 FAILURE를 반환해 상위 호출자가 구조 이상을 감지할 수 있게 한다.
 * 분할 후 부모에 끼워 넣을 때, 방금 split된 left child가 부모의 몇 번째 자식이었는지가 중요해서.
 */
static int bptree_find_child_index(const BPTreeNode *parent, const BPTreeNode *child) {
    int i;

    if (parent == NULL || child == NULL) {
        return FAILURE;
    }

    for (i = 0; i <= parent->key_count; i++) {
        if (parent->children[i] == child) {
            return i;
        }
    }

    return FAILURE;
}

/*
 * 새 루트 내부 노드를 만들어 좌우 자식과 separator key를 연결하는 함수다.
 * 루트 분할 시 공통으로 쓰이는 생성 로직을 한 곳에 모은다.
 */
static int bptree_create_new_root(BPTreeNode **root, BPTreeNode *left, long long key,
                                  BPTreeNode *right) {
    BPTreeNode *new_root;

    if (root == NULL || left == NULL || right == NULL) {
        return FAILURE;
    }

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

/*
 * 내부 노드 분할 뒤 오른쪽 새 내부 노드로 자식 포인터들을 옮기고 parent 포인터를 갱신하는 함수다.
 * 이 작업을 분리해 두면 split 함수 본문이 승격 key 처리에 더 집중할 수 있다.
 */
static void bptree_move_children_to_right_node(BPTreeNode *left, BPTreeNode *right,
                                               int start_child_index,
                                               int child_count) {
    int i;

    if (left == NULL || right == NULL) {
        return;
    }

    for (i = 0; i < child_count; i++) {
        right->children[i] = left->children[start_child_index + i];
        if (right->children[i] != NULL) {
            right->children[i]->parent = right;
            left->children[start_child_index + i] = NULL;
        }
    }
}

/*
 * 리프 노드 또는 내부 노드 하나를 새로 할당해 빈 상태로 초기화하는 함수다.
 * 노드가 잠시 overflow 될 수 있도록 키/포인터 배열은 한 칸 더 크게 선언되어 있다.
 */
BPTreeNode *bptree_create_node(int is_leaf) {
    BPTreeNode *node;

    node = (BPTreeNode *)calloc(1, sizeof(BPTreeNode));
    if (node == NULL) {
        fprintf(stderr, "Error: Failed to allocate B+ tree node.\n");
        return NULL;
    }

    node->is_leaf = is_leaf ? 1 : 0;
    node->key_count = 0;
    node->parent = NULL;
    node->next = NULL;
    return node;
}

/*
 * 검색 key가 포함될 가능성이 있는 리프 노드를 루트부터 내려가며 찾는 함수다.
 * 내부 노드에서는 separator key 비교만 하고, 실제 값 검사는 리프에서만 수행한다.
 */
BPTreeNode *bptree_find_leaf(BPTreeNode *root, long long key) {
    BPTreeNode *current;
    int child_slot;

    if (root == NULL) {
        return NULL;
    }

    current = root;
    while (!current->is_leaf) {
        child_slot = bptree_find_child_slot(current, key);
        current = current->children[child_slot];
        if (current == NULL) {
            return NULL;
        }
    }

    return current;
}

/*
 * 리프 노드에서 key를 실제로 찾아 대응 row_index를 반환하는 함수다.
 * 트리가 비어 있거나 key가 없으면 FAILURE를 반환한다.
 */
int bptree_search(BPTreeNode *root, long long key, int *out_row_index) {
    BPTreeNode *leaf;
    int i;

    leaf = bptree_find_leaf(root, key);
    if (leaf == NULL) {
        return FAILURE;
    }

    for (i = 0; i < leaf->key_count; i++) {
        if (leaf->keys[i] == key) {
            if (out_row_index != NULL) {
                *out_row_index = leaf->row_indices[i];
            }
            return SUCCESS;
        }
    }

    return FAILURE;
}

/*
 * 이미 확보한 리프 노드 안에서 정렬 위치를 찾아 key와 row_index를 삽입하는 함수다.
 * 중복 key는 상위 삽입 함수에서 먼저 차단하지만, 안전하게 한 번 더 검증한다.
 */
int bptree_insert_into_leaf(BPTreeNode *leaf, long long key, int row_index) {
    int insert_index;
    int i;

    if (leaf == NULL || !leaf->is_leaf) {
        return FAILURE;
    }

    insert_index = bptree_find_insert_index(leaf, key);
    if (insert_index < leaf->key_count && leaf->keys[insert_index] == key) {
        fprintf(stderr, "Error: Duplicate B+ tree key %lld.\n", key);
        return FAILURE;
    }

    for (i = leaf->key_count; i > insert_index; i--) {
        leaf->keys[i] = leaf->keys[i - 1];
        leaf->row_indices[i] = leaf->row_indices[i - 1];
    }

    leaf->keys[insert_index] = key;
    leaf->row_indices[insert_index] = row_index;
    leaf->key_count++;
    return SUCCESS;
}

/*
 * 꽉 찬 리프 노드를 둘로 나누고 오른쪽 리프의 첫 key를 부모 separator key로 올리는 함수다.
 * 리프 간 next 포인터도 함께 갱신해 이후 범위 순회가 가능하도록 유지한다.
 */
int bptree_split_leaf(BPTreeNode **root, BPTreeNode *leaf) {
    BPTreeNode *right_leaf;
    int split_index;
    int right_count;
    int i;

    if (root == NULL || leaf == NULL || !leaf->is_leaf) {
        return FAILURE;
    }

    right_leaf = bptree_create_node(1);
    if (right_leaf == NULL) {
        return FAILURE;
    }

    right_leaf->parent = leaf->parent;
    right_leaf->next = leaf->next;
    leaf->next = right_leaf;

    split_index = leaf->key_count / 2;
    right_count = leaf->key_count - split_index;

    for (i = 0; i < right_count; i++) {
        right_leaf->keys[i] = leaf->keys[split_index + i];
        right_leaf->row_indices[i] = leaf->row_indices[split_index + i];
    }

    right_leaf->key_count = right_count;
    leaf->key_count = split_index;

    return bptree_insert_into_parent(root, leaf, right_leaf->keys[0], right_leaf);
}

/*
 * 왼쪽 자식과 오른쪽 자식 사이 separator key를 부모 노드에 삽입하는 함수다.
 * 부모가 없으면 새 루트를 만들고, 부모가 overflow 되면 내부 노드 분할을 이어서 수행한다.
 */
int bptree_insert_into_parent(BPTreeNode **root, BPTreeNode *left, long long key,
                              BPTreeNode *right) {
    BPTreeNode *parent;
    int left_index;
    int i;

    if (root == NULL || left == NULL || right == NULL) {
        return FAILURE;
    }

    parent = left->parent;
    if (parent == NULL) {
        return bptree_create_new_root(root, left, key, right);
    }

    left_index = bptree_find_child_index(parent, left);
    if (left_index == FAILURE) {
        fprintf(stderr, "Error: Failed to locate left child in B+ tree parent.\n");
        return FAILURE;
    }

    for (i = parent->key_count; i > left_index; i--) {
        parent->keys[i] = parent->keys[i - 1];
    }
    for (i = parent->key_count + 1; i > left_index + 1; i--) {
        parent->children[i] = parent->children[i - 1];
    }

    parent->keys[left_index] = key;
    parent->children[left_index + 1] = right;
    right->parent = parent;
    parent->key_count++;

    if (parent->key_count > BPTREE_MAX_KEYS) {
        return bptree_split_internal(root, parent);
    }

    return SUCCESS;
}

/*
 * overflow 된 내부 노드를 좌우 두 내부 노드로 나누고 가운데 key를 상위 부모로 올리는 함수다.
 * 오른쪽으로 옮긴 자식들의 parent 포인터도 새 노드를 가리키도록 함께 갱신한다.
 */
int bptree_split_internal(BPTreeNode **root, BPTreeNode *node) {
    BPTreeNode *right_node;
    int promote_index;
    long long promote_key;
    int right_key_count;
    int i;

    if (root == NULL || node == NULL || node->is_leaf) {
        return FAILURE;
    }

    right_node = bptree_create_node(0);
    if (right_node == NULL) {
        return FAILURE;
    }

    right_node->parent = node->parent;

    promote_index = node->key_count / 2;
    promote_key = node->keys[promote_index];
    right_key_count = node->key_count - promote_index - 1;

    for (i = 0; i < right_key_count; i++) {
        right_node->keys[i] = node->keys[promote_index + 1 + i];
    }
    right_node->key_count = right_key_count;

    bptree_move_children_to_right_node(node, right_node, promote_index + 1,
                                       right_key_count + 1);

    node->key_count = promote_index;
    return bptree_insert_into_parent(root, node, promote_key, right_node);
}

/*
 * key가 이미 있으면 거부하고, 없으면 해당 리프까지 내려가 삽입과 분할을 수행하는 함수다.
 * 트리가 비어 있으면 첫 리프 노드를 곧바로 루트로 생성한다.
 */
int bptree_insert(BPTreeNode **root, long long key, int row_index) {
    BPTreeNode *leaf;
    int existing_row_index;

    if (root == NULL) {
        return FAILURE;
    }

    if (bptree_search(*root, key, &existing_row_index) == SUCCESS) {
        fprintf(stderr, "Error: Duplicate B+ tree key %lld.\n", key);
        return FAILURE;
    }

    if (*root == NULL) {
        *root = bptree_create_node(1);
        if (*root == NULL) {
            return FAILURE;
        }
    }

    leaf = bptree_find_leaf(*root, key);
    if (leaf == NULL) {
        leaf = *root;
    }

    if (bptree_insert_into_leaf(leaf, key, row_index) != SUCCESS) {
        return FAILURE;
    }

    if (leaf->key_count > BPTREE_MAX_KEYS) {
        return bptree_split_leaf(root, leaf);
    }

    return SUCCESS;
}

/*
 * 루트부터 자식들을 재귀적으로 해제해 B+ 트리 전체 메모리를 반환하는 함수다.
 * 리프의 next 체인은 같은 노드를 중복 방문할 수 있으므로 재귀 해제에는 사용하지 않는다.
 */
void bptree_free(BPTreeNode *root) {
    int i;

    if (root == NULL) {
        return;
    }

    if (!root->is_leaf) {
        for (i = 0; i <= root->key_count; i++) {
            bptree_free(root->children[i]);
            root->children[i] = NULL;
        }
    }

    free(root);
}
