#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/tracing/tracing.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_INF);

#define STACK_SIZE            2048
#define CONTROL_PRIORITY         7
#define MAINTENANCE_PRIORITY     4
#define EVENT_PERIOD_MS        250
#define MAINTENANCE_LOAD_US  45000
#define DEADLINE_US          10000
#define EVENT_TOTAL             40
#define SUMMARY_EVERY           10
#define QUEUE_DEPTH              4

struct control_event {
	uint32_t seq;
	int64_t ready_ticks;
};

K_MSGQ_DEFINE(control_queue, sizeof(struct control_event), QUEUE_DEPTH, 4);
K_SEM_DEFINE(maintenance_start, 0, 1);
extern struct k_timer event_timer;

static uint32_t next_seq;
static uint32_t queue_full_drops;

static void event_timer_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	if (next_seq >= EVENT_TOTAL) {
		k_timer_stop(&event_timer);
		return;
	}

	struct control_event event = {
		.seq = next_seq++,
		.ready_ticks = k_uptime_ticks(),
	};

	sys_trace_named_event("evt_ready", event.seq, 0);

	if (k_msgq_put(&control_queue, &event, K_NO_WAIT) != 0) {
		queue_full_drops++;
		return;
	}

	k_sem_give(&maintenance_start);
}

K_TIMER_DEFINE(event_timer, event_timer_expiry, NULL);

static void control_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint32_t processed = 0;
	uint32_t misses = 0;
	uint32_t expected_seq = 0;
	uint32_t lost = 0;
	uint32_t max_latency_us = 0;

	while (processed < EVENT_TOTAL) {
		struct control_event event;
		int ret = k_msgq_get(&control_queue, &event, K_FOREVER);

		if (ret != 0) {
			LOG_ERR("[CONTROL] receive failed: %d", ret);
			continue;
		}

		if (event.seq != expected_seq) {
			lost += event.seq - expected_seq;
			LOG_WRN("[CONTROL] seq gap: expected=%u got=%u", expected_seq, event.seq);
		}
		expected_seq = event.seq + 1;

		int64_t latency_ticks = k_uptime_ticks() - event.ready_ticks;
		uint32_t latency_us = (uint32_t)k_ticks_to_us_floor64(latency_ticks);
		bool miss = latency_us > DEADLINE_US;

		if (latency_us > max_latency_us) {
			max_latency_us = latency_us;
		}

		sys_trace_named_event("ctrl_start", event.seq, latency_us);

		if (miss) {
			misses++;
			LOG_WRN("[CONTROL] seq=%u DEADLINE MISS start_latency=%u us (limit %d us) misses=%u",
				event.seq, latency_us, DEADLINE_US, misses);
		} else {
			LOG_INF("[CONTROL] seq=%u start_latency=%u us ok",
				event.seq, latency_us);
		}

		sys_trace_named_event("ctrl_done", event.seq, latency_us);
		processed++;

		if ((processed % SUMMARY_EVERY) == 0) {
			LOG_INF("[SUMMARY] processed=%u misses=%u max_latency=%u us lost=%u",
				processed, misses, max_latency_us, lost);
		}
	}

	LOG_INF("[FINAL] processed=%u misses=%u max_latency=%u us lost=%u queue_full_drops=%u",
		processed, misses, max_latency_us, lost, queue_full_drops);
}

static void maintenance_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_sem_take(&maintenance_start, K_FOREVER);
		k_busy_wait(MAINTENANCE_LOAD_US);
	}
}

K_THREAD_DEFINE(control, STACK_SIZE, control_fn,
		NULL, NULL, NULL, CONTROL_PRIORITY, 0, 0);

K_THREAD_DEFINE(maintenance, STACK_SIZE, maintenance_fn,
		NULL, NULL, NULL, MAINTENANCE_PRIORITY, 0, 0);

int main(void)
{
	LOG_INF("=== L6 Runtime Investigation ===");
	LOG_INF("deadline=%d us start latency, period=%d ms, events=%d",
		DEADLINE_US, EVENT_PERIOD_MS, EVENT_TOTAL);
	LOG_INF("control prio=%d maintenance prio=%d maintenance busy=%d us",
		CONTROL_PRIORITY, MAINTENANCE_PRIORITY, MAINTENANCE_LOAD_US);

	k_timer_start(&event_timer, K_MSEC(500), K_MSEC(EVENT_PERIOD_MS));

	return 0;
}
