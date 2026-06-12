#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/kernel.h>

#include "demo_protocol.h"

static K_SEM_DEFINE(endpoint_bound_sem, 0, 1);

static struct ipc_ept remote_ept;

static int send_message_blocking(struct demo_message *msg)
{
	int ret;

	while (true) {
		ret = ipc_service_send(&remote_ept, msg, sizeof(*msg));
		if (ret == -ENOMEM) {
			continue;
		}

		return ret;
	}
}

static uint32_t process_job(uint32_t operand)
{
	return operand * operand;
}

static void endpoint_bound(void *priv)
{
	ARG_UNUSED(priv);

	k_sem_give(&endpoint_bound_sem);
}

static void endpoint_error(const char *message, void *priv)
{
	ARG_UNUSED(priv);
	ARG_UNUSED(message);
}

static void endpoint_received(const void *data, size_t len, void *priv)
{
	struct demo_message response;
	int ret;

	ARG_UNUSED(priv);

	if (len != sizeof(response)) {
		return;
	}

	memcpy(&response, data, sizeof(response));

	if (!demo_message_valid(&response) ||
	    !demo_message_payload_valid(&response) ||
	    response.type != DEMO_MSG_JOB) {
		return;
	}

	response.type = DEMO_MSG_RESULT;
	response.result = process_job(response.operand);

	ret = send_message_blocking(&response);
	ARG_UNUSED(ret);
}

static struct ipc_ept_cfg remote_ept_cfg = {
	.name = "demo_ep",
	.cb = {
		.bound = endpoint_bound,
		.received = endpoint_received,
		.error = endpoint_error,
	},
};

int main(void)
{
	const struct device *ipc0 = DEVICE_DT_GET(DT_NODELABEL(ipc0));
	int ret;

	ret = ipc_service_open_instance(ipc0);
	if (ret < 0 && ret != -EALREADY) {
		return ret;
	}

	ret = ipc_service_register_endpoint(ipc0, &remote_ept, &remote_ept_cfg);
	if (ret < 0) {
		return ret;
	}

	k_sem_take(&endpoint_bound_sem, K_FOREVER);
	k_sleep(K_FOREVER);

	return 0;
}
