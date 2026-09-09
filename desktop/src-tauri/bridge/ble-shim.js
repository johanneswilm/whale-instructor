/* SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * WebBluetooth shim over the Tauri blec plugin (desktop builds only).
 * Injected by src-tauri/src/main.rs as a window initialization script, so the
 * plain browser build never sees it. It provides just enough of the
 * WebBluetooth surface for whale_instructor/static/js/whale_ble.js to run
 * unchanged:
 *
 *   navigator.bluetooth.requestDevice({filters: [{namePrefix}], optionalServices})
 *     -> device {id, name, gatt}
 *   device.gatt.connect() -> gatt server {connected, getPrimaryService(uuid)}
 *   service.getCharacteristic(uuid) -> {writeValueWithResponse(buf),
 *     startNotifications(), addEventListener('characteristicvaluechanged', cb)}
 *     where cb receives {target: {value: {buffer}}}
 *   device.addEventListener('gattserverdisconnected', cb)
 *   device.gatt.disconnect()
 *
 * Mapped onto tauri-plugin-blec (https://github.com/MnlPhlp/tauri-plugin-blec):
 *   scan            -> plugin:blec|scan   (Channel<BleDevice[]>)
 *   stopScan        -> plugin:blec|stop_scan
 *   connect         -> plugin:blec|connect(address, onDisconnect channel)
 *   disconnect      -> plugin:blec|disconnect
 *   write*          -> plugin:blec|send   (number[], writeType, service)
 *   notifications   -> plugin:blec|subscribe / unsubscribe (Channel<number[]>)
 * There is no chooser dialog in a desktop shell: requestDevice() scans for a
 * short window, matches the namePrefix filters and picks the strongest RSSI.
 */
