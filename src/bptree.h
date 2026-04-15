#ifndef BPTREE_H
#define BPTREE_H

#include "utils.h"

#define BPTREE_MAX_KEYS 3

typedef struct BPTreeNode {
    int is_leaf;
    int key_count;
    long long keys[BPTREE_MAX_KEYS + 1];
    struct BPTreeNode *parent;
    struct BPTreeNode *children[BPTREE_MAX_KEYS + 2];
    int row_indices[BPTREE_MAX_KEYS + 1];
    struct BPTreeNode *next;
} BPTreeNode;

/*
 * 리프 노드 또는 내부 노드 하나를 새로 할당해 초기화하는 함수다.
 * is_leaf가 1이면 리프 노드, 0이면 내부 노드로 생성한다.
 */
BPTreeNode *bptree_create_node(int is_leaf);

/*
 * 주어진 key가 들어 있어야 하는 리프 노드를 찾아 반환하는 함수다.
 * 트리가 비어 있으면 NULL을 반환한다.
 */
BPTreeNode *bptree_find_leaf(BPTreeNode *root, long long key);

/*
 * key로 B+ 트리를 검색해 대응하는 row_index를 찾는 함수다.
 * 찾으면 out_row_index에 값을 저장하고 SUCCESS를 반환한다.
 */
int bptree_search(BPTreeNode *root, long long key, int *out_row_index);

/*
 * key와 row_index 한 쌍을 B+ 트리에 삽입하는 함수다.
 * 중복 key는 허용하지 않으며, 필요하면 리프/내부 노드 분할까지 수행한다.
 */
int bptree_insert(BPTreeNode **root, long long key, int row_index);

/*
 * 이미 찾은 리프 노드 안에 key와 row_index를 정렬 상태로 삽입하는 함수다.
 * 이 함수는 분할 전까지의 리프 내부 정렬만 담당한다.
 */
int bptree_insert_into_leaf(BPTreeNode *leaf, long long key, int row_index);

/*
 * 꽉 찬 리프 노드를 좌우 두 리프로 분할하고 부모에 separator key를 올리는 함수다.
 * 분할 과정에서 루트가 바뀔 수 있으므로 root 포인터를 함께 받는다.
 */
int bptree_split_leaf(BPTreeNode **root, BPTreeNode *leaf);

/*
 * 왼쪽 자식과 오른쪽 자식 사이에 separator key를 부모 노드로 올리는 함수다.
 * 부모가 없으면 새 루트를 만들고, 부모가 넘치면 내부 노드 분할까지 이어진다.
 */
int bptree_insert_into_parent(BPTreeNode **root, BPTreeNode *left, long long key,
                              BPTreeNode *right);

/*
 * 꽉 찬 내부 노드를 좌우 두 노드로 분할하고 가운데 key를 상위 부모로 승격하는 함수다.
 * 루트 내부 노드가 분할되는 경우 새 루트 생성까지 포함한다.
 */
int bptree_split_internal(BPTreeNode **root, BPTreeNode *node);

/*
 * B+ 트리 전체를 후위 순회 방식으로 해제하는 함수다.
 * 리프 연결 리스트는 children 재귀 해제와 겹치지 않도록 next를 따라가지 않는다.
 */
void bptree_free(BPTreeNode *root);

#endif
