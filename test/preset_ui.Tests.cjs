const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

test('toolbar has only reload and save and help panels are removed', () => {
  const html = fs.readFileSync(path.join(__dirname, '..', 'docs', 'index.html'), 'utf8');
  const toolbar = html.match(/<div class="toolbar">([\s\S]*?)<\/div>\s*<p id="names-status"/)[1];
  assert.ok(toolbar.indexOf('id="reload"') < toolbar.indexOf('id="save"'));
  assert.equal((toolbar.match(/<button /g) || []).length, 2);
  assert.doesNotMatch(html, /refresh-names/);
  assert.doesNotMatch(html, /Make it yours|One press, one action|Names from your GP-5|class="sidebar"|class="workspace"/);
});

class Element extends EventTarget {
  constructor(tag = 'div') {
    super(); this.tag = tag; this.children = []; this.dataset = {};
    this.classList = { toggle() {} }; this.textContent = ''; this.value = ''; this.attributes = {};
  }
  append(...children) { this.children.push(...children); }
  add(child) { this.append(child); }
  replaceChildren(...children) { this.children = children; }
  setAttribute(name, value) { this.attributes[name] = value; }
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

function workspace({ failNames = false, rejectSave = false, earlyNameEnd = false, nameBatches = 20, nameTimeoutMs = 12000 } = {}) {
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
  let syncStartedAt = null;
  function receive(packet, channel = characteristic) {
    const padded = new Uint8Array(packet.length + 6);
    padded.set(packet, 3);
    channel.value = new DataView(padded.buffer, 3, packet.length);
    channel.dispatchEvent(new Event('characteristicvaluechanged'));
  }
  function receiveNameEnd() {
    const end = new Uint8Array(16);
    end.set([0x80, 0x80, 0xf0]);
    end[5] = 1; end[6] = 5; end[7] = 1; end[8] = 4; end[15] = 0xf7;
    receive(end);
  }
  characteristic.properties = { write: true };
  characteristic.startNotifications = async () => characteristic;
  configuration.properties = { write: true };
  configuration.startNotifications = async () => configuration;
  characteristic.writeValueWithResponse = async (packet) => {
    const bytes = Array.from(packet); writes.push(bytes);
    assert.equal(bytes[0], 128, 'Only MIDI traffic belongs on the MIDI characteristic');
    if (bytes[3] === 0) {
      assert.deepEqual(bytes, [128, 128, 240, 0, 14, 0, 1, 0, 0, 0, 2, 1, 2, 4, 0, 247]);
      syncStartedAt = Date.now();
      if (earlyNameEnd) queueMicrotask(receiveNameEnd);
    }
    if (bytes[3] === 3) {
      assert.notEqual(syncStartedAt, null, 'Patch-list request requires start sync first');
      assert.ok(Date.now() - syncStartedAt >= 90, 'Allow the GP-5 to enter sync mode before requesting names');
      syncStartedAt = null;
      if (failNames) throw new Error('GP-5 is unavailable');
      if (earlyNameEnd) queueMicrotask(receiveNameEnd);
      for (let batch = 0; batch < nameBatches; batch++) queueMicrotask(() => receive(namePacket(batch)));
      queueMicrotask(receiveNameEnd);
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
    } } }, Uint8Array, DataView, console,
    setTimeout: (callback, delay) => setTimeout(callback, delay === 12000 ? nameTimeoutMs : delay), clearTimeout,
    Option: class extends Element {
      constructor(text, value) { super('option'); this.textContent = text; this.value = value; }
    }
  });
  const html = fs.readFileSync(path.join(__dirname, '..', 'docs', 'index.html'), 'utf8');
  vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], context);
  return { run: code => vm.runInContext(code, context), elements, writes, receive,
    receiveConfiguration: packet => receive(packet, configuration), device, saved: () => saved };
}

