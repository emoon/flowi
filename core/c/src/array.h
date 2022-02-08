#pragma once
/*

struct FlAllocator;

typedef struct Array {
    struct FLAllocator* allocator;
    void* data;
    int len;
    int capacity;
} Array;

void array_new_impl(Array* array, struct FlAllocator* allocator);
void* array_alloc_item_impl(Array* array, int size);

#define array_push_item(ArrayName* array, Type, item) \
    { Type* ret_item = array_push_item_impl((Array*)array, sizeof(Type)); *ret_item = item; }
*/
