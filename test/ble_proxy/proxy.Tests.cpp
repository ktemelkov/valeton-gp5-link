#include "BleApiService.h"
#include "BleClient.h"
#include "esp_log.h"
#include <iostream>

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

    local.value = packet;
    local.callbacks->onWrite(&local, controller);
    assert(remote.writes == 1 && remote.written == packet && !remote.lastResponse);
    assert(local.notifications == 0);

    remote.callback(&remote, packet.data(), packet.size(), true);
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
    assert(remote.written == packet && local.notified == packet);

    remote.writeResult = false;
    local.notifyResult = false;
    assert(!client.Send(packet.data(), packet.size()));
    assert(!api.Send(packet.data(), packet.size()));
    int errors = mockErrors;
    local.callbacks->onWrite(&local, controller);
    remote.callback(&remote, packet.data(), packet.size(), true);
    assert(mockErrors >= errors + 4);
    remote.writeResult = local.notifyResult = true;

    local.subscribed = false;
    assert(!api.Send(packet.data(), packet.size()));
    local.subscribed = true;
    server.callbacks->onDisconnect(&server, controller, 0);
    assert(!api.Send(packet.data(), packet.size()));
    assert(server.advertising.starts == 2);

    controller.handle = 7;
    server.callbacks->onConnect(&server, controller);
    assert(api.Send(packet.data(), packet.size()));
    assert(local.lastHandle == 7);
    NimBLEConnInfo unrelated;
    unrelated.handle = 9;
    server.callbacks->onDisconnect(&server, unrelated, 0);
    assert(api.Send(packet.data(), packet.size()));
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
}
