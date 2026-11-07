#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/tracing/tracing.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_INF);

#define STACK_SIZE            2048
#define CONTROL_PRIORITY         7
#define MAINTENANCE_PRIORITY     4
#define EVENT_PERIOD_MS        250
#define MAINTENANCE_LOAD_US  45000
#define DEADLINE_MS             10
#define EVENT_TOTAL             40
#define SUMMARY_EVERY           10
#define WARN_EVERY               5

struct control_event {
	uint32_t seq;
	uint32_t ready_ms;
};

K_MSGQ_DEFINE(control_queue, sizeof(struct control_event), 4, 4);
K_SEM_DEFINE(maintenance_start, 0, 1);
extern struct k_timer event_timer;

static uint32_t next_seq;

/* ================================================================== */
/*  Timer expiry: creates one control event                           */
/* ================================================================== */

static void event_timer_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	if (next_seq >= EVENT_TOTAL) {
		k_timer_stop(&event_timer);
		return;
	}

	struct control_event event = {
		.seq = next_seq++,
		.ready_ms = k_uptime_get_32(),
	};

	/* Timer expiry runs in interrupt context, so never wait here. */
	int ret = k_msgq_put(&control_queue, &event, K_NO_WAIT);

	if (ret != 0) {
		return;
	}

	/* Both threads become ready when the timer interrupt returns. */
	k_sem_give(&maintenance_start);

	sys_trace_named_event("evt_ready", event.seq, 0);
}

K_TIMER_DEFINE(event_timer, event_timer_expiry, NULL);

/* ================================================================== */
/*  Control thread                                                   */
/*                                                                   */
/*  Response-time guarantee: the control work starts within          */
/*  DEADLINE_MS of the timer expiry that created its event.          */
/* ================================================================== */

static void control_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	uint32_t processed = 0;
	uint32_t misses = 0;
	uint32_t max_latency_ms = 0;

	while (processed < EVENT_TOTAL) {
		struct control_event event;
		int ret = k_msgq_get(&control_queue, &event, K_FOREVER);

		if (ret != 0) {
			LOG_ERR("[CONTROL] receive failed: %d", ret);
			continue;
		}

		uint32_t latency_ms = k_uptime_get_32() - event.ready_ms;

		if (latency_ms > max_latency_ms) {
			max_latency_ms = latency_ms;
		}

		if (latency_ms > DEADLINE_MS) {
			misses++;
			/* Rate-limited: first miss, then every WARN_EVERY-th one. */
			if (misses == 1 || (misses % WARN_EVERY) == 0) {
				LOG_WRN("[CONTROL] seq=%u DEADLINE MISS latency=%u ms (limit %d) misses=%u",
					event.seq, latency_ms, DEADLINE_MS, misses);
			}
		} else {
			LOG_INF("[CONTROL] processed seq=%u latency=%u ms", event.seq, latency_ms);
		}

		processed++;
		sys_trace_named_event("ctrl_done", event.seq, latency_ms);

		if ((processed % SUMMARY_EVERY) == 0) {
			LOG_INF("[SUMMARY] processed=%u misses=%u max_latency=%u ms",
				processed, misses, max_latency_ms);
		}
	}

	LOG_INF("[FINAL] processed=%u misses=%u max_latency=%u ms",
		processed, misses, max_latency_ms);
}

/* ================================================================== */
/*  Background maintenance thread                                    */
/* ================================================================== */

static void maintenance_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	while (true) {
		k_sem_take(&maintenance_start, K_FOREVER);

		/* This work is important, but it has no short deadline. */
		k_busy_wait(MAINTENANCE_LOAD_US);
	}
}

K_THREAD_DEFINE(control, STACK_SIZE, control_fn,
		NULL, NULL, NULL, CONTROL_PRIORITY, 0, 0);

K_THREAD_DEFINE(maintenance, STACK_SIZE, maintenance_fn,
		NULL, NULL, NULL, MAINTENANCE_PRIORITY, 0, 0);

int main(void)
{
	LOG_INF("=== L6 Homework: Runtime Investigation ===");
	LOG_INF("Control work must start within %d ms", DEADLINE_MS);
	LOG_INF("control prio=%d maintenance prio=%d busy=%d us",
		CONTROL_PRIORITY, MAINTENANCE_PRIORITY, MAINTENANCE_LOAD_US);

	k_timer_start(&event_timer, K_MSEC(500), K_MSEC(EVENT_PERIOD_MS));

	return 0;
}
