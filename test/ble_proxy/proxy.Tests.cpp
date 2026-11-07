#include "BleApiService.h"
#include "BleClient.h"
#include "esp_log.h"
#include <iostream>
#include "PresetManager.h"
#include "nvs.h"
#include "host/ble_gatt.h"

class Reply : public BleEndpoint
{
public:
    std::vector<uint8_t> packet;
    bool Send(const uint8_t *data, size_t size) override
    {
        packet.assign(data, data + size);
        return true;
    }
};

static void presetTests()
{
    PresetManager manager;
    assert(manager.setup());
    Reply reply;
    auto request = [&](uint8_t command, const std::vector<uint8_t> &payload, uint8_t status = 0)
    {
        std::vector<uint8_t> bytes = {1, command, 255};
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        assert(manager.handleApi(bytes.data(), bytes.size(), reply));
        assert(reply.packet[0] == 1 && reply.packet[1] == command && reply.packet[2] == 255);
        assert(reply.packet[3] == status);
        assert(reply.packet.size() == (command == 1 && status == 0 ? 132 : 4));
    };
    request(1, {});
    std::vector<uint8_t> map(reply.packet.begin() + 4, reply.packet.end());
    assert(map[0] == 0x80 && map[1] == 0x81);
    request(4, map);
    request(1, {});
    assert(std::vector<uint8_t>(reply.packet.begin() + 4, reply.packet.end()) == map);
    map.assign(128, 0xe3);
    map.back() = 0x7f;
    request(4, map);
    const auto stored = mockStored;
    auto invalid = map;
    invalid[0] = 0xe4;
    request(4, invalid, 2);
    invalid[0] = 0xff;
    request(4, invalid, 2);
    invalid.pop_back();
    request(4, invalid, 1);
    invalid.push_back(0);
    invalid.push_back(0);
    request(4, invalid, 1);
    request(2, {}, 1);
    request(1, {0}, 1);
    mockStorageFailure = true;
    request(4, std::vector<uint8_t>(128, 0x80), 3);
    mockStorageFailure = false;
    assert(mockStored == stored);
    PresetManager restored;
    assert(restored.setup());
    const uint8_t info[] = {1, 1, 255};
    assert(restored.handleApi(info, sizeof(info), reply));
    assert(std::vector<uint8_t>(reply.packet.begin() + 4, reply.packet.end()) == map);
    const uint8_t badVersion[] = {2, 1, 255};
    manager.handleApi(badVersion, sizeof(badVersion), reply);
    assert(reply.packet[3] == 1);
    manager.handleApi(nullptr, 0, reply);
    assert(reply.packet[3] == 1);

    NimBLEDevice::server.mtu = 135;
    BleApiService api;
    BleClient client;
    api.setup(&client, &manager);
    client.setup(&api);
    assert(client.Connect(NimBLEAddress{}));
    NimBLEDevice::client.mtu = 135;
    NimBLEConnInfo controller;
    api.onConnect(&NimBLEDevice::server, controller);
    auto &local = NimBLEDevice::server.service.characteristic;
    auto &configuration = NimBLEDevice::server.configurationService.characteristic;
    assert(configuration.properties == (NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY));
    local.subscribed = configuration.subscribed = true;
    api.onSubscribe(&local, controller, 1);
    api.onSubscribe(&configuration, controller, 1);
    const int writes = NimBLEDevice::client.service.characteristic.writes;
    configuration.value.assign(info, info + sizeof(info));
    api.onWrite(&configuration, controller);
    api.processNotifications();
    assert(configuration.notified.size() == 132 && configuration.notified[2] == 255);
    assert(NimBLEDevice::client.service.characteristic.writes == writes);
    api.onWrite(&configuration, controller);
    api.onSubscribe(&local, controller, 0);
    api.processNotifications();
    assert(configuration.notifications == 2);
    api.onWrite(&configuration, controller);
    api.onSubscribe(&configuration, controller, 0);
    api.onSubscribe(&configuration, controller, 1);
    api.processNotifications();
    assert(configuration.notifications == 2);
    api.onWrite(&configuration, controller);
    mockTicks += 2000;
    api.processNotifications();
    assert(configuration.notifications == 2);
    NimBLEDevice::server.mtu = 134;
    api.onWrite(&configuration, controller);
    api.processNotifications();
    assert(configuration.notifications == 2);
    NimBLEDevice::server.mtu = 135;
    local.value.assign(info, info + sizeof(info));
    api.onWrite(&local, controller);
    assert(NimBLEDevice::client.service.characteristic.writes == writes + 1);
    configuration.value = {1, 4, 255};
    configuration.value.insert(configuration.value.end(), map.begin(), map.end());
    api.onWrite(&configuration, controller);
    api.processNotifications();
    assert(configuration.notified == std::vector<uint8_t>({1, 4, 255, 0}));
    assert(NimBLEDevice::client.service.characteristic.writes == writes + 1);
    api.onSubscribe(&local, controller, 1);
    api.onSubscribe(&configuration, controller, 0);
    const uint8_t midi[] = {0x80, 0x80, 0xf0, 0x7d, 0x47, 0x50, 1, 1, 1, 0, 0xf7};
    assert(api.Send(midi, sizeof(midi)));
    api.processNotifications();
    assert(local.notified == std::vector<uint8_t>(midi, midi + sizeof(midi)));
    local.value.assign(midi, midi + sizeof(midi));
    api.onWrite(&local, controller);
    assert(NimBLEDevice::client.service.characteristic.written == local.value);
    std::cout << "PASS: binary configuration round-trip, validation, persistence, MTU and characteristic isolation.\n";
}