test('groups start collapsed and preserve expanded state across editor refreshes', () => {
  const app = workspace();
  app.run('groups = Array.from({length:16}, () => Array.from({length:8}, () => ({action:0,arg:0}))); render()');
  let cards = app.elements.get('groups').children;
  assert.equal(cards.length, 16);
  assert.ok(cards.every(card => card.tag === 'details' && card.open === false));
  assert.ok(cards.every(card => card.children[0].tag === 'summary'));
  cards[0].open = true;
  cards[12].open = true;
  app.run('render()');
  cards = app.elements.get('groups').children;
  assert.equal(cards[0].open, true);
  assert.equal(cards[12].open, true);
  assert.equal(cards[1].open, false);
  cards[0].open = false;
  app.run('render()');
  assert.equal(app.elements.get('groups').children[0].open, false);
});

test('compact groups keep named controls and paired navigation targets editable', () => {
  const app = workspace();
  app.run('groups = Array.from({length:16}, () => Array.from({length:8}, () => ({action:0,arg:0}))); render()');
  const card = app.elements.get('groups').children[12];
  const row = card.children[1];
  assert.equal(card.children[0].children[0].children.length, 1, 'No duplicate group heading');
  assert.equal(card.children[0].children[0].children[0].textContent, 'Extended group 1');
  const type = row.children[1].children[0];
  const targets = row.children[2].children[0];
  assert.equal(targets.className, 'target-controls navigation');
  assert.equal(targets.children.length, 2);
  const [group, slot] = targets.children;
  assert.match(type.attributes['aria-label'], /Extended group 1, action 1, type/);
  assert.match(group.attributes['aria-label'], /destination group/);
  assert.match(slot.attributes['aria-label'], /destination slot/);
  group.value = '15'; slot.value = '7'; slot.onchange();
  assert.equal(app.run('groups[12][0].arg'), 127);
  assert.equal(app.run('dirty'), true);
  type.value = '1'; type.onchange();
  const presetTargets = app.elements.get('groups').children[12].children[1].children[2].children[0];
  assert.equal(presetTargets.className, 'target-controls');
  assert.equal(presetTargets.children.length, 1);
  const preset = presetTargets.children[0];
  assert.match(preset.attributes['aria-label'], /preset/);
  preset.value = '99'; preset.onchange();
  assert.equal(app.run('groups[12][0].arg'), 99);
});

test('action lists include only slots through the first Navigate regardless of destination', () => {
  const app = workspace();
  app.run(`groups = Array.from({length:16}, () => Array.from({length:8}, () => ({action:1,arg:0})));
    groups[0][0] = {action:0,arg:0};
    groups[1][2] = {action:0,arg:0};
    groups[1][5] = {action:0,arg:0};
    groups[2][7] = {action:0,arg:0};
    groups[3][0] = {action:0,arg:1};
    groups[3][1] = {action:0,arg:16};
    groups[5][3] = {action:0,arg:16};
    groups[12][3] = {action:0,arg:127};
    render()`);
  const cards = app.elements.get('groups').children;
  assert.equal(cards[0].children.length - 1, 1);
  assert.equal(cards[0].children[0].children[1].textContent, '1 slot');
  assert.equal(cards[1].children.length - 1, 3);
  assert.equal(cards[1].children[0].children[1].textContent, '3 slots');
  assert.equal(cards[1].children[3].children[0].textContent, '03');
  assert.equal(cards[2].children.length - 1, 8);
  assert.equal(cards[3].children.length - 1, 1, 'Navigation to another group truncates');
  assert.equal(cards[4].children.length - 1, 8, 'Preset 0 does not truncate');
  assert.equal(cards[5].children.length - 1, 4, 'Navigation to another slot truncates');
  assert.equal(cards[12].children.length - 1, 4);
  assert.equal(app.run('groups.every(actions => actions.length === 8)'), true);
});

