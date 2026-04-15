#ifndef BPTREE_H
#define BPTREE_H

#include "utils.h"

#define BPTREE_MAX_KEYS 4

typedef struct BPTreeNode {
    int is_leaf;
    int key_count;
    long long keys[BPTREE_MAX_KEYS];
    struct BPTreeNode *parent;
    struct BPTreeNode *children[BPTREE_MAX_KEYS + 1];
    int row_indices[BPTREE_MAX_KEYS];
    struct BPTreeNode *next;
} BPTreeNode;

/*
 * 새 B+ 트리 노드 하나를 할당하고 초기화한다.
 * 성공 시 노드 포인터를, 실패 시 NULL을 반환한다.
 */
BPTreeNode *bptree_create_node(int is_leaf);

/*
 * key가 들어갈 리프 노드를 찾는다.
 * 루트가 없으면 NULL을 반환한다.
 */
BPTreeNode *bptree_find_leaf(BPTreeNode *root, long long key);

/*
 * key를 검색해 대응하는 row_index를 찾는다.
 * 성공 시 SUCCESS, 없으면 FAILURE를 반환한다.
 */
int bptree_search(BPTreeNode *root, long long key, int *row_index);

/*
 * key와 row_index를 B+ 트리에 삽입한다.
 * 성공 시 SUCCESS, 중복 키나 메모리 오류면 FAILURE를 반환한다.
 */
int bptree_insert(BPTreeNode **root, long long key, int row_index);

/*
 * 공간이 남아 있는 리프 노드에 key와 row_index를 삽입한다.
 */
int bptree_insert_into_leaf(BPTreeNode *leaf, long long key, int row_index);

/*
 * 가득 찬 리프 노드를 분할하면서 key와 row_index를 삽입한다.
 */
int bptree_split_leaf(BPTreeNode **root, BPTreeNode *leaf,
                      long long key, int row_index);

/*
 * 자식 분할 결과를 부모 노드에 반영한다.
 */
int bptree_insert_into_parent(BPTreeNode **root, BPTreeNode *left,
                              long long key, BPTreeNode *right);

/*
 * 가득 찬 내부 노드에 새 분기 정보를 넣기 위해 내부 노드를 분할한다.
 */
int bptree_split_internal(BPTreeNode **root, BPTreeNode *node,
                          int insert_index, long long key,
                          BPTreeNode *right_child);

/*
 * 루트 이하의 모든 노드를 재귀적으로 해제한다.
 */
void bptree_free(BPTreeNode *root);

#endif