static void notificationTests()
{
    BleApiService api;
    BleClient client;
    api.setup(&client);
    auto &server = NimBLEDevice::server;
    auto &local = server.service.characteristic;
    server.mtu = 256;
    local.subscribed = true;
    local.notifyResult = true;
    NimBLEConnInfo controller;
    controller.handle = 1;
    api.onConnect(&server, controller);
    assert(server.parameterUpdates > 0);
    assert(!api.Send(nullptr, 0));
    api.onSubscribe(&local, controller, 1);

    std::vector<uint8_t> packet(212, 0);
    mockSentPackets.clear();
    // Entire name-list burst is copied while the BLE host keeps processing input.
    for (uint8_t batch = 0; batch < 20; ++batch)
    {
        packet[7] = batch;
        assert(api.Send(packet.data(), packet.size()));
    }
    packet[7] = 99;
    assert(mockSentPackets.empty());
    mockAllocationFailures = 2;
    api.processNotifications();
    api.processNotifications();
    assert(mockSentPackets.empty());
    mockNotifyCode = BLE_HS_ENOMEM;
    api.processNotifications();
    assert(mockSentPackets.empty());
    mockTicks += 2500;
    mockNotifyCode = 0;
    for (int batch = 0; batch < 20; ++batch)
    {
        mockTicks += 20;
        api.processNotifications();
        assert(mockSentPackets.back().size() == 212);
        assert(mockSentPackets.back()[7] == batch);
    }
    assert(mockSentPackets.size() == 20);

    assert(api.Send(packet.data(), packet.size()));
    mockNotifyCode = BLE_HS_EBUSY;
    api.processNotifications();
    mockNotifyCode = BLE_HS_EAGAIN;
    api.processNotifications();
    mockNotifyCode = 0;
    api.processNotifications();
    assert(mockSentPackets.size() == 21);

    assert(api.Send(packet.data(), packet.size()));
    mockNotifyCode = BLE_HS_ENOMEM;
    api.processNotifications();
    mockTicks += 9999;
    api.processNotifications();
    assert(mockSentPackets.size() == 21);
    mockTicks += 1;
    const int errors = mockErrors;
    api.processNotifications();
    assert(mockErrors > errors);
    mockNotifyCode = 0;
    api.processNotifications();
    assert(mockSentPackets.size() == 21);

    for (int i = 0; i < 32; ++i) assert(api.Send(packet.data(), packet.size()));
    assert(!api.Send(packet.data(), packet.size()));
    api.onDisconnect(&server, controller, 0);
    api.onConnect(&server, controller);
    api.onSubscribe(&local, controller, 1);
    for (int i = 0; i < 32; ++i) api.processNotifications();
    assert(mockSentPackets.size() == 21);

    assert(api.Send(packet.data(), packet.size()));
    api.onSubscribe(&local, controller, 0);
    api.onSubscribe(&local, controller, 1);
    api.processNotifications();
    assert(mockSentPackets.size() == 21);

    assert(api.Send(packet.data(), packet.size()));
    mockNotifyCode = 7;
    api.processNotifications();
    mockNotifyCode = 0;
    api.processNotifications();
    assert(mockSentPackets.size() == 21);
    assert(api.Send(packet.data(), packet.size()));
    server.mtu = 23;
    api.processNotifications();
    assert(mockSentPackets.size() == 21);
    std::cout << "PASS: 20-packet name burst, buffer retries, ordering, expiry, capacity and stale subscriptions.\n";
}

