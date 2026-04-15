#include "bptree.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * 각 테스트 조건을 검사하고 실패 시 메시지를 출력하는 공통 헬퍼 함수다.
 * 어떤 검증이 깨졌는지 바로 알 수 있게 짧고 명확한 실패 문구만 남긴다.
 */
static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

/*
 * 현재 B+ 트리 설정에서 첫 리프 분할을 강제로 일으키는 최소 삽입 개수를 계산하는 함수다.
 * 리프 최대 key 수를 하나 넘기는 순간 첫 split이 발생하므로 BPTREE_MAX_KEYS + 1을 반환한다.
 */
static int first_split_insert_count(void) {
    return BPTREE_MAX_KEYS + 1;
}

/*
 * 현재 B+ 트리 설정에서 내부 노드 분할까지 유도하기 위한 충분한 대량 삽입 개수를 계산하는 함수다.
 * max key 값이 커져도 테스트가 깨지지 않도록 (BPTREE_MAX_KEYS + 1)^2 개를 사용한다.
 */
static int internal_split_insert_count(void) {
    return (BPTREE_MAX_KEYS + 1) * (BPTREE_MAX_KEYS + 1);
}

/*
 * 루트에서 가장 왼쪽 자식만 따라가 트리 높이를 계산하는 함수다.
 * 리프 한 층만 있으면 1, 내부 노드가 하나 더 있으면 2 이상이 된다.
 */
static int bptree_height(const BPTreeNode *root) {
    int height;
    const BPTreeNode *current;

    height = 0;
    current = root;
    while (current != NULL) {
        height++;
        if (current->is_leaf) {
            break;
        }
        current = current->children[0];
    }

    return height;
}

/*
 * 트리 전체에 대해 1부터 expected_count까지 모든 key가 정확한 row_index로 검색되는지 검사하는 함수다.
 * 대량 삽입 뒤 검색 정합성을 빠르게 검증하기 위해 반복적으로 사용한다.
 */
