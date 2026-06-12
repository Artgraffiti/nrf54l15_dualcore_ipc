#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/sys/printk.h>

#include "demo_protocol.h"

static K_SEM_DEFINE(endpoint_bound_sem, 0, 1);
static K_SEM_DEFINE(result_sem, 0, 1);

static struct ipc_ept host_ept;
static struct demo_message last_result;

static void endpoint_bound(void *priv)
{
	ARG_UNUSED(priv);

	printk("IPC endpoint bound, CPUFLPR image is reachable\n");
	k_sem_give(&endpoint_bound_sem);
}

static void endpoint_error(const char *message, void *priv)
{
	ARG_UNUSED(priv);

	printk("IPC backend error: %s\n", message);
}

static void endpoint_received(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(priv);

	if (len != sizeof(last_result)) {
		printk("Unexpected response size: %u\n", (unsigned int)len);
		return;
	}

	memcpy(&last_result, data, sizeof(last_result));

	if (!demo_message_valid(&last_result) || last_result.type != DEMO_MSG_RESULT) {
		printk("Unexpected response payload\n");
		return;
	}

	k_sem_give(&result_sem);
}

static struct ipc_ept_cfg host_ept_cfg = {
	.name = "demo_ep",
	.cb = {
		.bound = endpoint_bound,
		.received = endpoint_received,
		.error = endpoint_error,
	},
};

static int send_message_blocking(struct demo_message *msg)
{
	int ret;

	while (true) {
		ret = ipc_service_send(&host_ept, msg, sizeof(*msg));
		if (ret == -ENOMEM) {
			k_yield();
			continue;
		}

		return ret;
	}
}

static int result_message_valid(const struct demo_message *expected,
				const struct demo_message *received)
{
	if (!demo_message_valid(received) ||
	    !demo_message_payload_valid(received) ||
	    received->type != DEMO_MSG_RESULT) {
		return 0;
	}

	return received->sequence == expected->sequence &&
	       received->operand == expected->operand &&
	       received->result == (expected->operand * expected->operand) &&
	       received->payload_hash == expected->payload_hash &&
	       memcmp(received->payload, expected->payload, sizeof(received->payload)) == 0;
}

static void print_rate_line(const char *label, uint64_t numerator, uint64_t denominator,
			    const char *unit)
{
	uint64_t whole = numerator / denominator;
	uint64_t fraction = ((numerator % denominator) * 100u) / denominator;

	printk("%s: %llu.%02llu %s\n", label, whole, fraction, unit);
}

int main(void)
{
	const struct device *ipc0 = DEVICE_DT_GET(DT_NODELABEL(ipc0));
	const size_t message_size = sizeof(struct demo_message);
	uint64_t total_cycles = 0;
	uint64_t min_cycles = UINT64_MAX;
	uint64_t max_cycles = 0;
	int ret;

	printk("nrf54l15_dualcore_ipc started\n");
	printk("IPC ping-pong benchmark: payload=%u bytes, message=%u bytes, iterations=%u\n",
	       CONFIG_DEMO_PAYLOAD_SIZE, (unsigned int)message_size, CONFIG_DEMO_JOB_COUNT);

	ret = ipc_service_open_instance(ipc0);
	if (ret < 0 && ret != -EALREADY) {
		printk("ipc_service_open_instance() failed: %d\n", ret);
		return ret;
	}

	ret = ipc_service_register_endpoint(ipc0, &host_ept, &host_ept_cfg);
	if (ret < 0) {
		printk("ipc_service_register_endpoint() failed: %d\n", ret);
		return ret;
	}

	k_sem_take(&endpoint_bound_sem, K_FOREVER);

	for (uint32_t seq = 1; seq <= CONFIG_DEMO_JOB_COUNT; ++seq) {
		struct demo_message job = demo_job_message(seq, seq + 2U);
		uint64_t started_at = k_cycle_get_64();
		uint64_t elapsed_cycles;

		ret = send_message_blocking(&job);
		if (ret < 0) {
			printk("ipc_service_send() failed: %d\n", ret);
			return ret;
		}

		ret = k_sem_take(&result_sem, K_SECONDS(2));
		if (ret < 0) {
			printk("Timed out waiting for CPUFLPR response\n");
			return ret;
		}

		if (!result_message_valid(&job, &last_result)) {
			printk("Received invalid result for job %u\n", seq);
			return -EINVAL;
		}

		elapsed_cycles = k_cycle_get_64() - started_at;
		total_cycles += elapsed_cycles;
		if (elapsed_cycles < min_cycles) {
			min_cycles = elapsed_cycles;
		}
		if (elapsed_cycles > max_cycles) {
			max_cycles = elapsed_cycles;
		}

		if (CONFIG_DEMO_PROGRESS_INTERVAL > 0 &&
		    (seq % CONFIG_DEMO_PROGRESS_INTERVAL) == 0U) {
			uint64_t elapsed_us = k_cyc_to_us_floor64(total_cycles);

			printk("Progress: %u/%u round trips, average RTT %llu us\n",
			       seq, CONFIG_DEMO_JOB_COUNT, elapsed_us / seq);
		}
	}

	{
		uint64_t total_us = k_cyc_to_us_floor64(total_cycles);
		uint64_t avg_ns = k_cyc_to_ns_floor64(total_cycles) / CONFIG_DEMO_JOB_COUNT;
		uint64_t min_ns = k_cyc_to_ns_floor64(min_cycles);
		uint64_t max_ns = k_cyc_to_ns_floor64(max_cycles);
		uint64_t total_bytes = (uint64_t)CONFIG_DEMO_JOB_COUNT * message_size * 2u;

		printk("\nBenchmark completed.\n");
		printk("Round trips: %u\n", CONFIG_DEMO_JOB_COUNT);
		printk("Total time: %llu us\n", total_us);
		printk("RTT min/avg/max: %llu ns / %llu ns / %llu ns\n",
		       min_ns, avg_ns, max_ns);
		print_rate_line("Exchange frequency", (uint64_t)CONFIG_DEMO_JOB_COUNT * USEC_PER_SEC,
				total_us, "Hz");
		print_rate_line("Message rate", (uint64_t)CONFIG_DEMO_JOB_COUNT * 2u * USEC_PER_SEC,
				total_us, "msg/s");
		print_rate_line("Data rate", total_bytes * USEC_PER_SEC, total_us, "B/s");
	}

	while (true) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