test('destination edits keep later slots hidden and type edits reveal through the next Navigate', () => {
  const app = workspace();
  app.run('groups = Array.from({length:16}, () => Array.from({length:8}, () => ({action:1,arg:99}))); groups[0][1] = {action:0,arg:0}; render()');
  let card = app.elements.get('groups').children[0];
  card.open = true;
  let targets = card.children[2].children[2].children[0].children;
  targets[0].value = '1'; targets[0].onchange();
  card = app.elements.get('groups').children[0];
  assert.equal(card.children.length - 1, 2);
  assert.equal(card.open, true);
  targets = card.children[2].children[2].children[0].children;
  targets[0].value = '0'; targets[0].onchange();
  card = app.elements.get('groups').children[0];
  assert.equal(card.children.length - 1, 2);
  targets = card.children[2].children[2].children[0].children;
  targets[1].value = '1'; targets[1].onchange();
  assert.equal(app.elements.get('groups').children[0].children.length - 1, 2);
  app.run('groups[0][1].arg = 0; render()');
  card = app.elements.get('groups').children[0];
  const type = card.children[2].children[1].children[0];
  type.value = '1'; type.onchange();
  card = app.elements.get('groups').children[0];
  assert.equal(card.children.length - 1, 8);
  assert.equal(card.open, true);
  assert.equal(app.run('dirty'), true);
  assert.equal(app.run('groups[0][7].arg'), 99);
  app.run('groups[0][1] = {action:0,arg:127}; groups[0][5] = {action:0,arg:16}; render()');
  const firstNavigate = app.elements.get('groups').children[0].children[2].children[1].children[0];
  firstNavigate.value = '1'; firstNavigate.onchange();
  card = app.elements.get('groups').children[0];
  assert.equal(card.children.length - 1, 6);
  assert.equal(card.open, true);
  assert.equal(card.children[0].children[1].textContent, '6 slots');
});

test('saving retains all hidden actions in the fixed device map', async () => {
  const app = workspace();
  await app.run('connect()');
  app.run('groups[0][1] = {action:0,arg:0}; groups[0][7] = {action:1,arg:99}; markDirty(); render()');
  assert.equal(app.elements.get('groups').children[0].children.length - 1, 2);
  await app.elements.get('save').onclick();
  assert.equal(app.saved().length, 128);
  assert.equal(app.saved()[1], 0);
  assert.equal(app.saved()[7], 227);
  assert.equal(app.run('dirty'), false);
});

test('connection controls show only the current action and notifications remain visible', async () => {
  const app = workspace();
  assert.equal(app.elements.get('connect').hidden, false);
  assert.equal(app.elements.get('disconnect').hidden, true);
  assert.equal(app.elements.get('device-info').hidden, true);
  await app.run('connect()');
  assert.equal(app.elements.get('connect').hidden, true);
  assert.equal(app.elements.get('disconnect').hidden, false);
  assert.equal(app.elements.get('device-info').hidden, false);
  assert.equal(app.elements.get('names-status').hidden, false);
  app.device.gatt.disconnect();
  assert.equal(app.elements.get('connect').hidden, false);
  assert.equal(app.elements.get('disconnect').hidden, true);
  assert.equal(app.elements.get('device-info').hidden, true);
  assert.equal(app.elements.get('message').hidden, false);
  assert.match(app.elements.get('message').textContent, /Disconnected/);
});

test('connect loads actions and all 100 GP-5 names, honoring DataView offsets', async () => {
  const app = workspace();
  await app.run('connect()');
  assert.equal(app.run('groups.length'), 16);
  assert.equal(app.run('groups[1][0].arg'), 99);
  assert.equal(app.run('presetLabel(0)'), '0 - Tone 0');
  assert.equal(app.run('presetLabel(99)'), '99 - Tone 99');
  assert.deepEqual(app.writes.filter(bytes => bytes[0] === 128).map(bytes => bytes[3]), [0, 3]);
  assert.deepEqual(app.writes.find(bytes => bytes[3] === 3),
    [128, 128, 240, 3, 5, 0, 1, 0, 0, 0, 2, 1, 2, 2, 4, 247]);
  assert.equal(app.elements.get('groups').children.length, 16);
});

