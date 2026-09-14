#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define PRIO_LOW  7
#define PRIO_MED  5
#define PRIO_HIGH 3
#define PRIO_COOP (-1)

#define COOP_BUSY_ITERATIONS 5
#define COOP_BUSY_MS_PER_ITER 100

void t_low_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		LOG_INF("T_LOW running");
		k_msleep(300);
	}
}

void t_med_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		LOG_INF("T_MED running");
		k_msleep(200);
	}
}

void t_high_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		LOG_INF("T_HIGH running");
		k_msleep(100);
	}
}

void t_coop_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (int i = 1; i <= COOP_BUSY_ITERATIONS; i++) {
		LOG_INF("T_COOP busy iteration %d/%d", i, COOP_BUSY_ITERATIONS);
		k_busy_wait(COOP_BUSY_MS_PER_ITER * 1000);
	}

	LOG_INF("T_COOP done, calling k_yield()");
	k_yield();
	LOG_INF("T_COOP resumed after k_yield(), thread exiting");
}

K_THREAD_DEFINE(t_low, STACK_SIZE, t_low_fn,
		 NULL, NULL, NULL, PRIO_LOW, 0, 0);
K_THREAD_DEFINE(t_med, STACK_SIZE, t_med_fn,
		 NULL, NULL, NULL, PRIO_MED, 0, 0);
K_THREAD_DEFINE(t_high, STACK_SIZE, t_high_fn,
		 NULL, NULL, NULL, PRIO_HIGH, 0, 0);
K_THREAD_DEFINE(t_coop, STACK_SIZE, t_coop_fn,
		 NULL, NULL, NULL, PRIO_COOP, 0, 0);

int main(void)
{
	return 0;
}
