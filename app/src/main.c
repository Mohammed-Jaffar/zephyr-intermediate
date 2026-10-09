#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_DBG);

#define STACK_SIZE          1024
#define SENSOR_MS           100
#define EVENT_COUNT          10

#define BURST_EVENTS          5
#define BURST_INTERVAL_MS     4
#define DEBOUNCE_MS          30

static int events_processed;

static void sensor_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	events_processed++;
	LOG_INF("[HANDLER] processed event %d  tick=%u",
		events_processed, k_uptime_get_32());
}

K_WORK_DEFINE(sensor_work, sensor_handler);

static void sensor_sim_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (int i = 0; i < EVENT_COUNT; i++) {
		k_msleep(SENSOR_MS);

		LOG_INF("[SENSOR] event %d  tick=%u", i, k_uptime_get_32());

		int ret = k_work_submit(&sensor_work);

		if (ret < 0) {
			LOG_ERR("submit failed: %d", ret);
		}
	}

	LOG_INF("[SENSOR] all events produced");
}

static int debounce_triggers;
static int debounce_calls;

static void debounce_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	debounce_calls++;
	LOG_INF("[DEBOUNCE] handler fired  calls=%d  tick=%u",
		debounce_calls, k_uptime_get_32());
}

K_WORK_DELAYABLE_DEFINE(debounce_work, debounce_handler);

static void burst_sim_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (int i = 0; i < BURST_EVENTS; i++) {
		debounce_triggers++;
		LOG_INF("[BURST] trigger %d  tick=%u",
			debounce_triggers, k_uptime_get_32());

		k_work_reschedule(&debounce_work, K_MSEC(DEBOUNCE_MS));

		k_msleep(BURST_INTERVAL_MS);
	}

	LOG_INF("[BURST] burst complete, waiting for debounce window");
}

static struct k_thread sensor_thread_data;
static struct k_thread burst_thread_data;
K_THREAD_STACK_DEFINE(sensor_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(burst_stack, STACK_SIZE);

int main(void)
{
	LOG_INF("=== L3 Task: Polling to Workqueue (event-driven) ===");
	LOG_INF("sensor_sim fires %d events, %dms apart, via k_work_submit",
		EVENT_COUNT, SENSOR_MS);

	k_tid_t sensor_tid = k_thread_create(&sensor_thread_data, sensor_stack,
					      K_THREAD_STACK_SIZEOF(sensor_stack),
					      sensor_sim_fn, NULL, NULL, NULL,
					      5, 0, K_NO_WAIT);
	k_thread_join(sensor_tid, K_FOREVER);
	k_msleep(20);

	LOG_INF("[SUMMARY] events_fired=%d  handler_calls=%d",
		EVENT_COUNT, events_processed);

	LOG_INF("=== BONUS: debounce a burst with k_work_reschedule ===");
	LOG_INF("%d triggers within ~%dms, debounce window=%dms",
		BURST_EVENTS, BURST_EVENTS * BURST_INTERVAL_MS, DEBOUNCE_MS);

	k_tid_t burst_tid = k_thread_create(&burst_thread_data, burst_stack,
					     K_THREAD_STACK_SIZEOF(burst_stack),
					     burst_sim_fn, NULL, NULL, NULL,
					     5, 0, K_NO_WAIT);
	k_thread_join(burst_tid, K_FOREVER);
	k_msleep(DEBOUNCE_MS + 50);

	LOG_INF("[SUMMARY] triggers=%d  handler_calls=%d",
		debounce_triggers, debounce_calls);

	return 0;
}
