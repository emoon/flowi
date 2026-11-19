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

typedef struct DllFixture {
    TestNode node[3];
    TestNode* first;
    TestNode* last;
} DllFixture;

typedef struct SllFixture {
    TestSingleNode node[3];
    TestSingleNode* first;
    TestSingleNode* last;
} SllFixture;

static void dll_fixture_init(DllFixture* f, int linked) {
    *f = (DllFixture) {};
    for (int i = 0; i < 3; i++) {
        f->node[i].value = i + 1;
    }
    for (int i = 0; i < linked; i++) {
        dll_push_back(f->first, f->last, &f->node[i]);
    }
}

static void sll_fixture_init(SllFixture* f, int linked) {
    *f = (SllFixture) {};
    for (int i = 0; i < 3; i++) {
        f->node[i].value = i + 1;
    }
    for (int i = 0; i < linked; i++) {
        sll_queue_push(f->first, f->last, &f->node[i]);
    }
}

static void expect_dll_ends(int* utest_result, const DllFixture* f, const TestNode* first, const TestNode* last) {
    ASSERT_EQ(f->first, first);
    ASSERT_EQ(f->last, last);
}

static void expect_dll_links(int* utest_result, const TestNode* node, const TestNode* prev, const TestNode* next) {
    ASSERT_EQ(node->prev, prev);
    ASSERT_EQ(node->next, next);
}

static void expect_sll_ends(int* utest_result, const SllFixture* f, const TestSingleNode* first,
                            const TestSingleNode* last) {
    ASSERT_EQ(f->first, first);
    ASSERT_EQ(f->last, last);
}

UTEST(linked_list, dll_push_back_empty_list) {
    DllFixture f;
    dll_fixture_init(&f, 0);
    TestNode* n = &f.node[0];

    dll_push_back(f.first, f.last, n);

    expect_dll_ends(utest_result, &f, n, n);
    expect_dll_links(utest_result, n, nullptr, nullptr);
}

UTEST(linked_list, dll_push_back_single_node) {
    DllFixture f;
    dll_fixture_init(&f, 2);

    expect_dll_ends(utest_result, &f, &f.node[0], &f.node[1]);
    expect_dll_links(utest_result, &f.node[0], nullptr, &f.node[1]);
    expect_dll_links(utest_result, &f.node[1], &f.node[0], nullptr);
}

UTEST(linked_list, dll_push_front_single_node) {
    DllFixture f;
    dll_fixture_init(&f, 0);

    dll_push_front(f.first, f.last, &f.node[0]);
    dll_push_front(f.first, f.last, &f.node[1]);

    expect_dll_ends(utest_result, &f, &f.node[1], &f.node[0]);
    expect_dll_links(utest_result, &f.node[1], nullptr, &f.node[0]);
    expect_dll_links(utest_result, &f.node[0], &f.node[1], nullptr);
}

UTEST(linked_list, dll_insert_middle) {
    DllFixture f;
    dll_fixture_init(&f, 0);

    dll_push_back(f.first, f.last, &f.node[0]);
    dll_push_back(f.first, f.last, &f.node[2]);
    dll_insert(f.first, f.last, &f.node[0], &f.node[1]);

    expect_dll_ends(utest_result, &f, &f.node[0], &f.node[2]);
    ASSERT_EQ(f.node[0].next, &f.node[1]);
    ASSERT_EQ(f.node[1].next, &f.node[2]);
    ASSERT_EQ(f.node[1].prev, &f.node[0]);
    ASSERT_EQ(f.node[2].prev, &f.node[1]);
}

UTEST(linked_list, dll_remove_first) {
    DllFixture f;
    dll_fixture_init(&f, 3);

    dll_remove(f.first, f.last, &f.node[0]);

    expect_dll_ends(utest_result, &f, &f.node[1], &f.node[2]);
    ASSERT_EQ(f.node[1].prev, nullptr);
    ASSERT_EQ(f.node[1].next, &f.node[2]);
}

UTEST(linked_list, dll_remove_last) {
    DllFixture f;
    dll_fixture_init(&f, 3);

    dll_remove(f.first, f.last, &f.node[2]);

    expect_dll_ends(utest_result, &f, &f.node[0], &f.node[1]);
    ASSERT_EQ(f.node[1].next, nullptr);
    ASSERT_EQ(f.node[1].prev, &f.node[0]);
}

UTEST(linked_list, dll_remove_middle) {
    DllFixture f;
    dll_fixture_init(&f, 3);

    dll_remove(f.first, f.last, &f.node[1]);

    expect_dll_ends(utest_result, &f, &f.node[0], &f.node[2]);
    ASSERT_EQ(f.node[0].next, &f.node[2]);
    ASSERT_EQ(f.node[2].prev, &f.node[0]);
}

UTEST(linked_list, dll_remove_only_node) {
    DllFixture f;
    dll_fixture_init(&f, 1);

    dll_remove(f.first, f.last, &f.node[0]);

    expect_dll_ends(utest_result, &f, nullptr, nullptr);
}