static int assert_full_search(BPTreeNode *root, int expected_count) {
    int i;
    int row_index;

    for (i = 1; i <= expected_count; i++) {
        if (bptree_search(root, i, &row_index) != SUCCESS || row_index != i * 10) {
            fprintf(stderr, "[FAIL] key %d should map to row_index %d\n", i, i * 10);
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * 리프 연결 리스트를 왼쪽부터 순회하면서 key가 오름차순으로 모두 존재하는지 검사하는 함수다.
 * 리프 분할 이후 next 포인터 체인이 올바르게 유지되는지 검증할 때 사용한다.
 */
static int assert_leaf_chain_sorted(BPTreeNode *root, int expected_count) {
    BPTreeNode *leaf;
    int expected_key;
    int i;

    if (root == NULL) {
        return assert_true(expected_count == 0, "empty tree should only expect zero keys");
    }

    leaf = root;
    while (leaf != NULL && !leaf->is_leaf) {
        leaf = leaf->children[0];
    }

    expected_key = 1;
    while (leaf != NULL) {
        for (i = 0; i < leaf->key_count; i++) {
            if (leaf->keys[i] != expected_key) {
                fprintf(stderr, "[FAIL] expected leaf key %d but found %lld\n",
                        expected_key, leaf->keys[i]);
                return FAILURE;
            }
            expected_key++;
        }
        leaf = leaf->next;
    }

    return assert_true(expected_key - 1 == expected_count,
                       "leaf chain should contain every inserted key exactly once");
}

/*
 * 소규모 삽입과 검색이 분할 없이도 정확히 동작하는지 검증하는 테스트 함수다.
 * 가장 기본적인 B+ 트리 insert/search 경로가 안정적인지 먼저 확인한다.
 */
static int test_basic_insert_and_search(void) {
    BPTreeNode *root;
    int row_index;
    long long keys_to_insert[3];
    int insert_count;
    int i;

    root = NULL;
    keys_to_insert[0] = 10;
    keys_to_insert[1] = 20;
    keys_to_insert[2] = 5;
    insert_count = BPTREE_MAX_KEYS >= 3 ? 3 : BPTREE_MAX_KEYS;

    for (i = 0; i < insert_count; i++) {
        if (assert_true(bptree_insert(&root, keys_to_insert[i],
                                      (int)keys_to_insert[i] * 10) == SUCCESS,
                        "basic insert should succeed") != SUCCESS) {
            bptree_free(root);
            return FAILURE;
        }
    }

    if (assert_true(root != NULL && root->is_leaf,
                    "root should stay a leaf while inserted key count is within max_keys") != SUCCESS ||
        assert_true(root->key_count == insert_count,
                    "leaf root should contain every basic test key before split") != SUCCESS ||
        assert_true(bptree_search(root, keys_to_insert[0], &row_index) == SUCCESS &&
                        row_index == (int)keys_to_insert[0] * 10,
                    "search should return inserted row_index") != SUCCESS ||
        assert_true(bptree_search(root, 999, &row_index) == FAILURE,
                    "search should fail for missing key") != SUCCESS) {
        bptree_free(root);
        return FAILURE;
    }

    bptree_free(root);
    return SUCCESS;
}

/*
 * 첫 리프 분할이 발생했을 때 새 루트와 좌우 리프 연결이 올바른지 검증하는 테스트 함수다.
 * B+ 트리의 첫 구조 확장이 기대한 형태로 일어나는지 확인한다.
 */
static int test_leaf_split_and_root_creation(void) {
    BPTreeNode *root;
    BPTreeNode *left_leaf;
    BPTreeNode *right_leaf;
    int insert_count;
    int i;

    root = NULL;
    insert_count = first_split_insert_count();

    for (i = 1; i <= insert_count; i++) {
        if (assert_true(bptree_insert(&root, i * 10, i * 100) == SUCCESS,
                        "insert should succeed until first leaf split") != SUCCESS) {
            bptree_free(root);
            return FAILURE;
        }
    }

    if (assert_true(root != NULL && !root->is_leaf,
                    "root should become an internal node after first split") != SUCCESS ||
        assert_true(root->key_count == 1,
                    "new root should contain one separator key after first split") != SUCCESS) {
        bptree_free(root);
        return FAILURE;
    }

    left_leaf = root->children[0];
    right_leaf = root->children[1];

    if (assert_true(left_leaf != NULL && right_leaf != NULL,
                    "root should point to two child leaves") != SUCCESS ||
        assert_true(left_leaf->is_leaf && right_leaf->is_leaf,
                    "children after first split should both be leaves") != SUCCESS ||
        assert_true(left_leaf->next == right_leaf,
                    "left leaf should link to right leaf through next pointer") != SUCCESS ||
        assert_true(bptree_search(root, 10, NULL) == SUCCESS,
                    "search should still find left-side key after split") != SUCCESS ||
        assert_true(bptree_search(root, insert_count * 10, NULL) == SUCCESS,
                    "search should still find right-side key after split") != SUCCESS) {
        bptree_free(root);
        return FAILURE;
    }

    bptree_free(root);
    return SUCCESS;
}

/*
 * 많은 key를 넣어 내부 노드 분할과 루트 높이 증가가 실제로 일어나는지 검증하는 테스트 함수다.
 * 동시에 모든 key 검색과 리프 체인 정렬도 함께 확인한다.
 */
static int test_internal_split_and_full_search(void) {
    BPTreeNode *root;
    int insert_count;
    int i;

    root = NULL;
    insert_count = internal_split_insert_count();

    for (i = 1; i <= insert_count; i++) {
        if (bptree_insert(&root, i, i * 10) != SUCCESS) {
            fprintf(stderr, "[FAIL] insert %d should succeed during bulk load\n", i);
            bptree_free(root);
            return FAILURE;
        }
    }

    if (assert_true(root != NULL && !root->is_leaf,
                    "bulk insert should produce an internal root") != SUCCESS ||
        assert_true(bptree_height(root) >= 3,
                    "bulk insert should grow tree height beyond leaf root") != SUCCESS ||
        assert_true(root->children[0] != NULL && !root->children[0]->is_leaf,
                    "root first child should become an internal node after internal split") != SUCCESS ||
        assert_true(assert_full_search(root, insert_count) == SUCCESS,
                    "every inserted key should remain searchable after splits") != SUCCESS ||
        assert_true(assert_leaf_chain_sorted(root, insert_count) == SUCCESS,
                    "leaf chain should stay sorted after repeated splits") != SUCCESS) {
        bptree_free(root);
        return FAILURE;
    }

    bptree_free(root);
    return SUCCESS;
}

/*
 * 중복 key 삽입이 거부되고 기존 값이 유지되는지 검증하는 테스트 함수다.
 * id 전용 인덱스 특성상 key uniqueness가 반드시 보장되어야 한다.
 */
static int test_duplicate_key_rejected(void) {
    BPTreeNode *root;
    int row_index;

    root = NULL;

    if (assert_true(bptree_insert(&root, 7, 70) == SUCCESS,
                    "initial insert should succeed") != SUCCESS ||
        assert_true(bptree_insert(&root, 7, 999) == FAILURE,
                    "duplicate key insert should fail") != SUCCESS ||
        assert_true(bptree_search(root, 7, &row_index) == SUCCESS && row_index == 70,
                    "existing row_index should remain unchanged after duplicate insert") != SUCCESS) {
        bptree_free(root);
        return FAILURE;
    }

    bptree_free(root);
    return SUCCESS;
}

/*
 * B+ 트리 단위 테스트 전체를 순서대로 실행하는 메인 함수다.
 * 검색, 첫 분할, 내부 분할, 중복 key 거부까지 핵심 시나리오를 모두 확인한다.
 */
int main(void) {
    if (test_basic_insert_and_search() != SUCCESS ||
        test_leaf_split_and_root_creation() != SUCCESS ||
        test_internal_split_and_full_search() != SUCCESS ||
        test_duplicate_key_rejected() != SUCCESS) {
        return EXIT_FAILURE;
    }

    puts("[PASS] bptree");
    return EXIT_SUCCESS;
}
