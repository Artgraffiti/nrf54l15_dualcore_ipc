#ifndef DEMO_PROTOCOL_H_
#define DEMO_PROTOCOL_H_

#include <stdint.h>

#define DEMO_PROTOCOL_MAGIC 0x49504331u

enum demo_message_type {
	DEMO_MSG_JOB = 1,
	DEMO_MSG_RESULT = 2,
};

struct demo_message {
	uint32_t magic;
	uint16_t type;
	uint16_t reserved;
	uint32_t sequence;
	uint32_t operand;
	uint32_t result;
	uint32_t payload_hash;
	uint8_t payload[CONFIG_DEMO_PAYLOAD_SIZE];
};

static inline uint32_t demo_payload_hash(const uint8_t *payload)
{
	uint32_t hash = 5381u;

	for (uint32_t i = 0; i < CONFIG_DEMO_PAYLOAD_SIZE; ++i) {
		hash = ((hash << 5) + hash) ^ payload[i];
	}

	return hash;
}

static inline void demo_fill_payload(struct demo_message *msg)
{
	for (uint32_t i = 0; i < CONFIG_DEMO_PAYLOAD_SIZE; ++i) {
		msg->payload[i] = (uint8_t)(msg->sequence + msg->operand + (i * 17u));
	}
}

static inline struct demo_message demo_job_message(uint32_t sequence, uint32_t operand)
{
	struct demo_message msg = {
		.magic = DEMO_PROTOCOL_MAGIC,
		.type = DEMO_MSG_JOB,
		.sequence = sequence,
		.operand = operand,
	};

	demo_fill_payload(&msg);
	msg.payload_hash = demo_payload_hash(msg.payload);

	return msg;
}

static inline int demo_message_valid(const struct demo_message *msg)
{
	return msg->magic == DEMO_PROTOCOL_MAGIC;
}

static inline int demo_message_payload_valid(const struct demo_message *msg)
{
	return msg->payload_hash == demo_payload_hash(msg->payload);
}

#endif /* DEMO_PROTOCOL_H_ */
