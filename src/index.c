#include "index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Hash one text key for equality-index bucket placement.
 */
static unsigned long index_hash_string(const char *text) {
    unsigned long hash;
    size_t i;

    hash = 5381UL;
    for (i = 0; text[i] != '\0'; i++) {
        hash = ((hash << 5U) + hash) + (unsigned char)text[i];
    }

    return hash;
}

/*
 * Append one file offset to a dynamic offset list.
 * Returns SUCCESS when the offset is stored in list.
 */
static int index_offset_list_append(OffsetList *list, long offset) {
    long *new_items;

    if (list == NULL) {
        return FAILURE;
    }

    if (list->items == NULL) {
        list->capacity = 4;
        list->items = (long *)malloc((size_t)list->capacity * sizeof(long));
        if (list->items == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory.\n");
            return FAILURE;
        }
    } else if (list->count >= list->capacity) {
        list->capacity *= 2;
        new_items = (long *)realloc(list->items,
                                    (size_t)list->capacity * sizeof(long));
        if (new_items == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory.\n");
            return FAILURE;
        }
        list->items = new_items;
    }

    list->items[list->count++] = offset;
    return SUCCESS;
}

/*
 * Insert one key/offset pair into the equality hash index.
 */
static int index_add_hash_entry(EqualityIndex *equality, const char *key,
                                long offset) {
    unsigned long bucket_index;
    HashNode *node;

    if (equality == NULL || key == NULL) {
        return FAILURE;
    }

    bucket_index = index_hash_string(key) % (unsigned long)equality->bucket_count;
    node = equality->buckets[bucket_index];
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            return index_offset_list_append(&node->offsets, offset);
        }
        node = node->next;
    }

    node = (HashNode *)calloc(1, sizeof(HashNode));
    if (node == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    node->key = utils_strdup(key);
    if (node->key == NULL) {
        free(node);
        return FAILURE;
    }

    if (index_offset_list_append(&node->offsets, offset) != SUCCESS) {
        free(node->key);
        free(node);
        return FAILURE;
    }

    node->next = equality->buckets[bucket_index];
    equality->buckets[bucket_index] = node;
    return SUCCESS;
}

/*
 * Sort range entries by indexed value and then by source row offset.
 */
static int index_compare_range_entries(const void *lhs, const void *rhs) {
    const RangeEntry *left = (const RangeEntry *)lhs;
    const RangeEntry *right = (const RangeEntry *)rhs;
    int comparison = utils_compare_values(left->key, right->key);

    if (comparison != 0) {
        return comparison;
    }

    if (left->offset < right->offset) {
        return -1;
    }
    if (left->offset > right->offset) {
        return 1;
    }
    return 0;
}

/*
 * Compare one sorted range entry against a query value.
 */
static int index_compare_entry_to_value(const RangeEntry *entry, const char *value) {
    return utils_compare_values(entry->key, value);
}

/*
 * Return the first range position whose key is not less than value.
 */
static int index_lower_bound(const RangeEntry *entries, int count,
                             const char *value) {
    int left;
    int right;
    int middle;

    left = 0;
    right = count;
    while (left < right) {
        middle = left + (right - left) / 2;
        if (index_compare_entry_to_value(&entries[middle], value) < 0) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }

    return left;
}

/*
 * Return the first range position whose key is greater than value.
 */
static int index_upper_bound(const RangeEntry *entries, int count,
                             const char *value) {
    int left;
    int right;
    int middle;

    left = 0;
    right = count;
    while (left < right) {
        middle = left + (right - left) / 2;
        if (index_compare_entry_to_value(&entries[middle], value) <= 0) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }

    return left;
}

/*
 * Copy offsets into a caller-owned buffer for query results.
 */