test('reload replaces groups and refreshes names without collapsing an expanded group', async () => {
  const app = workspace();
  await app.run('connect()');
  app.elements.get('groups').children[0].open = true;
  app.run('groups[0][0] = {action:1,arg:99}; names[99] = "Stale name"; markDirty()');
  const reload = app.elements.get('reload').onclick();
  assert.equal(app.elements.get('reload').disabled, true);
  assert.equal(app.elements.get('save').disabled, true);
  await reload;
  assert.equal(app.run('groups[0][0].arg'), 0);
  assert.equal(app.run('dirty'), false);
  assert.equal(app.elements.get('reload').disabled, false);
  assert.equal(app.writes.filter(bytes => bytes[0] === 1 && bytes[1] === 1).length, 2);
  assert.deepEqual(app.writes.filter(bytes => bytes[0] === 128).map(bytes => bytes[3]), [0, 3, 0, 3]);
  assert.equal(app.run('presetLabel(99)'), '99 - Tone 99');
  assert.equal(app.elements.get('groups').children[0].open, true);
});

test('cancelled reload preserves edits and names without device requests', async () => {
  const app = workspace();
  await app.run('connect()');
  app.run('groups[0][0].arg = 99; markDirty(); window.confirm = () => false');
  const writesBefore = app.writes.length;
  await app.elements.get('reload').onclick();
  assert.equal(app.writes.length, writesBefore);
  assert.equal(app.run('groups[0][0].arg'), 99);
  assert.equal(app.run('dirty'), true);
  assert.equal(app.run('presetLabel(99)'), '99 - Tone 99');
});

test('reload still refreshes names after a group load failure and reports the error', async () => {
  const app = workspace();
  await app.run('connect()');
  app.run('loadGroups = async () => { throw new Error("Group load failed"); }; names[99] = "Stale name"');
  await app.elements.get('reload').onclick();
  assert.equal(app.run('presetLabel(99)'), '99 - Tone 99');
  assert.match(app.elements.get('message').textContent, /Cannot load preset actions: Group load failed/);
  assert.equal(app.elements.get('reload').disabled, false);
});

test('reload keeps freshly loaded groups when preset names are unavailable', async () => {
  const app = workspace({ failNames: true });
  await app.run('connect()');
  app.run('groups[0][0].arg = 99; markDirty()');
  await app.elements.get('reload').onclick();
  assert.equal(app.run('groups[0][0].arg'), 0);
  assert.equal(app.run('dirty'), false);
  assert.equal(app.run('loaded'), true);
  assert.equal(app.run('presetLabel(99)'), '99');
  assert.match(app.elements.get('message').textContent, /Preset names unavailable/);
  assert.equal(app.elements.get('reload').disabled, false);
});

test('normal end markers during sync and before name data do not abort loading', async () => {
  const app = workspace({ earlyNameEnd: true });
  await app.run('connect()');
  assert.deepEqual(app.writes.filter(bytes => bytes[0] === 128).map(bytes => bytes[3]), [0, 3]);
  assert.equal(app.run('presetLabel(0)'), '0 - Tone 0');
  assert.equal(app.run('presetLabel(99)'), '99 - Tone 99');
  assert.equal(app.run('namePending'), null);
});

test('incomplete names remain an error with received count and missing indexes', async () => {
  const app = workspace({ nameBatches: 19, nameTimeoutMs: 500 });
  await app.run('connect()');
  assert.equal(app.run('presetLabel(0)'), '0');
  assert.equal(app.run('namePending'), null);
  assert.match(app.elements.get('message').textContent, /received 95 \/ 100 names/);
  assert.match(app.elements.get('message').textContent, /Missing indexes: 95, 96, 97, 98, 99/);
  assert.match(app.elements.get('message').textContent, /Using indexes only/);
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
