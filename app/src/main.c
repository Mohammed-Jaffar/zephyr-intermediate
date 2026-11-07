#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(reliability, LOG_LEVEL_DBG);

#define STACK_SIZE            1024
#define PRIO_PRODUCER         5
#define PRIO_CONSUMER         5
#define PRIO_HEALTH           7

#define QUEUE_DEPTH           8
#define WARN_PERCENT          75
#define WARN_LEVEL            ((QUEUE_DEPTH * WARN_PERCENT) / 100)

#define PRODUCE_PERIOD_MS     50
#define CONSUME_TIME_MS       40
#define TOTAL_MESSAGES        200
#define STUCK_AFTER_MSG       20
#define STUCK_TIME_MS         3000
#define WDT_PERIOD_MS         1000
#define HEALTH_PERIOD_MS      100

struct sample {
	uint32_t seq;
	uint32_t ts_ms;
};

K_MSGQ_DEFINE(pipe_q, sizeof(struct sample), QUEUE_DEPTH, 4);

static int wdt_channel = -1;
static atomic_t wdt_expired_count;
static atomic_t producer_dropped;
static volatile bool producer_done;
static volatile bool consumer_done;

static void wdt_expired_cb(int channel_id, void *user_data)
{
	ARG_UNUSED(user_data);

	atomic_inc(&wdt_expired_count);
	LOG_ERR("[WDT] channel %d expired at %u ms: consumer not fed (q=%u/%d)",
		channel_id, k_uptime_get_32(),
		k_msgq_num_used_get(&pipe_q), QUEUE_DEPTH);
}

static void producer_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (uint32_t seq = 0; seq < TOTAL_MESSAGES; seq++) {
		struct sample s = {
			.seq = seq,
			.ts_ms = k_uptime_get_32(),
		};

		if (k_msgq_put(&pipe_q, &s, K_NO_WAIT) != 0) {
			atomic_inc(&producer_dropped);
			LOG_WRN("[PROD] queue full, dropped seq=%u", seq);
		}

		k_msleep(PRODUCE_PERIOD_MS);
	}

	producer_done = true;
	LOG_INF("[PROD] done");
}

static void consumer_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint32_t processed = 0;

	while (true) {
		struct sample s;
		int ret = k_msgq_get(&pipe_q, &s, K_MSEC(500));

		if (ret != 0) {
			if (producer_done && k_msgq_num_used_get(&pipe_q) == 0) {
				break;
			}
			task_wdt_feed(wdt_channel);
			continue;
		}

		processed++;
		LOG_INF("[CONS] seq=%u age=%ums q=%u/%d",
			s.seq, k_uptime_get_32() - s.ts_ms,
			k_msgq_num_used_get(&pipe_q), QUEUE_DEPTH);

		if (processed == STUCK_AFTER_MSG) {
			LOG_WRN("[CONS] simulating stuck consumer for %d ms",
				STUCK_TIME_MS);
			k_msleep(STUCK_TIME_MS);
			LOG_WRN("[CONS] resumed after stall");
		} else {
			k_msleep(CONSUME_TIME_MS);
		}

		task_wdt_feed(wdt_channel);
	}

	consumer_done = true;
	LOG_INF("[CONS] done processed=%u", processed);
}

static void health_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	bool warned = false;

	while (!consumer_done) {
		uint32_t used = k_msgq_num_used_get(&pipe_q);

		if (used >= WARN_LEVEL && !warned) {
			LOG_WRN("[HEALTH] queue at %u/%d (%u%%) >= %d%%",
				used, QUEUE_DEPTH,
				(used * 100) / QUEUE_DEPTH, WARN_PERCENT);
			warned = true;
		} else if (used < WARN_LEVEL && warned) {
			LOG_INF("[HEALTH] queue recovered to %u/%d", used, QUEUE_DEPTH);
			warned = false;
		}

		k_msleep(HEALTH_PERIOD_MS);
	}

	LOG_INF("[HEALTH] stopped");
}

K_THREAD_DEFINE(producer_tid, STACK_SIZE, producer_fn,
		NULL, NULL, NULL, PRIO_PRODUCER, 0, 0);
K_THREAD_DEFINE(consumer_tid, STACK_SIZE, consumer_fn,
		NULL, NULL, NULL, PRIO_CONSUMER, 0, 0);
K_THREAD_DEFINE(health_tid, STACK_SIZE, health_fn,
		NULL, NULL, NULL, PRIO_HEALTH, 0, 0);

int main(void)
{
	int ret = task_wdt_init(NULL);

	if (ret != 0) {
		LOG_ERR("task_wdt_init failed: %d", ret);
		return ret;
	}

	wdt_channel = task_wdt_add(WDT_PERIOD_MS, wdt_expired_cb, NULL);
	if (wdt_channel < 0) {
		LOG_ERR("task_wdt_add failed: %d", wdt_channel);
		return wdt_channel;
	}

	LOG_INF("=== L5 Reliability: producer/consumer + task WDT ===");
	LOG_INF("queue depth=%d warn=%d%% (%d) wdt=%dms stall=%dms at msg %d",
		QUEUE_DEPTH, WARN_PERCENT, WARN_LEVEL,
		WDT_PERIOD_MS, STUCK_TIME_MS, STUCK_AFTER_MSG);

	return 0;
}
