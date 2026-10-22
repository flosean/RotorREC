#include <assert.h>
#include <stdio.h>
#define RR_CONNECT_HOST_TEST
#include "../components/dji_camera_sdk/logic/connect_logic.c"

test_ble_profile_t s_ble_profile;
bool camera_status_initialized;
static bool missing_ack, rejected, truncated;
static unsigned waits, replies, scans, disconnects;
void receive_camera_notify_handler(const uint8_t *data, size_t length) { (void)data; (void)length; }
void ble_set_notify_callback(void (*callback)(const uint8_t *, size_t)) { (void)callback; }
void ble_set_state_callback(void (*callback)(void)) { (void)callback; }
void ble_set_reconnecting(bool reconnecting) { (void)reconnecting; }
int ble_init(void) { return 0; }
int ble_start_scanning_and_connect(void) { ++scans; return 0; }
bool ble_connect_attempt_finished(void) { return true; }
int ble_cancel_pending_connect(void) { return 0; }
int ble_disconnect(void) { ++disconnects; s_ble_profile.connection_status.is_connected = false; receive_camera_disconnect_handler(); return 0; }
int ble_register_notify(uint16_t connection, uint16_t handle) { (void)connection; (void)handle; return 0; }
int ble_start_advertising(void) { return 0; }
void vTaskDelay(unsigned ticks) { (void)ticks; }
uint16_t generate_seq(void) { return 1; }
CommandResult send_command(uint8_t set, uint8_t id, uint8_t type,
    const void *frame, uint16_t seq, int timeout)
{
    (void)frame; (void)seq; (void)timeout;
    assert(set == 0 && id == 0x19);
    if (type == ACK_NO_RESPONSE) { ++replies; return (CommandResult){0}; }
    if (missing_ack) return (CommandResult){0};
    return (CommandResult){calloc(1, sizeof(connection_request_response_frame)), sizeof(connection_request_response_frame)};
}
int data_wait_for_result_by_cmd(uint8_t set, uint8_t id, int timeout,
    uint16_t *seq, void **result, size_t *length)
{
    (void)timeout; assert(set == 0 && id == 0x19);
    ++waits;
    connection_request_command_frame *request = calloc(1, sizeof(*request));
    request->verify_mode = 2; request->verify_data = rejected ? 1 : 0;
    *seq = 2; *result = request; *length = truncated ? 1 : sizeof(*request);
    return 0;
}
int main(void)
{
    int8_t mac[6] = {0};
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        missing_ack = scenario == 1; rejected = scenario == 2; truncated = scenario == 3;
        waits = replies = disconnects = 0; connect_state = BLE_CONNECTED;
        s_ble_profile.connection_status.is_connected = true;
        int result = connect_logic_protocol_connect(1, sizeof(mac), mac, 0, 1, 1234, 0);
        assert(waits == 1); /* Includes approval arriving without a request ACK. */
        if (scenario < 2) assert(result == 0 && replies == 1 && connect_state == PROTOCOL_CONNECTED);
        else assert(result == -1 && replies == 0 && disconnects == 1);
    }
    camera_status_initialized = true; connect_state = PROTOCOL_CONNECTED;
    receive_camera_disconnect_handler();
    assert(connect_state == BLE_INIT_COMPLETE && !camera_status_initialized && scans == 0);
    s_ble_profile.connection_status.is_connected = false;
    assert(connect_logic_ble_disconnect() == 0 && connect_state == BLE_INIT_COMPLETE);
    assert(connect_logic_protocol_connect(1, 6, mac, 0, 1, 1234, 0) == -1);
    puts("DJI handshake ACK/approval ordering, rejection, truncation and nonblocking disconnect passed.");
    return 0;
}