static int index_copy_offsets(const long *source, int count, long **offsets) {
    if (count <= 0) {
        *offsets = NULL;
        return SUCCESS;
    }

    *offsets = (long *)malloc((size_t)count * sizeof(long));
    if (*offsets == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    memcpy(*offsets, source, (size_t)count * sizeof(long));
    return SUCCESS;
}

/*
 * Build equality and range indexes for one loaded table column.
 * The caller owns out_index and must release it with index_free().
 */
int index_build(const TableData *table, int column_index, TableIndex *out_index) {
    int i;

    if (table == NULL || out_index == NULL || column_index < 0 ||
        column_index >= table->col_count) {
        return FAILURE;
    }

    memset(out_index, 0, sizeof(*out_index));
    out_index->column_index = column_index;
    out_index->equality.bucket_count = HASH_BUCKET_COUNT;
    out_index->equality.buckets = (HashNode **)calloc(
        (size_t)out_index->equality.bucket_count, sizeof(HashNode *));
    if (out_index->equality.buckets == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    if (table->row_count > 0) {
        out_index->range.entries = (RangeEntry *)malloc(
            (size_t)table->row_count * sizeof(RangeEntry));
        if (out_index->range.entries == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory.\n");
            index_free(out_index);
            return FAILURE;
        }
    }

    out_index->range.count = table->row_count;
    for (i = 0; i < table->row_count; i++) {
        if (index_add_hash_entry(&out_index->equality,
                                 table->rows[i][column_index],
                                 table->offsets[i]) != SUCCESS) {
            index_free(out_index);
            return FAILURE;
        }

        if (utils_safe_strcpy(out_index->range.entries[i].key,
                              sizeof(out_index->range.entries[i].key),
                              table->rows[i][column_index]) != SUCCESS) {
            fprintf(stderr, "Error: Indexed value is too long.\n");
            index_free(out_index);
            return FAILURE;
        }
        out_index->range.entries[i].offset = table->offsets[i];
    }

    qsort(out_index->range.entries, (size_t)out_index->range.count,
          sizeof(RangeEntry), index_compare_range_entries);
    return SUCCESS;
}

/*
 * Query the equality hash index for one value.
 * Caller owns the returned offsets array.
 */
int index_query_equals(const TableIndex *index, const char *value,
                       long **offsets, int *count) {
    unsigned long bucket_index;
    HashNode *node;

    if (index == NULL || value == NULL || offsets == NULL || count == NULL ||
        index->equality.buckets == NULL) {
        return FAILURE;
    }

    bucket_index = index_hash_string(value) %
                   (unsigned long)index->equality.bucket_count;
    node = index->equality.buckets[bucket_index];
    while (node != NULL) {
        if (strcmp(node->key, value) == 0) {
            *count = node->offsets.count;
            return index_copy_offsets(node->offsets.items, node->offsets.count,
                                      offsets);
        }
        node = node->next;
    }

    *count = 0;
    *offsets = NULL;
    return SUCCESS;
}

/*
 * Query the range index for !=, >, >=, <, and <= operations.
 * Caller owns the returned offsets array.
 */
int index_query_range(const TableIndex *index, const char *op, const char *value,
                      long **offsets, int *count) {
    int start;
    int end;
    int i;
    int result_count;
    long *result_offsets;

    if (index == NULL || op == NULL || value == NULL || offsets == NULL ||
        count == NULL) {
        return FAILURE;
    }

    *offsets = NULL;
    *count = 0;

    if (strcmp(op, "!=") == 0) {
        if (index->range.count == 0) {
            return SUCCESS;
        }

        result_offsets = (long *)malloc((size_t)index->range.count * sizeof(long));
        if (result_offsets == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory.\n");
            return FAILURE;
        }

        result_count = 0;
        for (i = 0; i < index->range.count; i++) {
            if (utils_compare_values(index->range.entries[i].key, value) != 0) {
                result_offsets[result_count++] = index->range.entries[i].offset;
            }
        }

        if (result_count == 0) {
            free(result_offsets);
            return SUCCESS;
        }

        *offsets = result_offsets;
        *count = result_count;
        return SUCCESS;
    }

    start = 0;
    end = index->range.count;
    if (strcmp(op, ">") == 0) {
        start = index_upper_bound(index->range.entries, index->range.count, value);
    } else if (strcmp(op, ">=") == 0) {
        start = index_lower_bound(index->range.entries, index->range.count, value);
    } else if (strcmp(op, "<") == 0) {
        end = index_lower_bound(index->range.entries, index->range.count, value);
    } else if (strcmp(op, "<=") == 0) {
        end = index_upper_bound(index->range.entries, index->range.count, value);
    } else {
        fprintf(stderr, "Error: Unsupported WHERE operator '%s'.\n", op);
        return FAILURE;
    }

    result_count = end - start;
    if (result_count <= 0) {
        return SUCCESS;
    }

    result_offsets = (long *)malloc((size_t)result_count * sizeof(long));
    if (result_offsets == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    for (i = 0; i < result_count; i++) {
        result_offsets[i] = index->range.entries[start + i].offset;
    }

    *offsets = result_offsets;
    *count = result_count;
    return SUCCESS;
}

/*
 * Release every dynamic allocation owned by one built index.
 */
void index_free(TableIndex *index) {
    int i;
    HashNode *node;
    HashNode *next;

    if (index == NULL) {
        return;
    }

    if (index->equality.buckets != NULL) {
        for (i = 0; i < index->equality.bucket_count; i++) {
            node = index->equality.buckets[i];
            while (node != NULL) {
                next = node->next;
                free(node->key);
                node->key = NULL;
                free(node->offsets.items);
                node->offsets.items = NULL;
                free(node);
                node = next;
            }
        }
        free(index->equality.buckets);
        index->equality.buckets = NULL;
    }

    free(index->range.entries);
    index->range.entries = NULL;
    index->range.count = 0;
}
