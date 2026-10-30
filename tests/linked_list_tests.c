#include "utest/utest.h"
#include <core/core.h>
#include <stddef.h>

typedef struct TestNode {
    int value;
    struct TestNode* next;
    struct TestNode* prev;
} TestNode;

typedef struct TestSingleNode {
    int value;
    struct TestSingleNode* next;
} TestSingleNode;

UTEST(linked_list, dll_push_back_empty_list) {
    TestNode* first = nullptr;
    TestNode* last = nullptr;
    TestNode node = { 42, nullptr, nullptr };

    dll_push_back(first, last, &node);

    ASSERT_EQ(first, &node);
    ASSERT_EQ(last, &node);
    ASSERT_EQ(node.next, nullptr);
    ASSERT_EQ(node.prev, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, dll_push_back_single_node) {
    TestNode* first = nullptr;
    TestNode* last = nullptr;
    TestNode node1 = { 1, nullptr, nullptr };
    TestNode node2 = { 2, nullptr, nullptr };

    dll_push_back(first, last, &node1);
    dll_push_back(first, last, &node2);

    ASSERT_EQ(first, &node1);
    ASSERT_EQ(last, &node2);
    ASSERT_EQ(node1.next, &node2);
    ASSERT_EQ(node1.prev, nullptr);
    ASSERT_EQ(node2.next, nullptr);
    ASSERT_EQ(node2.prev, &node1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, dll_push_front_single_node) {
    TestNode* first = nullptr;
    TestNode* last = nullptr;
    TestNode node1 = { 1, nullptr, nullptr };
    TestNode node2 = { 2, nullptr, nullptr };

    dll_push_front(first, last, &node1);
    dll_push_front(first, last, &node2);

    ASSERT_EQ(first, &node2);
    ASSERT_EQ(last, &node1);
    ASSERT_EQ(node2.next, &node1);
    ASSERT_EQ(node2.prev, nullptr);
    ASSERT_EQ(node1.next, nullptr);
    ASSERT_EQ(node1.prev, &node2);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, dll_insert_middle) {
    TestNode* first = nullptr;
    TestNode* last = nullptr;
    TestNode node1 = { 1, nullptr, nullptr };
    TestNode node2 = { 2, nullptr, nullptr };
    TestNode node3 = { 3, nullptr, nullptr };

    dll_push_back(first, last, &node1);
    dll_push_back(first, last, &node3);
    dll_insert(first, last, &node1, &node2);

    ASSERT_EQ(first, &node1);
    ASSERT_EQ(last, &node3);
    ASSERT_EQ(node1.next, &node2);
    ASSERT_EQ(node2.next, &node3);
    ASSERT_EQ(node2.prev, &node1);
    ASSERT_EQ(node3.prev, &node2);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, dll_remove_first) {
    TestNode* first = nullptr;
    TestNode* last = nullptr;
    TestNode node1 = { 1, nullptr, nullptr };
    TestNode node2 = { 2, nullptr, nullptr };
    TestNode node3 = { 3, nullptr, nullptr };

    dll_push_back(first, last, &node1);
    dll_push_back(first, last, &node2);
    dll_push_back(first, last, &node3);

    dll_remove(first, last, &node1);

    ASSERT_EQ(first, &node2);
    ASSERT_EQ(last, &node3);
    ASSERT_EQ(node2.prev, nullptr);
    ASSERT_EQ(node2.next, &node3);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, dll_remove_last) {
    TestNode* first = nullptr;
    TestNode* last = nullptr;
    TestNode node1 = { 1, nullptr, nullptr };
    TestNode node2 = { 2, nullptr, nullptr };
    TestNode node3 = { 3, nullptr, nullptr };

    dll_push_back(first, last, &node1);
    dll_push_back(first, last, &node2);
    dll_push_back(first, last, &node3);

    dll_remove(first, last, &node3);

    ASSERT_EQ(first, &node1);
    ASSERT_EQ(last, &node2);
    ASSERT_EQ(node2.next, nullptr);
    ASSERT_EQ(node2.prev, &node1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, dll_remove_middle) {
    TestNode* first = nullptr;
    TestNode* last = nullptr;
    TestNode node1 = { 1, nullptr, nullptr };
    TestNode node2 = { 2, nullptr, nullptr };
    TestNode node3 = { 3, nullptr, nullptr };

    dll_push_back(first, last, &node1);
    dll_push_back(first, last, &node2);
    dll_push_back(first, last, &node3);

    dll_remove(first, last, &node2);

    ASSERT_EQ(first, &node1);
    ASSERT_EQ(last, &node3);
    ASSERT_EQ(node1.next, &node3);
    ASSERT_EQ(node3.prev, &node1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, dll_remove_only_node) {
    TestNode* first = nullptr;
    TestNode* last = nullptr;
    TestNode node = { 42, nullptr, nullptr };

    dll_push_back(first, last, &node);
    dll_remove(first, last, &node);

    ASSERT_EQ(first, nullptr);
    ASSERT_EQ(last, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, sll_queue_push_empty) {
    TestSingleNode* first = nullptr;
    TestSingleNode* last = nullptr;
    TestSingleNode node = { 42, nullptr };

    sll_queue_push(first, last, &node);

    ASSERT_EQ(first, &node);
    ASSERT_EQ(last, &node);
    ASSERT_EQ(node.next, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, sll_queue_push_multiple) {
    TestSingleNode* first = nullptr;
    TestSingleNode* last = nullptr;
    TestSingleNode node1 = { 1, nullptr };
    TestSingleNode node2 = { 2, nullptr };
    TestSingleNode node3 = { 3, nullptr };

    sll_queue_push(first, last, &node1);
    sll_queue_push(first, last, &node2);
    sll_queue_push(first, last, &node3);

    ASSERT_EQ(first, &node1);
    ASSERT_EQ(last, &node3);
    ASSERT_EQ(node1.next, &node2);
    ASSERT_EQ(node2.next, &node3);
    ASSERT_EQ(node3.next, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, sll_queue_push_front) {
    TestSingleNode* first = nullptr;
    TestSingleNode* last = nullptr;
    TestSingleNode node1 = { 1, nullptr };
    TestSingleNode node2 = { 2, nullptr };

    sll_queue_push(first, last, &node1);
    sll_queue_push_front(first, last, &node2);

    ASSERT_EQ(first, &node2);
    ASSERT_EQ(last, &node1);
    ASSERT_EQ(node2.next, &node1);
    ASSERT_EQ(node1.next, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, sll_queue_pop) {
    TestSingleNode* first = nullptr;
    TestSingleNode* last = nullptr;
    TestSingleNode node1 = { 1, nullptr };
    TestSingleNode node2 = { 2, nullptr };
    TestSingleNode node3 = { 3, nullptr };

    sll_queue_push(first, last, &node1);
    sll_queue_push(first, last, &node2);
    sll_queue_push(first, last, &node3);

    sll_queue_pop(first, last);

    ASSERT_EQ(first, &node2);
    ASSERT_EQ(last, &node3);
    ASSERT_EQ(node2.next, &node3);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, sll_queue_pop_last) {
    TestSingleNode* first = nullptr;
    TestSingleNode* last = nullptr;
    TestSingleNode node = { 42, nullptr };

    sll_queue_push(first, last, &node);
    sll_queue_pop(first, last);

    ASSERT_EQ(first, nullptr);
    ASSERT_EQ(last, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, sll_queue_remove_first) {
    TestSingleNode* first = nullptr;
    TestSingleNode* last = nullptr;
    TestSingleNode node1 = { 1, nullptr };
    TestSingleNode node2 = { 2, nullptr };
    TestSingleNode node3 = { 3, nullptr };

    sll_queue_push(first, last, &node1);
    sll_queue_push(first, last, &node2);
    sll_queue_push(first, last, &node3);

    sll_queue_remove(first, last, &node1);

    ASSERT_EQ(first, &node2);
    ASSERT_EQ(last, &node3);
    ASSERT_EQ(node2.next, &node3);
    ASSERT_EQ(node3.next, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, sll_queue_remove_middle) {
    TestSingleNode* first = nullptr;
    TestSingleNode* last = nullptr;
    TestSingleNode node1 = { 1, nullptr };
    TestSingleNode node2 = { 2, nullptr };
    TestSingleNode node3 = { 3, nullptr };

    sll_queue_push(first, last, &node1);
    sll_queue_push(first, last, &node2);
    sll_queue_push(first, last, &node3);

    sll_queue_remove(first, last, &node2);

    ASSERT_EQ(first, &node1);
    ASSERT_EQ(last, &node3);
    ASSERT_EQ(node1.next, &node3);
    ASSERT_EQ(node3.next, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, sll_queue_remove_last) {
    TestSingleNode* first = nullptr;
    TestSingleNode* last = nullptr;
    TestSingleNode node1 = { 1, nullptr };
    TestSingleNode node2 = { 2, nullptr };
    TestSingleNode node3 = { 3, nullptr };

    sll_queue_push(first, last, &node1);
    sll_queue_push(first, last, &node2);
    sll_queue_push(first, last, &node3);

    sll_queue_remove(first, last, &node3);

    ASSERT_EQ(first, &node1);
    ASSERT_EQ(last, &node2);
    ASSERT_EQ(node1.next, &node2);
    ASSERT_EQ(node2.next, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, sll_queue_remove_single_node) {
    TestSingleNode* first = nullptr;
    TestSingleNode* last = nullptr;
    TestSingleNode node = { 42, nullptr };

    sll_queue_push(first, last, &node);

    sll_queue_remove(first, last, &node);

    ASSERT_EQ(first, nullptr);
    ASSERT_EQ(last, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, sll_stack_push) {
    TestSingleNode* top = nullptr;
    TestSingleNode node1 = { 1, nullptr };
    TestSingleNode node2 = { 2, nullptr };

    sll_stack_push(top, &node1);
    sll_stack_push(top, &node2);

    ASSERT_EQ(top, &node2);
    ASSERT_EQ(node2.next, &node1);
    ASSERT_EQ(node1.next, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, sll_stack_pop) {
    TestSingleNode* top = nullptr;
    TestSingleNode node1 = { 1, nullptr };
    TestSingleNode node2 = { 2, nullptr };

    sll_stack_push(top, &node1);
    sll_stack_push(top, &node2);

    sll_stack_pop(top);

    ASSERT_EQ(top, &node1);
    ASSERT_EQ(node1.next, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, sll_stack_pop_last) {
    TestSingleNode* top = nullptr;
    TestSingleNode node = { 42, nullptr };

    sll_stack_push(top, &node);
    sll_stack_pop(top);

    ASSERT_EQ(top, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, for_each_list_iteration) {
    TestSingleNode* first = nullptr;
    TestSingleNode* last = nullptr;
    TestSingleNode node1 = { 1, nullptr };
    TestSingleNode node2 = { 2, nullptr };
    TestSingleNode node3 = { 3, nullptr };

    sll_queue_push(first, last, &node1);
    sll_queue_push(first, last, &node2);
    sll_queue_push(first, last, &node3);

    int count = 0;
    int sum = 0;
    for_each_list(node, first) {
        count++;
        sum += node->value;
    }

    ASSERT_EQ(count, 3);
    ASSERT_EQ(sum, 6);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(linked_list, for_each_list_empty) {
    TestSingleNode* first = nullptr;

    int count = 0;
    for_each_list(node, first) {
        count++;
    }

    ASSERT_EQ(count, 0);
}
