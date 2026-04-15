#ifndef TABLE_RUNTIME_H
#define TABLE_RUNTIME_H

#include "bptree.h"
#include "parser.h"

typedef struct {
    char table_name[MAX_IDENTIFIER_LEN];
    int col_count;
    char columns[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    char ***rows;
    int row_count;
    int capacity;
    int id_column_index;
    long long next_id;
    BPTreeNode *id_index_root;
    int loaded;
} TableRuntime;

/*
 * 런타임 테이블 구조체를 빈 상태로 초기화한다.
 */
int table_init(TableRuntime *table, const char *table_name);

/*
 * 런타임 테이블이 소유한 메모리를 모두 해제한다.
 */
void table_free(TableRuntime *table);

/*
 * 행 배열 용량이 부족하면 한 단계 늘린다.
 */
int table_reserve_if_needed(TableRuntime *table);

/*
 * 현재 활성 테이블을 가져오거나, 다른 테이블이면 새 빈 런타임으로 바꾼다.
 */
TableRuntime *table_get_or_load(const char *table_name);

/*
 * 현재 활성 런타임 테이블을 정리한다.
 */
void table_cleanup_active(void);

/*
 * 입력 컬럼/값 쌍을 메모리 테이블에 행 하나로 추가한다.
 * 성공 시 inserted_id에 새 자동 id를 저장한다.
 */
int table_insert_row(TableRuntime *table,
                     const char columns[][MAX_IDENTIFIER_LEN],
                     const char values[][MAX_VALUE_LEN],
                     int column_count, int use_index,
                     long long *inserted_id);

/*
 * row_index 위치의 행을 반환한다.
 * 범위를 벗어나면 NULL을 반환한다.
 */
char **table_get_row_by_slot(const TableRuntime *table, int row_index);

/*
 * WHERE 조건을 선형 탐색해 일치하는 row_index 목록을 만든다.
 * 반환된 배열은 호출자가 free() 해야 한다.
 */
int table_linear_scan_by_field(const TableRuntime *table, const WhereClause *where,
                               int **matching_slots, int *match_count);

#endif
