const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

class Element extends EventTarget {
  constructor(tag = 'div') {
    super(); this.tag = tag; this.children = []; this.dataset = {};
    this.classList = { toggle() {} }; this.textContent = ''; this.value = '';
  }
  append(...children) { this.children.push(...children); }
  add(child) { this.append(child); }
  replaceChildren(...children) { this.children = children; }
  setAttribute() {}
  querySelectorAll(selector) {
    const all = this.children.flatMap(child => [child, ...child.querySelectorAll('*')]);
    if (selector === '*') return all;
    if (selector === '[data-add]') return all.filter(child => 'add' in child.dataset);
    if (selector === '[data-remove-group]') return all.filter(child => 'removeGroup' in child.dataset);
    return all.filter(child => child.tag === 'button' || child.tag === 'select');
  }
}

function namePacket(batch, invalid = false) {
  const packet = new Uint8Array(212);
  packet.set([0x80, 0x80, 0xf0], 0);
  packet[5] = 1; packet[6] = 5; packet[7] = batch >> 4; packet[8] = batch & 15;
  packet[211] = 0xf7;
  for (let slot = 0; slot < 5; slot++) {
    const text = `Tone ${batch * 5 + slot}`;
    for (let i = 0; i < 10; i++) {
      const char = text.charCodeAt(i) || 0;
      packet[23 + slot * 40 + i * 2] = char >> 4;
      packet[24 + slot * 40 + i * 2] = char & 15;
    }
  }
  if (invalid) packet[23] = 16;
  return packet;
}

function workspace({ failNames = false, rejectSave = false } = {}) {
  const elements = new Map();
  const document = {
    getElementById(id) { if (!elements.has(id)) elements.set(id, new Element()); return elements.get(id); },
    createElement(tag) { return new Element(tag); }
  };
  const device = new EventTarget(), characteristic = new EventTarget(), configuration = new EventTarget();
  const writes = [];
  const remote = Array(128).fill(0);
  remote[0] = 128; remote[8] = 227; remote[127] = 127;
  let saved;
  function receive(packet, channel = characteristic) {
    const padded = new Uint8Array(packet.length + 6);
    padded.set(packet, 3);
    channel.value = new DataView(padded.buffer, 3, packet.length);
    channel.dispatchEvent(new Event('characteristicvaluechanged'));
  }
  characteristic.properties = { write: true };
  characteristic.startNotifications = async () => characteristic;
  configuration.properties = { write: true };
  configuration.startNotifications = async () => configuration;
  characteristic.writeValueWithResponse = async (packet) => {
    const bytes = Array.from(packet); writes.push(bytes);
    assert.equal(bytes[0], 128, 'Only MIDI traffic belongs on the MIDI characteristic');
    if (bytes[3] === 3) {
      if (failNames) throw new Error('GP-5 is unavailable');
      for (let batch = 0; batch < 20; batch++) queueMicrotask(() => receive(namePacket(batch)));
    }
  };
  configuration.writeValueWithResponse = async packet => {
    const bytes = Array.from(packet); writes.push(bytes);
    assert.equal(bytes[0], 1);
    const [command, token] = bytes.slice(1, 3), payload = bytes.slice(3);
    let result = [], status = 0;
    if (command === 1) { assert.equal(payload.length, 0); result = remote; }
    if (command === 4) {
      assert.equal(payload.length, 128);
      if (rejectSave) status = 3;
      else saved = payload;
    }
    queueMicrotask(() => receive([1, command, token, status, ...result], configuration));
  };
  device.name = 'GP5 Proxy';
  device.gatt = {
    connected: false,
    async connect() {
      this.connected = true;
      return { getPrimaryService: async uuid => ({ getCharacteristic: async char => {
        if (uuid === '9c7b1000-6e4d-4f42-9a31-2d5e8b0c7f60') {
          assert.equal(char, '9c7b1001-6e4d-4f42-9a31-2d5e8b0c7f60');
          return configuration;
        }
        assert.equal(uuid, '03b80e5a-ede8-4b33-a751-6ce34ec4c700');
        return characteristic;
      } }) };
    },
    disconnect() { this.connected = false; device.dispatchEvent(new Event('gattserverdisconnected')); }
  };
  const window = new EventTarget();
  window.isSecureContext = true; window.confirm = () => true;
  const context = vm.createContext({
    document, window, navigator: { bluetooth: { requestDevice: async (options) => {
      assert.equal(options.filters[0].name, 'GP5 Proxy');
      assert.ok(options.optionalServices.includes('9c7b1000-6e4d-4f42-9a31-2d5e8b0c7f60'));
      return device;
    } } }, Uint8Array, DataView, console, setTimeout, clearTimeout,
    Option: class extends Element {
      constructor(text, value) { super('option'); this.textContent = text; this.value = value; }
    }
  });
  const html = fs.readFileSync(path.join(__dirname, '..', 'docs', 'index.html'), 'utf8');
  vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], context);
  return { run: code => vm.runInContext(code, context), elements, writes, receive,
    receiveConfiguration: packet => receive(packet, configuration), device, saved: () => saved };
}

