/*
 * temp_alarm - Happy-path unit tests (homework skeleton)
 *
 * test_status_after_init is provided as a worked example. Finish Task 0 first,
 * then fill in the Task 1 stubs according to TEST_SPEC.md. The stubs call
 * ztest_test_skip(), so the binary builds and runs cleanly until each test is
 * implemented.
 *
 * Run:
 *   west twister -T tests/temp_alarm -p native_sim
 */

#include <errno.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "sensor_fake.h"
#include "temp_alarm.h"

#define DEFAULT_THRESHOLD 30

/*
 * Shared by every suite: a clean fake, then the module initialised
 * with the fake device. temp_alarm_init() resets all module state,
 * so no deinit is needed between tests.
 */
static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	sensor_fake_reset();
	sensor_fake_init();

	zassume_ok(temp_alarm_init(&fake_sensor_dev, DEFAULT_THRESHOLD),
		   "precondition: temp_alarm_init must succeed");
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_get_status
 *
 * The status right after a successful init.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_get_status, NULL, NULL, before, NULL, NULL);

/* PROVIDED — study this test before writing the rest. */
ZTEST(temp_alarm_get_status, test_status_after_init)
{
	struct temp_alarm_status status;

	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");

	zassert_equal(status.last_temp, 0, "last_temp must be 0 after init, got %d",
		      status.last_temp);
	zassert_equal(status.alarm_count, 0U, "alarm_count must be 0 after init, got %u",
		      status.alarm_count);
	zassert_false(status.is_alarming, "is_alarming must be false after init");
	zassert_equal(status.last_error, 0, "last_error must be 0 after init, got %d",
		      status.last_error);
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_check
 *
 * Readings below, at and above the threshold.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_check, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_check, test_below_threshold)
{
	struct temp_alarm_status status;

	sensor_fake_set_temperature(20);
	zassert_ok(temp_alarm_check(), "check must succeed");

	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
	zassert_equal(status.last_temp, 20, "last_temp must be 20, got %d", status.last_temp);
	zassert_false(status.is_alarming, "20 C must not raise the alarm");

	zassert_equal(fake_sensor_sample_fetch_fake.call_count, 1U,
		      "sample must be fetched exactly once");
	zassert_equal(fake_sensor_channel_get_fake.call_count, 1U,
		      "channel must be read exactly once");
	zassert_equal(fake_sensor_channel_get_fake.arg1_val, SENSOR_CHAN_AMBIENT_TEMP,
		      "must read the ambient temperature channel");
}

ZTEST(temp_alarm_check, test_at_threshold)
{
	struct temp_alarm_status status;

	sensor_fake_set_temperature(DEFAULT_THRESHOLD);
	zassert_ok(temp_alarm_check(), "check must succeed");

	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
	zassert_false(status.is_alarming, "a reading equal to the threshold must not alarm");
	zassert_equal(status.alarm_count, 0U, "alarm_count must stay 0, got %u",
		      status.alarm_count);
}

ZTEST(temp_alarm_check, test_above_threshold)
{
	struct temp_alarm_status status;

	sensor_fake_set_temperature(40);
	zassert_ok(temp_alarm_check(), "check must succeed");

	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
	zassert_equal(status.last_temp, 40, "last_temp must be 40, got %d", status.last_temp);
	zassert_true(status.is_alarming, "40 C must raise the alarm");
	zassert_equal(status.alarm_count, 1U, "alarm_count must be 1, got %u",
		      status.alarm_count);
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_threshold
 *
 * Changing the threshold at run time.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_threshold, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_threshold, test_new_threshold_clears_alarm)
{
	struct temp_alarm_status status;

	sensor_fake_set_temperature(40);
	zassert_ok(temp_alarm_check(), "first check must succeed");

	zassert_ok(temp_alarm_set_threshold(50), "set_threshold(50) must succeed");
	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
	zassert_false(status.is_alarming, "a new threshold must clear is_alarming");

	zassert_ok(temp_alarm_check(), "second check must succeed");
	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
	zassert_false(status.is_alarming, "40 C must not alarm against a 50 C threshold");
	zassert_equal(status.alarm_count, 1U, "alarm_count must stay at 1, got %u",
		      status.alarm_count);
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_reset
 *
 * temp_alarm_reset() clears the alarm but keeps the last reading.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_reset, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_reset, test_reset_clears_state)
{
	struct temp_alarm_status status;

	sensor_fake_set_temperature(40);
	for (int i = 0; i < 3; i++) {
		zassert_ok(temp_alarm_check(), "check %d must succeed", i);
	}

	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
	zassert_equal(status.alarm_count, 3U, "alarm_count must be 3 before reset, got %u",
		      status.alarm_count);

	temp_alarm_reset();

	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
	zassert_false(status.is_alarming, "reset must clear is_alarming");
	zassert_equal(status.alarm_count, 0U, "reset must clear alarm_count, got %u",
		      status.alarm_count);
	zassert_equal(status.last_error, 0, "reset must clear last_error, got %d",
		      status.last_error);
	zassert_equal(status.last_temp, 40, "reset must keep last_temp, got %d",
		      status.last_temp);
}
