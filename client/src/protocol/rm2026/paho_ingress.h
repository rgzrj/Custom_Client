/* Project-local guards injected into the pinned historical Paho packet factory.
 * Not a general MQTT client: only this read-only MQTT 3.1.1 profile is accepted.
 * Called under Paho's receive mutex; no GUI work or new packet queue.
 */
#ifndef RM2026_PAHO_INGRESS_H
#define RM2026_PAHO_INGRESS_H

/* Project limits, not referee protocol constants. Topic <=64, payload <=64 KiB. */
static int rm2026_header_allowed(unsigned char header, size_t length)
{
    if (header == 0x20) return length == 2; /* CONNACK */
    if (header == 0x90) return length == 3; /* One-topic SUBACK */
    if (header == 0xd0) return length == 0; /* PINGRESP */
    return (header >> 4) == 3 && ((header >> 1) & 3) <= 1
        && length >= 3 && length <= 65536 + 64 + 4;
}

static int rm2026_body_allowed(unsigned char header, const char* data, size_t length)
{
    /* Validate lengths before upstream packet constructors allocate topic/acks. */
    if ((header >> 4) == 3) {
        const size_t topic = ((unsigned char)data[0] << 8) | (unsigned char)data[1];
        const size_t variable = 2 + topic + (((header >> 1) & 3) ? 2 : 0);
        if (topic == 0 || topic > 64 || variable > length || length - variable > 65536)
            return 0;
        if (memchr(data + 2, 0, topic) || memchr(data + 2, '+', topic) || memchr(data + 2, '#', topic))
            return 0;
        if (((header >> 1) & 3) && data[variable - 2] == 0 && data[variable - 1] == 0)
            return 0;
    } else if (header == 0x20) {
        if (data[0] != 0 || (unsigned char)data[1] > 5) return 0; /* Clean session. */
    } else if (header == 0x90) {
        if ((data[0] == 0 && data[1] == 0)
            || ((unsigned char)data[2] > 2 && (unsigned char)data[2] != 0x80)) return 0;
    }
    return 1;
}

static int rm2026_receive_budget(size_t length)
{
    /* ponytail: process-wide budget, conservative for the single historical
     * observer. Use per-client budgets only if multiple adapters are required.
     * Count complete packets once, not each retry of an interrupted body read.
     */
    static START_TIME_TYPE window;
    static unsigned packets = 0;
    static size_t bytes = 0;
    static int started = 0;
    if (!started || MQTTTime_elapsed(window) >= 1000) {
        window = MQTTTime_now(); packets = 0; bytes = 0; started = 1;
    }
    if (packets >= 256 || length > 1048576 - bytes) return 0;
    ++packets;
    bytes += length;
    return 1;
}
#endif
