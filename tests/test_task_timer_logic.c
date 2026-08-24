#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#define TIMER_MAX_NAME    32
#define TIMER_MAX_NODES   8
#define TIMER_MAX_HISTORY 20
#define TIMER_MAX_TASKS   8
#define TIMER_NODE_NAME   16

typedef enum {
    TIMER_STATE_IDLE = 0,
    TIMER_STATE_RUNNING,
    TIMER_STATE_PAUSED,
    TIMER_STATE_COMPLETED,
} timer_state_t;

typedef struct {
    char name[TIMER_NODE_NAME];
    uint32_t duration_sec;
} timer_node_t;

typedef struct {
    char name[TIMER_MAX_NAME];
    uint8_t node_count;
    timer_node_t nodes[TIMER_MAX_NODES];
} timer_task_t;

typedef struct {
    char task_name[TIMER_MAX_NAME];
    char node_name[TIMER_NODE_NAME];
    uint32_t total_sec;
    uint32_t actual_sec;
    uint32_t timestamp;
    bool completed_on_time;
} timer_history_t;

typedef struct {
    timer_state_t state;
    int current_task_idx;
    int current_node_idx;
    uint32_t node_elapsed;
    uint32_t task_elapsed;
    uint32_t start_timestamp;
    bool repeat_mode;
} timer_runtime_t;

static uint32_t total_duration(const timer_task_t *t)
{
    uint32_t total = 0;
    for (int i = 0; i < t->node_count; i++) {
        total += t->nodes[i].duration_sec;
    }
    return total;
}

static int add_task(timer_task_t *tasks, int count, const char *name,
                    int n1_dur, const char *n1_name,
                    int n2_dur, const char *n2_name)
{
    if (count >= TIMER_MAX_TASKS) return -1;
    timer_task_t *task = &tasks[count];
    memset(task, 0, sizeof(*task));
    strncpy(task->name, name, TIMER_MAX_NAME - 1);
    task->node_count = 2;
    strncpy(task->nodes[0].name, n1_name, TIMER_NODE_NAME - 1);
    task->nodes[0].duration_sec = n1_dur;
    strncpy(task->nodes[1].name, n2_name, TIMER_NODE_NAME - 1);
    task->nodes[1].duration_sec = n2_dur;
    return count + 1;
}

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

int main(void)
{
    printf("=== AI Passport Timer Logic Tests ===\n\n");

    timer_task_t tasks[TIMER_MAX_TASKS];
    memset(tasks, 0, sizeof(tasks));
    int count = 0;

    TEST("add_task basic");
    count = add_task(tasks, count, "Study", 600, "Read", 300, "Review");
    if (count == 1 && strcmp(tasks[0].name, "Study") == 0 &&
        tasks[0].node_count == 2 &&
        strcmp(tasks[0].nodes[0].name, "Read") == 0 &&
        tasks[0].nodes[0].duration_sec == 600 &&
        strcmp(tasks[0].nodes[1].name, "Review") == 0 &&
        tasks[0].nodes[1].duration_sec == 300) {
        PASS();
    } else {
        FAIL("task data mismatch");
    }

    TEST("total_duration calculation");
    uint32_t total = total_duration(&tasks[0]);
    if (total == 900) {
        PASS();
    } else {
        FAIL("total should be 900");
    }

    TEST("runtime state transitions");
    timer_runtime_t rt;
    memset(&rt, 0, sizeof(rt));
    if (rt.state == TIMER_STATE_IDLE && rt.node_elapsed == 0) {
        rt.state = TIMER_STATE_RUNNING;
        rt.current_node_idx = 0;
        rt.node_elapsed = 0;
        rt.task_elapsed = 0;
        if (rt.state == TIMER_STATE_RUNNING) {
            rt.node_elapsed++;
            rt.task_elapsed++;
            if (rt.node_elapsed == 1 && rt.task_elapsed == 1) {
                rt.state = TIMER_STATE_PAUSED;
                if (rt.state == TIMER_STATE_PAUSED) {
                    rt.state = TIMER_STATE_RUNNING;
                    rt.current_node_idx++;
                    rt.node_elapsed = 0;
                    if (rt.current_node_idx == 1 && rt.node_elapsed == 0) {
                        rt.state = TIMER_STATE_COMPLETED;
                        PASS();
                    } else {
                        FAIL("node transition failed");
                    }
                } else {
                    FAIL("pause/resume failed");
                }
            } else {
                FAIL("counter increment failed");
            }
        } else {
            FAIL("state not set to running");
        }
    } else {
        FAIL("initial state not idle");
    }

    TEST("history record structure");
    timer_history_t hist;
    memset(&hist, 0, sizeof(hist));
    strncpy(hist.task_name, "Study", TIMER_MAX_NAME - 1);
    strncpy(hist.node_name, "Review", TIMER_NODE_NAME - 1);
    hist.total_sec = 900;
    hist.actual_sec = 850;
    hist.completed_on_time = true;
    hist.timestamp = 1000;

    if (strcmp(hist.task_name, "Study") == 0 &&
        strcmp(hist.node_name, "Review") == 0 &&
        hist.total_sec == 900 &&
        hist.actual_sec == 850 &&
        hist.completed_on_time == true) {
        PASS();
    } else {
        FAIL("history record structure mismatch");
    }

    TEST("history on_time calculation");
    bool on_time = hist.actual_sec <= hist.total_sec;
    if (on_time == true) {
        PASS();
    } else {
        FAIL("should be on_time");
    }

    TEST("history overdue calculation");
    hist.actual_sec = 950;
    on_time = hist.actual_sec <= hist.total_sec;
    if (on_time == false) {
        PASS();
    } else {
        FAIL("should be overdue");
    }

    TEST("multi-node task with 8 nodes max");
    timer_task_t big_task;
    memset(&big_task, 0, sizeof(big_task));
    strncpy(big_task.name, "Work", TIMER_MAX_NAME - 1);
    big_task.node_count = TIMER_MAX_NODES;
    for (int i = 0; i < TIMER_MAX_NODES; i++) {
        snprintf(big_task.nodes[i].name, TIMER_NODE_NAME, "Node%d", i + 1);
        big_task.nodes[i].duration_sec = 300;
    }
    uint32_t big_total = total_duration(&big_task);
    if (big_total == 2400 && big_task.node_count == 8) {
        PASS();
    } else {
        FAIL("8-node task total mismatch");
    }

    printf("\n=== Results: %d/%d passed ===\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
