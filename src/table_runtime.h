#ifndef TABLE_RUNTIME_H
#define TABLE_RUNTIME_H

#include "parser.h"

struct BPTreeNode;

typedef struct {
    char table_name[MAX_IDENTIFIER_LEN];
    int col_count;
    char columns[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    char ***rows;
    int row_count;
    int capacity;
    int id_column_index;
    long long next_id;
    struct BPTreeNode *id_index_root;
    int loaded;
} TableRuntime;

/*
 * 활성 런타임 테이블 구조체를 초기 상태로 되돌리는 함수다.
 * 이 함수는 구조체의 필드 값만 초기화하며, 기존 동적 메모리가 있다면 먼저
 * table_free()를 호출한 뒤 사용해야 한다.
 */
void table_init(TableRuntime *table);

/*
 * 런타임 테이블이 소유한 모든 행 메모리를 해제하고 구조체를 초기 상태로
 * 되돌리는 함수다.
 */
void table_free(TableRuntime *table);

/*
 * 현재 활성 테이블을 가져오거나, 다른 테이블 이름이면 기존 활성 테이블을
 * 정리한 뒤 새 빈 런타임 테이블을 준비하는 함수다.
 * 성공 시 out_table에 활성 테이블 포인터를 돌려준다.
 */
int table_get_or_load(const char *table_name, TableRuntime **out_table);

/*
 * 행 배열이 한 줄 더 들어갈 수 있도록 용량을 확장하는 함수다.
 * 현재 capacity가 충분하면 아무 작업도 하지 않고 SUCCESS를 반환한다.
 */
int table_reserve_if_needed(TableRuntime *table);

/*
 * INSERT 문 내용을 메모리 런타임 행으로 변환해 append하는 함수다.
 * 사용자가 id 컬럼을 직접 넣는 경우는 거부하며, 성공 시 새 row_index와
 * 자동 생성된 id를 반환한다.
 */
int table_insert_row(TableRuntime *table, const InsertStatement *stmt,
                     int *out_row_index, long long *out_id);

/*
 * row_index로 메모리 안의 실제 행 포인터를 조회하는 함수다.
 * 반환된 포인터의 소유권은 런타임 테이블에 있으며 호출자가 해제하면 안 된다.
 */
char **table_get_row_by_slot(const TableRuntime *table, int row_index);

/*
 * 특정 컬럼 조건으로 메모리 행들을 선형 탐색해 일치한 row_index 목록을
 * 반환하는 함수다.
 * column_name이 NULL이거나 빈 문자열이면 모든 행을 반환한다.
 */
int table_linear_scan_by_field(const TableRuntime *table, const char *column_name,
                               const char *op, const char *value,
                               int **out_row_indices, int *out_match_count);

/*
 * 모듈이 관리하는 활성 런타임 테이블 전체를 정리하는 함수다.
 * 프로그램 종료 시 호출해 전역 활성 테이블 메모리를 회수할 때 사용한다.
 */
void table_runtime_cleanup(void);

#endif