UTEST(linked_list, sll_queue_push_empty) {
    SllFixture f;
    sll_fixture_init(&f, 0);
    TestSingleNode* n = &f.node[0];

    sll_queue_push(f.first, f.last, n);

    expect_sll_ends(utest_result, &f, n, n);
    ASSERT_EQ(n->next, nullptr);
}

UTEST(linked_list, sll_queue_push_multiple) {
    SllFixture f;
    sll_fixture_init(&f, 3);

    expect_sll_ends(utest_result, &f, &f.node[0], &f.node[2]);
    ASSERT_EQ(f.node[0].next, &f.node[1]);
    ASSERT_EQ(f.node[1].next, &f.node[2]);
    ASSERT_EQ(f.node[2].next, nullptr);
}

UTEST(linked_list, sll_queue_push_front) {
    SllFixture f;
    sll_fixture_init(&f, 1);

    sll_queue_push_front(f.first, f.last, &f.node[1]);

    expect_sll_ends(utest_result, &f, &f.node[1], &f.node[0]);
    ASSERT_EQ(f.node[1].next, &f.node[0]);
    ASSERT_EQ(f.node[0].next, nullptr);
}

UTEST(linked_list, sll_queue_pop) {
    SllFixture f;
    sll_fixture_init(&f, 3);

    sll_queue_pop(f.first, f.last);

    expect_sll_ends(utest_result, &f, &f.node[1], &f.node[2]);
    ASSERT_EQ(f.node[1].next, &f.node[2]);
}

UTEST(linked_list, sll_queue_pop_last) {
    SllFixture f;
    sll_fixture_init(&f, 1);

    sll_queue_pop(f.first, f.last);

    expect_sll_ends(utest_result, &f, nullptr, nullptr);
}

UTEST(linked_list, sll_queue_remove_first) {
    SllFixture f;
    sll_fixture_init(&f, 3);

    sll_queue_remove(f.first, f.last, &f.node[0]);

    expect_sll_ends(utest_result, &f, &f.node[1], &f.node[2]);
    ASSERT_EQ(f.node[1].next, &f.node[2]);
    ASSERT_EQ(f.node[2].next, nullptr);
}

UTEST(linked_list, sll_queue_remove_middle) {
    SllFixture f;
    sll_fixture_init(&f, 3);

    sll_queue_remove(f.first, f.last, &f.node[1]);

    expect_sll_ends(utest_result, &f, &f.node[0], &f.node[2]);
    ASSERT_EQ(f.node[0].next, &f.node[2]);
    ASSERT_EQ(f.node[2].next, nullptr);
}

UTEST(linked_list, sll_queue_remove_last) {
    SllFixture f;
    sll_fixture_init(&f, 3);

    sll_queue_remove(f.first, f.last, &f.node[2]);

    expect_sll_ends(utest_result, &f, &f.node[0], &f.node[1]);
    ASSERT_EQ(f.node[0].next, &f.node[1]);
    ASSERT_EQ(f.node[1].next, nullptr);
}

UTEST(linked_list, sll_queue_remove_single_node) {
    SllFixture f;
    sll_fixture_init(&f, 1);

    sll_queue_remove(f.first, f.last, &f.node[0]);

    expect_sll_ends(utest_result, &f, nullptr, nullptr);
}

UTEST(linked_list, sll_stack_push) {
    SllFixture f;
    sll_fixture_init(&f, 0);

    sll_stack_push(f.first, &f.node[0]);
    sll_stack_push(f.first, &f.node[1]);

    ASSERT_EQ(f.first, &f.node[1]);
    ASSERT_EQ(f.node[1].next, &f.node[0]);
    ASSERT_EQ(f.node[0].next, nullptr);
}

UTEST(linked_list, sll_stack_pop) {
    SllFixture f;
    sll_fixture_init(&f, 0);

    sll_stack_push(f.first, &f.node[0]);
    sll_stack_push(f.first, &f.node[1]);

    sll_stack_pop(f.first);

    ASSERT_EQ(f.first, &f.node[0]);
    ASSERT_EQ(f.node[0].next, nullptr);
}

UTEST(linked_list, sll_stack_pop_last) {
    SllFixture f;
    sll_fixture_init(&f, 0);

    sll_stack_push(f.first, &f.node[0]);
    sll_stack_pop(f.first);

    ASSERT_EQ(f.first, nullptr);
}

UTEST(linked_list, for_each_list_iteration) {
    SllFixture f;
    sll_fixture_init(&f, 3);

    int count = 0;
    int sum = 0;
    for_each_list(node, f.first) {
        count++;
        sum += node->value;
    }

    ASSERT_EQ(count, 3);
    ASSERT_EQ(sum, 6);
}

UTEST(linked_list, for_each_list_empty) {
    SllFixture f;
    sll_fixture_init(&f, 0);

    int count = 0;
    for_each_list(node, f.first) {
        count++;
    }

    ASSERT_EQ(count, 0);
}
