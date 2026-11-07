#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(zbus_demo, LOG_LEVEL_DBG);

#define PUBLISH_PERIOD_MS    100
#define SAMPLE_COUNT         50
#define LOGGER_DELAY_MS      500
#define LOGGER_QUEUE_SIZE    8
#define STACK_SIZE           1024
#define PRIO_PUBLISHER       5
#define PRIO_LOGGER          7

struct sensor_msg {
	uint32_t seq;
	int32_t temp_c_x100;
	uint32_t humidity_pct;
	uint32_t tick_ms;
};

static void display_listener_cb(const struct zbus_channel *chan);
ZBUS_LISTENER_DEFINE(display_listener, display_listener_cb);
ZBUS_SUBSCRIBER_DEFINE(logger_sub, LOGGER_QUEUE_SIZE);

ZBUS_CHAN_DEFINE(sensor_chan,
		 struct sensor_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(display_listener, logger_sub),
		 ZBUS_MSG_INIT(0));

static void display_listener_cb(const struct zbus_channel *chan)
{
	const struct sensor_msg *msg = zbus_chan_const_msg(chan);

	LOG_INF("[DISPLAY] seq=%u  temp=%d.%02d C  hum=%u%%",
		msg->seq, msg->temp_c_x100 / 100,
		(msg->temp_c_x100 < 0 ? -msg->temp_c_x100 : msg->temp_c_x100) % 100,
		msg->humidity_pct);
}

static void publisher_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int64_t next = k_uptime_get();

	for (uint32_t seq = 0; seq < SAMPLE_COUNT; seq++) {
		struct sensor_msg msg = {
			.seq = seq,
			.temp_c_x100 = 2000 + (int32_t)(seq % 20) * 10,
			.humidity_pct = 40 + (seq % 10),
			.tick_ms = (uint32_t)k_uptime_get_32(),
		};

		int ret = zbus_chan_pub(&sensor_chan, &msg, K_MSEC(10));

		if (ret < 0) {
			LOG_ERR("publish seq=%u failed: %d", seq, ret);
		}

		next += PUBLISH_PERIOD_MS;
		int64_t sleep_ms = next - k_uptime_get();

		if (sleep_ms > 0) {
			k_msleep(sleep_ms);
		}
	}

	LOG_INF("[PUBLISHER] done, %d samples published", SAMPLE_COUNT);
}

static void logger_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct zbus_channel *chan;
	struct sensor_msg msg;
	uint32_t logged = 0;
	uint32_t last_seq = 0;
	bool first = true;

	while (1) {
		int ret = zbus_sub_wait(&logger_sub, &chan, K_MSEC(1000));

		if (ret == -EAGAIN) {
			break;
		}

		if (ret < 0) {
			LOG_ERR("sub_wait failed: %d", ret);
			continue;
		}

		ret = zbus_chan_read(chan, &msg, K_MSEC(100));
		if (ret < 0) {
			LOG_ERR("chan_read failed: %d", ret);
			continue;
		}

		k_msleep(LOGGER_DELAY_MS);

		logged++;
		if (!first && msg.seq > last_seq + 1) {
			LOG_WRN("[LOGGER] skipped %u samples", msg.seq - last_seq - 1);
		}
		first = false;
		last_seq = msg.seq;

		LOG_INF("[LOGGER] seq=%u tick=%u temp=%d hum=%u",
			msg.seq, msg.tick_ms, msg.temp_c_x100, msg.humidity_pct);
	}

	LOG_INF("[LOGGER] finished, logged=%u", logged);
}

K_THREAD_DEFINE(publisher_tid, STACK_SIZE, publisher_fn,
		NULL, NULL, NULL, PRIO_PUBLISHER, 0, 0);
K_THREAD_DEFINE(logger_tid, STACK_SIZE, logger_fn,
		NULL, NULL, NULL, PRIO_LOGGER, 0, 0);

int main(void)
{
	LOG_INF("=== L4 zbus: sensor channel ===");
	LOG_INF("publish every %dms, %d samples", PUBLISH_PERIOD_MS, SAMPLE_COUNT);
	return 0;
}