int main()
{
    BleClient client;
    BleApiService api;
    std::vector<uint8_t> packet(24);
    for (size_t i = 0; i < packet.size(); ++i)
        packet[i] = static_cast<uint8_t>(i * 11);

    assert(!client.Send(packet.data(), packet.size()));
    assert(!api.Send(packet.data(), packet.size()));
    client.setup(&api);
    api.setup(&client);
    assert(client.Connect(NimBLEAddress{}));

    auto& server = NimBLEDevice::server;
    auto& local = server.service.characteristic;
    auto& remote = NimBLEDevice::client.service.characteristic;
    assert(server.started);
    assert(local.properties & NIMBLE_PROPERTY::WRITE);
    assert(local.properties & NIMBLE_PROPERTY::WRITE_NR);
    assert(local.properties & NIMBLE_PROPERTY::NOTIFY);

    NimBLEConnInfo controller;
    server.callbacks->onConnect(&server, controller);
    local.subscribed = true;
    local.callbacks->onSubscribe(&local, controller, 1);
    const int subscriptionErrors = mockErrors;
    local.callbacks->onStatus(&local, controller, 0);
    assert(mockErrors == subscriptionErrors);
    local.callbacks->onStatus(&local, controller, 6);
    assert(mockErrors == subscriptionErrors + 1);

    local.value = packet;
    local.callbacks->onWrite(&local, controller);
    assert(remote.writes == 1 && remote.written == packet && !remote.lastResponse);
    assert(local.notifications == 0);

    remote.callback(&remote, packet.data(), packet.size(), true);
    assert(local.notifications == 0);
    api.processNotifications();
    assert(local.notifications == 1 && local.notified == packet && local.lastHandle == 0);
    assert(remote.writes == 1);
    assert(mockLocksHeld == 0);

    // Direct button sends still use the existing no-response path.
    assert(client.Send(packet.data(), packet.size()));
    assert(remote.writes == 2 && remote.written == packet && !remote.lastResponse);
    assert(local.notifications == 1);

    packet.push_back(0xF7);
    local.value = packet;
    const int writes = remote.writes;
    const int notifications = local.notifications;
    assert(!client.Send(packet.data(), packet.size()));
    assert(!api.Send(packet.data(), packet.size()));
    assert(remote.writes == writes && local.notifications == notifications);
    NimBLEDevice::client.mtu = server.mtu = 28;
    assert(client.Send(packet.data(), packet.size()));
    assert(api.Send(packet.data(), packet.size()));
    api.processNotifications();
    assert(remote.written == packet && local.notified == packet);

    remote.writeResult = false;
    local.notifyResult = false;
    assert(!client.Send(packet.data(), packet.size()));
    assert(api.Send(packet.data(), packet.size()));
    api.processNotifications();
    int errors = mockErrors;
    local.callbacks->onWrite(&local, controller);
    remote.callback(&remote, packet.data(), packet.size(), true);
    api.processNotifications();
    assert(mockErrors >= errors + 3);
    remote.writeResult = local.notifyResult = true;

    local.subscribed = false;
    api.onSubscribe(&local, controller, 0);
    assert(!api.Send(packet.data(), packet.size()));
    local.subscribed = true;
    api.onSubscribe(&local, controller, 1);
    server.callbacks->onDisconnect(&server, controller, 0);
    assert(!api.Send(packet.data(), packet.size()));
    assert(server.advertising.starts == 2);

    controller.handle = 7;
    server.callbacks->onConnect(&server, controller);
    api.onSubscribe(&local, controller, 1);
    assert(api.Send(packet.data(), packet.size()));
    api.processNotifications();
    assert(local.lastHandle == 7);
    NimBLEConnInfo unrelated;
    unrelated.handle = 9;
    server.callbacks->onDisconnect(&server, unrelated, 0);
    assert(api.Send(packet.data(), packet.size()));
    api.processNotifications();
    assert(local.lastHandle == 7);

    NimBLEDevice::client.disconnect();
    assert(!client.IsConnected());
    errors = mockErrors;
    local.callbacks->onWrite(&local, controller);
    assert(mockErrors >= errors + 2);
    assert(client.Connect(NimBLEAddress{}));
    local.callbacks->onWrite(&local, controller);
    assert(remote.written == packet && !remote.lastResponse);

    assert(!client.Send(nullptr, 1));
    assert(!api.Send(nullptr, 1));
    NimBLEDevice::client.mtu = server.mtu = 2;
    assert(!client.Send(packet.data(), packet.size()));
    assert(!api.Send(packet.data(), packet.size()));
    assert(mockLocksHeld == 0);

    std::cout << "PASS: bidirectional BLE proxy, reconnects, MTU boundaries and failures.\n";
    presetTests();
    notificationTests();
}