test('connect loads actions and all 100 GP-5 names, honoring DataView offsets', async () => {
  const app = workspace();
  await app.run('connect()');
  assert.equal(app.run('groups.length'), 16);
  assert.equal(app.run('groups[1][0].arg'), 99);
  assert.equal(app.run('presetLabel(0)'), '0 - Tone 0');
  assert.equal(app.run('presetLabel(99)'), '99 - Tone 99');
  assert.deepEqual(app.writes.find(bytes => bytes[3] === 3),
    [128, 128, 240, 3, 5, 0, 1, 0, 0, 0, 2, 1, 2, 2, 4, 247]);
  assert.equal(app.elements.get('groups').children.length, 16);
});

test('failed name fetch uses indexes only without losing loaded groups', async () => {
  const app = workspace({ failNames: true });
  await app.run('connect()');
  assert.equal(app.run('presetLabel(0)'), '0');
  assert.equal(app.run('presetLabel(99)'), '99');
  assert.equal(app.run('loaded'), true);
  assert.match(app.elements.get('message').textContent, /Using indexes only/);
});

test('fixed packed map, navigation targets, and one-message acknowledged save', async () => {
  const app = workspace();
  await app.run('connect()');
  assert.equal(app.run('normalCount'), 12);
  assert.equal(app.run('groups.length'), 16);
  assert.equal(app.run('groups[15][7].arg'), 127);
  app.run('groups[0][7] = {action:1,arg:99}; groups[0][6]={action:0,arg:127}; markDirty(); render()');
  await app.elements.get('save').onclick();
  assert.equal(app.saved().length, 128);
  assert.equal(app.saved()[7], 227);
  assert.equal(app.saved()[6], 127);
  assert.equal(app.saved()[127], 127);
  assert.equal(app.run('dirty'), false);
  assert.equal(app.writes.filter(bytes => bytes[0] === 1 && bytes[1] === 4).length, 1);
  assert.equal(app.writes.find(bytes => bytes[0] === 1 && bytes[1] === 4).length, 131);
  assert.throws(() => app.run('groups[0][0]={action:1,arg:100}; validateGroups(groups)'), /Invalid action/);
  assert.throws(() => app.run('groups[0].push({action:0,arg:0}); validateGroups(groups)'), /exactly 8/);
});

test('failed commit preserves dirty state and reports flash error', async () => {
  const app = workspace({ rejectSave: true });
  await app.run('connect()'); app.run('markDirty()');
  await app.elements.get('save').onclick();
  assert.equal(app.run('dirty'), true);
  assert.match(app.elements.get('message').textContent, /Flash save failed/);
  assert.equal(app.saved(), undefined);
});

test('disconnect rejects pending requests and malformed names are rejected', async () => {
  const app = workspace();
  await app.run('connect()');
  assert.throws(() => app.run(`parseNamePacket(new Uint8Array(${JSON.stringify(Array.from(namePacket(0, true)))}))`), /encoding/);
  app.run('configurationCharacteristic.writeValueWithResponse = async () => {}');
  const request = app.run('request(1,[],nextToken())');
  app.device.gatt.disconnect();
  await assert.rejects(request, /disconnected/);
  assert.equal(app.run('pending'), null);
  assert.equal(app.elements.get('save').disabled, true);
});

test('early notification does not allow overlapping GATT writes', async () => {
  const app = workspace();
  await app.run('connect()');
  let finishWrite;
  app.run('configurationCharacteristic.writeValueWithResponse = () => new Promise(resolve => { window.finishWrite = resolve; })');
  let finished = false;
  const request = app.run('request(1,[],nextToken())').then(() => { finished = true; });
  const token = app.run('pending.token');
  app.receiveConfiguration([1, 1, token, 0, ...Array(128).fill(0)]);
  await Promise.resolve();
  assert.equal(finished, false);
  finishWrite = app.run('window.finishWrite');
  finishWrite();
  await request;
  assert.equal(finished, true);
});

test('configuration notifications validate version, length, status and correlation', async () => {
  const app = workspace();
  await app.run('connect()');
  app.run('configurationCharacteristic.writeValueWithResponse = async () => {}');
  for (const packet of [[2, 1, 255, 0], [1, 1, 255], [1, 1, 255, 0], [1, 1, 255, 4]]) {
    const request = app.run('request(1,[],255)');
    app.receiveConfiguration(packet);
    await assert.rejects(request, /Invalid configuration response|Unknown firmware error/);
  }
  const request = app.run('request(1,[],255)');
  app.receiveConfiguration([1, 1, 254, 0, ...Array(128).fill(0)]);
  assert.notEqual(app.run('pending'), null);
  app.receiveConfiguration([1, 1, 255, 0, ...Array(128).fill(0)]);
  assert.equal((await request).length, 128);
});
