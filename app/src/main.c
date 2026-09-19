#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024
#define PRIO_WORKER 5
#define NUM_INCREMENTS 20

static int shared_counter;
static struct k_mutex counter_lock;

struct worker_args {
	const char *name;
	bool use_mutex;
};

static void increment_counter(bool use_mutex, const char *name)
{
	if (use_mutex) {
		k_mutex_lock(&counter_lock, K_FOREVER);
	}

	int temp = shared_counter;

	k_yield();

	temp = temp + 1;
	shared_counter = temp;

	if (use_mutex) {
		k_mutex_unlock(&counter_lock);
	}

	LOG_INF("%s -> counter = %d", name, shared_counter);
}

static void worker_fn(void *p1, void *p2, void *p3)
{
	struct worker_args *args = (struct worker_args *)p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (int i = 0; i < NUM_INCREMENTS; i++) {
		increment_counter(args->use_mutex, args->name);
	}
}

static struct k_thread worker_a_thread;
static struct k_thread worker_b_thread;
K_THREAD_STACK_DEFINE(worker_a_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(worker_b_stack, STACK_SIZE);

static void run_pair(struct worker_args *a, struct worker_args *b)
{
	k_tid_t tid_a = k_thread_create(&worker_a_thread, worker_a_stack,
					 K_THREAD_STACK_SIZEOF(worker_a_stack),
					 worker_fn, a, NULL, NULL,
					 PRIO_WORKER, 0, K_NO_WAIT);
	k_tid_t tid_b = k_thread_create(&worker_b_thread, worker_b_stack,
					 K_THREAD_STACK_SIZEOF(worker_b_stack),
					 worker_fn, b, NULL, NULL,
					 PRIO_WORKER, 0, K_NO_WAIT);

	k_thread_join(tid_a, K_FOREVER);
	k_thread_join(tid_b, K_FOREVER);
}

int main(void)
{
	struct worker_args unsafe_a = { .name = "UNSAFE_A", .use_mutex = false };
	struct worker_args unsafe_b = { .name = "UNSAFE_B", .use_mutex = false };
	struct worker_args safe_a = { .name = "SAFE_A", .use_mutex = true };
	struct worker_args safe_b = { .name = "SAFE_B", .use_mutex = true };

	k_mutex_init(&counter_lock);

	LOG_INF("=== Unsafe pass: no locking ===");
	shared_counter = 0;
	run_pair(&unsafe_a, &unsafe_b);
	LOG_INF("Unsafe final counter = %d (expected %d)", shared_counter,
		NUM_INCREMENTS * 2);

	LOG_INF("=== Safe pass: mutex protected ===");
	shared_counter = 0;
	run_pair(&safe_a, &safe_b);
	LOG_INF("Safe final counter = %d (expected %d)", shared_counter,
		NUM_INCREMENTS * 2);

	return 0;
}