(function () {
  'use strict';
  if (typeof window === 'undefined' || typeof navigator === 'undefined') return;
  if (navigator.bluetooth) return;              // real WebBluetooth wins
  if (!window.__WHALE_BLE_SHIM__) window.__WHALE_BLE_SHIM__ = {};

  var SCAN_TOTAL_MS = 6000;    // hard cap on the emulated chooser
  var SCAN_SETTLE_MS = 1500;   // pick once quiet this long after first match

  function makeChannel(invokeRoot, onmessage) {
    var core = invokeRoot;
    if (typeof core.Channel === 'function') {
      var ch = new core.Channel();
      ch.onmessage = onmessage;
      return ch;
    }
    // Stand-in for @tauri-apps/api's Channel on top of transformCallback.
    var chan = { onmessage: onmessage };
    chan.id = core.transformCallback(function (raw) {
      var msg = (raw && typeof raw === 'object' && 'message' in raw)
        ? raw.message : raw;
      if (chan.onmessage) chan.onmessage(msg);
    });
    chan.toJSON = function () { return '__CHANNEL__:' + chan.id; };
    return chan;
  }

  function addEmitter(obj) {
    var listeners = Object.create(null);
    obj.addEventListener = function (type, cb) {
      if (typeof cb !== 'function') return;
      (listeners[type] = listeners[type] || []).push(cb);
    };
    obj.removeEventListener = function (type, cb) {
      var l = listeners[type];
      if (!l) return;
      var i = l.indexOf(cb);
      if (i >= 0) l.splice(i, 1);
    };
    obj.dispatch = function (type, ev) {
      var l = listeners[type];
      if (!l) return;
      l.slice().forEach(function (cb) {
        try { cb(ev); } catch (e) { console.error(e); }
      });
    };
  }

  function toBytes(buf) {
    return Array.from(new Uint8Array(buf));
  }

  function makeCharacteristic(invoke, state, serviceUuid, uuid, reconnect) {
    var ch = { uuid: uuid, service: { uuid: serviceUuid }, value: null };
    addEmitter(ch);
    /* BlueZ can hand back an incomplete service list when GATT discovery
     * races the just-finished scan (the plugin then reports the
     * characteristic as "not available"), and a connect can race a still
     * tearing-down scan or disconnect ("In Progress"). Both mean: settle,
     * reconnect once, retry the operation. */
    function retryable(op) {
      return op().catch(function (e) {
        if (!e || !/not available|in progress/i.test(e.message || '') ||
            state._retried) {
          throw e;
        }
        state._retried = true;
        return reconnect().then(op);
      });
    }
    ch.startNotifications = function () {
      return retryable(function () {
        return invoke('plugin:blec|subscribe', {
          characteristic: uuid,
          service: serviceUuid,
          onData: makeChannel(state.api, function (data) {
            var bytes = Uint8Array.from(data || []);
            ch.value = { buffer: bytes.buffer };
            ch.dispatch('characteristicvaluechanged', { target: { value: ch.value } });
          })
        });
      });
    };
    ch.stopNotifications = function () {
      return invoke('plugin:blec|unsubscribe', {
        characteristic: uuid, service: serviceUuid
      });
    };
    ch.writeValueWithResponse = function (buf) {
      return retryable(function () {
        return invoke('plugin:blec|send', {
          characteristic: uuid,
          data: toBytes(buf),
          writeType: 'withResponse',
          service: serviceUuid
        });
      });
    };
    ch.writeValueWithoutResponse = function (buf) {
      return retryable(function () {
        return invoke('plugin:blec|send', {
          characteristic: uuid,
          data: toBytes(buf),
          writeType: 'withoutResponse',
          service: serviceUuid
        });
      });
    };
    ch.writeValue = ch.writeValueWithResponse;
    ch.readValue = function () {
      return invoke('plugin:blec|recv', {
        characteristic: uuid, service: serviceUuid
      }).then(function (data) {
        ch.value = { buffer: Uint8Array.from(data || []).buffer };
        return ch.value;
      });
    };
    return ch;
  }

  function wrapDevice(invoke, api, dev) {
    var state = { api: api, connected: false, suppressDisconnect: false };
    var device = {
      id: dev.address,
      name: dev.name || '',
      __whaleShim: true
    };
    addEmitter(device);

    function onDisconnected() {
      var was = state.connected;
      state.connected = false;
      if (state.suppressDisconnect) {   // we initiated it; whale_ble already knows
        state.suppressDisconnect = false;
        return;
      }
      if (was) device.dispatch('gattserverdisconnected', { target: device });
    }

    function settle(ms) {
      return new Promise(function (r) { setTimeout(r, ms); });
    }

    function ensureFree() {
      /* BlueZ refuses a connect while discovery is still tearing down
       * ("Btleplug error: In Progress"), and a link the plugin still holds
       * from a previously failed attempt makes GATT discovery unreliable
       * ("Characteristic not available"). Drain both before connecting. */
      return invoke('plugin:blec|stop_scan').catch(function () {})
        .then(function () { return settle(500); })
        .then(function () { return invoke('plugin:blec|disconnect'); })
        .catch(function () {})
        .then(function () { return settle(300); });
    }

    var gatt = {
      device: device,
      get connected() { return state.connected; },
      connect: function () {
        if (state.connected) return Promise.resolve(gatt);
        return ensureFree().then(function () {
          /* BlueZ may need more than one beat after a busy period;
           * retry "In Progress" with growing patience. */
          var attempt = function (n) {
            return invoke('plugin:blec|connect', {
              address: dev.address,
              onDisconnect: makeChannel(api, onDisconnected),
              allowIbeacons: false
            }).catch(function (e) {
              if (!e || !/in progress/i.test(e.message || '') || n >= 3) {
                throw e;
              }
              return settle(1200 * (n + 1)).then(function () {
                return attempt(n + 1);
              });
            });
          };
          return attempt(0);
        }).then(function () {
          /* give BlueZ a beat to settle GATT resolution */
          return settle(400);
        }).then(function () {
          state.connected = true;
          state._retried = false;   // fresh retry budget for this session
          return gatt;
        });
      },
      disconnect: function () {
        if (!state.connected) return;
        state.suppressDisconnect = true;
        state.connected = false;
        invoke('plugin:blec|disconnect').catch(function () {});
      },
      getPrimaryService: function (uuid) {
        var service = { uuid: uuid, device: device };
        service.getCharacteristic = function (charUuid) {
          return Promise.resolve(
            makeCharacteristic(invoke, state, uuid, charUuid, reconnect));
        };
        return Promise.resolve(service);
      }
    };

    function reconnect() {
      state.suppressDisconnect = true;   // plugin disconnect must not surface
      return invoke('plugin:blec|disconnect').catch(function () {})
        .then(function () { return settle(800); })
        .then(function () {
          return invoke('plugin:blec|connect', {
            address: dev.address,
            onDisconnect: makeChannel(api, onDisconnected),
            allowIbeacons: false
          });
        })
        .catch(function (e) {
          if (!e || !/in progress/i.test(e.message || '')) throw e;
          return settle(1500).then(function () {
            return invoke('plugin:blec|connect', {
              address: dev.address,
              onDisconnect: makeChannel(api, onDisconnected),
              allowIbeacons: false
            });
          });
        })
        .then(function () { return settle(400); })
        .then(function () {
          state.connected = true;
        });
    }
    device.gatt = gatt;
    return device;
  }

  /* --- candidate chooser (injected DOM, no page changes needed) --- */

  var chooserEl = null;

  function closeChooser() {
    if (chooserEl && chooserEl.parentNode) chooserEl.parentNode.removeChild(chooserEl);
    chooserEl = null;
  }

  /* Desktop stand-in for the WebBluetooth picker dialog: lists every
   * matching controller with live signal strength; the user picks one.
   * onPick(address) resolves the request, onCancel rejects it. */
  function showChooser(onPick, onCancel) {
    if (typeof document === 'undefined') return;
    closeChooser();
    var style = document.createElement('style');
    style.textContent =
      '.whale-bt-veil{position:fixed;inset:0;background:rgba(0,0,0,.45);' +
      'z-index:99999;display:flex;align-items:center;' +
      'justify-content:center;font-family:sans-serif}' +
      '.whale-bt-box{background:#fff;color:#222;border-radius:10px;' +
      'padding:18px 22px;min-width:320px;max-width:90vw;box-shadow:0 8px 30px rgba(0,0,0,.4)}' +
      '.whale-bt-box h2{margin:0 0 4px;font-size:17px}' +
      '.whale-bt-box p{margin:0 0 12px;font-size:12px;color:#666}' +
      '.whale-bt-list{max-height:50vh;overflow:auto;margin:0 0 12px;' +
      'padding:0;list-style:none}' +
      '.whale-bt-item{display:flex;justify-content:space-between;gap:16px;' +
      'align-items:baseline;padding:9px 12px;border:1px solid #ddd;' +
      'border-radius:8px;margin-bottom:6px;cursor:pointer}' +
      '.whale-bt-item:hover{background:#eef6ff;border-color:#3b82f6}' +
      '.whale-bt-item .nm{font-weight:600;font-size:14px}' +
      '.whale-bt-item .meta{font-size:11px;color:#777;text-align:right}' +
      '.whale-bt-cancel{width:100%;padding:8px;border:1px solid #ccc;' +
      'border-radius:8px;background:#f5f5f5;cursor:pointer;font-size:13px}';
    var veil = document.createElement('div');
    veil.className = 'whale-bt-veil';
    var box = document.createElement('div');
    box.className = 'whale-bt-box';
    box.innerHTML = '<h2>Pick a controller</h2>' +
      '<p>Bluetooth devices in reach — click yours to connect.</p>';
    var list = document.createElement('ul');
    list.className = 'whale-bt-list';
    var cancel = document.createElement('button');
    cancel.className = 'whale-bt-cancel';
    cancel.textContent = 'Cancel';
    cancel.addEventListener('click', function () {
      closeChooser();
      onCancel();
    });
    box.appendChild(list);
    box.appendChild(cancel);
    veil.appendChild(box);
    (document.body || document.documentElement).appendChild(veil);
    chooserEl = veil;
    chooserEl._list = list;
    chooserEl._render = function (items) {
      list.innerHTML = '';
      items.forEach(function (it) {
        var li = document.createElement('li');
        li.className = 'whale-bt-item';
        var rssi = it.rssi == null ? '' : (it.rssi + ' dBm');
        li.innerHTML = '<span class="nm"></span><span class="meta"></span>';
        li.querySelector('.nm').textContent = it.name || it.address;
        li.querySelector('.meta').textContent =
          (it.address || '') + (rssi ? ' · ' + rssi : '');
        li.addEventListener('click', function () {
          closeChooser();
          onPick(it);
        });
        list.appendChild(li);
      });
    };
  }

  function requestDevice(invoke, api, options) {
    var prefixes = [];
    (options && options.filters || []).forEach(function (f) {
      if (f && f.namePrefix) prefixes.push(String(f.namePrefix).toLowerCase());
    });
    var uuidFilters = [];
    (options && options.filters || []).forEach(function (f) {
      (f && f.services || []).forEach(function (u) {
        uuidFilters.push(String(u).toLowerCase());
      });
    });
    ((options && options.optionalServices) || []).forEach(function (u) {
      uuidFilters.push(String(u).toLowerCase());
    });
    var acceptAll = !prefixes.length && !uuidFilters.length &&
      !!(options && (options.acceptAllDevices || options.optionalServices));

    return new Promise(function (resolve, reject) {
      var matches = [];
      var seen = [];               // everything sighted, for the error line
      var firstMatchAt = 0;
      var settled = false;

      function matchesFilters(d) {
        var nm = (d.name || '').toLowerCase();
        if (acceptAll) return true;
        if (prefixes.some(function (p) { return nm.indexOf(p) === 0; })) {
          return true;
        }
        /* Some controllers (the MC101s included) put the local name only in
         * the scan response, which never reaches BleDevice.name — but they
         * do advertise the NUS service UUID, so match on that too. */
        var advertised = (d.services || []).map(function (u) {
          return String(u).toLowerCase();
        }).concat(Object.keys(d.serviceData || {}).map(function (u) {
          return String(u).toLowerCase();
        }));
        return uuidFilters.some(function (u) {
          return advertised.indexOf(u) !== -1;
        });
      }

      var chooserOpen = false;

      function pick(d) {
        if (settled) return;
        settled = true;
        clearTimeout(hardStop);
        clearInterval(settleTimer);
        invoke('plugin:blec|stop_scan').catch(function () {});
        resolve(wrapDevice(invoke, api, d));
      }

      function fail() {
        if (settled) return;
        settled = true;
        clearTimeout(hardStop);
        clearInterval(settleTimer);
        invoke('plugin:blec|stop_scan').catch(function () {});
        var nearby = seen.map(function (d) {
          return '"' + (d.name || '?') + '" [' +
            (d.services || []).join(', ') + ']';
        });
        reject(new Error('no Bluetooth device found matching ' +
          JSON.stringify(prefixes.concat(uuidFilters)) +
          (nearby.length ? ' — nearby: ' + nearby.join('; ') : '')));
      }

      function syncChooser() {
        if (!chooserEl) return;
        var items = matches.slice().sort(function (a, b) {
          return (b.rssi || 0) - (a.rssi || 0);
        });
        chooserEl._render(items);
      }

      function cancelPick() {
        if (settled) return;
        settled = true;
        clearTimeout(hardStop);
        clearInterval(settleTimer);
        invoke('plugin:blec|stop_scan').catch(function () {});
        reject(new Error('cancelled'));
      }

      function finishQuiet() {
        /* Scan window over: one candidate -> take it; several -> the
         * chooser stays open and waits for the user; none -> fail. */
        if (settled) return;
        if (matches.length >= 2 && !chooserOpen) {
          chooserOpen = true;
          showChooser(pick, cancelPick);
        }
        syncChooser();
        if (!matches.length) fail();
        else if (matches.length === 1 && !chooserOpen) pick(matches[0]);
      }

      var onDevices = makeChannel(api, function (devices) {
        (devices || []).forEach(function (d) {
          if (seen.length < 20 &&
              !seen.some(function (s) { return s.address === d.address; })) {
            seen.push(d);
          }
          var known = null;
          for (var i = 0; i < matches.length; i++) {
            if (matches[i].address === d.address) { known = matches[i]; break; }
          }
          if (!matchesFilters(d)) return;
          if (known) { known.rssi = d.rssi; syncChooser(); return; }
          matches.push(d);
          if (!firstMatchAt) firstMatchAt = Date.now();
          /* A second candidate appearing after the first was auto-picked
           * is too late — that race is what the settle window is for. */
          syncChooser();
        });
      });

      var settleTimer = setInterval(function () {
        if (!firstMatchAt || settled) return;
        if (Date.now() - firstMatchAt >= SCAN_SETTLE_MS) finishQuiet();
      }, 200);

      /* The plugin's scan command resolves as soon as the scan TASK is
       * spawned — devices stream in via the channel — so the no-match
       * timeout must be ours, not the invoke's settling. "In Progress"
       * means the previous scan is still tearing down: settle and retry. */
      var hardStop = setTimeout(finishQuiet, SCAN_TOTAL_MS + 500);
      var scanAttempt = function (n) {
        return invoke('plugin:blec|scan', {
          timeout: SCAN_TOTAL_MS,
          onDevices: onDevices,
          allowIbeacons: false
        }).catch(function (e) {
          if (!e || !/in progress/i.test(e.message || '') || n >= 2) throw e;
          return invoke('plugin:blec|stop_scan').catch(function () {})
            .then(function () {
              return new Promise(function (r) { setTimeout(r, 1500); });
            })
            .then(function () { return scanAttempt(n + 1); });
        });
      };
      scanAttempt(0).catch(function (e) {
        if (!settled) {
          settled = true;
          clearTimeout(hardStop);
          clearInterval(settleTimer);
          reject(e);
        }
      });
    });
  }

  function install() {
    var T = window.__TAURI__;
    if (!T || !T.core || typeof T.core.invoke !== 'function') return false;
    var invoke = T.core.invoke;
    var shim = {
      __whaleShim: true,
      requestDevice: function (options) {
        return requestDevice(invoke, T.core, options);
      },
      getAvailability: function () {
        return invoke('plugin:blec|get_adapter_state')
          .then(function (s) { return s === 'On'; });
      }
    };
    Object.defineProperty(navigator, 'bluetooth', {
      configurable: true,
      enumerable: true,
      get: function () { return shim; }
    });
    return true;
  }

  // Initialization scripts may run before the core __TAURI__ global exists;
  // retry briefly instead of giving up.
  var tries = 0;
  (function attempt() {
    if (install()) return;
    if (++tries > 50 || navigator.bluetooth) return;
    setTimeout(attempt, 100);
  })();
})();
