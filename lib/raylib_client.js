'use strict';
/**
 * raylib_client.js — Node.js client library for raylib_server.
 *
 * Mirrors the Ruby / Python RaylibClient API.
 *
 * Example (async/await):
 *
 *   const { RaylibClient } = require('./lib/raylib_client');
 *
 *   const rls = await RaylibClient.connect();
 *
 *   await rls.displayList('main', async () => {
 *     await rls.clearBackground({ color: 'RAYWHITE' });
 *     await rls.drawCircleV({ center: [400, 300], radius: 50, color: 'RED' });
 *   });
 *
 *   const info = await rls.getServerInfo();
 *   console.log(`Server v${info.version} on port ${info.port}`);
 *
 *   await rls.close();
 */

const net  = require('net');
const { randomBytes } = require('crypto');

// Commands that require a synchronous response from the server.
const SYNC_CMDS = new Set([
  'LoadTexture','LoadRenderTexture','LoadFont','LoadFontEx',
  'LoadSound','LoadMusicStream','LoadShader',
  'UnloadTexture','UnloadRenderTexture','UnloadFont',
  'UnloadSound','UnloadMusicStream','UnloadShader',
  'UploadTexture','UploadTextureRaw','UploadFont','UploadSound','UploadMusic','UploadShader',
  'BeginUpload','UploadChunk','CommitUpload','AbortUpload','ListUploads',
  'MeasureText','MeasureTextEx',
  'GetScreenWidth','GetScreenHeight','GetRenderWidth','GetRenderHeight',
  'GetWindowPosition','GetWindowScaleDPI',
  'IsWindowReady','IsWindowFullscreen','IsWindowHidden','IsWindowMinimized',
  'IsWindowMaximized','IsWindowFocused','IsWindowResized',
  'GetFPS','GetFrameTime','GetTime',
  'GetMonitorCount','GetCurrentMonitor','GetMonitorWidth','GetMonitorHeight','GetMonitorName',
  'IsKeyPressed','IsKeyDown','IsKeyReleased','IsKeyUp','GetKeyPressed','GetCharPressed',
  'IsMouseButtonPressed','IsMouseButtonDown','IsMouseButtonReleased',
  'GetMousePosition','GetMouseDelta','GetMouseWheelMove','GetMouseWheelMoveV',
  'IsGamepadAvailable','IsGamepadButtonPressed','IsGamepadButtonDown','GetGamepadAxisMovement',
  'GetTouchPointCount','GetTouchPosition','GetGestureDetected',
  'ListHandles','GetTextureInfo','GetFontInfo',
  'GetDisplayLists','GetDisplayListCommands','GetServerInfo',
  'Subscribe','Unsubscribe',
  'LoadModel',
  'TimerCreate','TimerOnce','ListTimers',
]);

// Post-processing rules for camelCase conversion.
const CMD_FIXES = [
  [/Fps/g,    'FPS'],
  [/Dpi/g,    'DPI'],
  [/Mode2d/g, 'Mode2D'],
  [/Mode3d/g, 'Mode3D'],
  [/Npatch/g, 'NPatch'],
];

const DEFAULT_CHUNK_SIZE = 48 * 1024;

/** Convert snake_case to CamelCase raylib command name. */
function snakeToCmd(name) {
  let result = name.replace(/_([a-z0-9])/g, (_, c) => c.toUpperCase());
  result = result[0].toUpperCase() + result.slice(1);
  for (const [from, to] of CMD_FIXES) result = result.replace(from, to);
  return result;
}

class RaylibError extends Error {}

class RaylibClient {
  /**
   * Connect to a running raylib_server.
   * @param {object} [opts]
   * @param {string} [opts.host='localhost']
   * @param {number} [opts.port=7878]
   * @returns {Promise<RaylibClient>}
   */
  static connect({ host = 'localhost', port = 7878 } = {}) {
    return new Promise((resolve, reject) => {
      const client = new RaylibClient();
      client._sock = net.createConnection({ host, port }, () => resolve(client));
      client._sock.once('error', reject);
      client._sock.setEncoding('utf8');

      // Line buffer: accumulate incoming data and resolve pending syncs.
      let buf = '';
      client._sock.on('data', chunk => {
        buf += chunk;
        let nl;
        while ((nl = buf.indexOf('\n')) !== -1) {
          const line = buf.slice(0, nl);
          buf = buf.slice(nl + 1);
          client._onLine(line);
        }
      });
      client._sock.on('close', () => {
        for (const [, rej] of client._pending.values()) rej(new RaylibError('server disconnected'));
        client._pending.clear();
      });
    });
  }

  constructor() {
    this._sock     = null;
    this._pending  = new Map();  // id → [resolve, reject]
    this._events   = [];         // buffered server-push events (no id field)
    this._batch    = null;       // string[] when batching, null otherwise
  }

  _onLine(line) {
    let msg;
    try { msg = JSON.parse(line); } catch { return; }
    if (msg.id && this._pending.has(msg.id)) {
      const [res, rej] = this._pending.get(msg.id);
      this._pending.delete(msg.id);
      if (msg.ok) res(msg.result ?? null);
      else rej(new RaylibError(`command failed: ${msg.error}`));
    } else if (msg.event) {
      this._events.push(msg);
    }
  }

  // ------------------------------------------------------------------
  // Low-level command API
  // ------------------------------------------------------------------

  /**
   * Send a fire-and-forget command.
   * @param {string} name  Wire protocol command name (e.g. 'DrawCircle')
   * @param {object} [args]
   */
  cmd(name, args = null) {
    const msg = args ? { cmd: name, args } : { cmd: name };
    const line = JSON.stringify(msg) + '\n';
    if (this._batch !== null) {
      this._batch.push(line);
    } else {
      this._sock.write(line);
    }
  }

  /**
   * Send a synchronous command and resolve with the result object.
   * @param {string} name
   * @param {object} [args]
   * @returns {Promise<object|null>}
   */
  sync(name, args = null) {
    return new Promise((resolve, reject) => {
      const id  = 'j' + randomBytes(3).toString('hex');
      const msg = args ? { id, cmd: name, args } : { id, cmd: name };
      this._pending.set(id, [resolve, reject]);
      this._sock.write(JSON.stringify(msg) + '\n');
    });
  }

  // ------------------------------------------------------------------
  // Batch mode
  // ------------------------------------------------------------------

  /**
   * Buffer all fire-and-forget commands inside the callback and flush in
   * a single write.  Returns the result of the callback.
   * @param {Function} fn  async or sync callback
   */
  async batch(fn) {
    this._batch = [];
    try {
      return await fn();
    } finally {
      const data = this._batch;
      this._batch = null;
      if (data.length) this._sock.write(data.join(''));
    }
  }

  // ------------------------------------------------------------------
  // Display list DSL
  // ------------------------------------------------------------------

  /**
   * Record draw commands into a named display list.
   * @param {string}   name
   * @param {Function} fn  async or sync callback containing draw commands
   */
  async displayList(name, fn) {
    this.cmd('DisplayListBegin', { name });
    try {
      await fn();
    } finally {
      this.cmd('DisplayListEnd');
    }
  }

  // ------------------------------------------------------------------
  // Event streaming
  // ------------------------------------------------------------------

  async subscribe(...events) {
    return this.sync('Subscribe', { events: events.flat() });
  }

  async unsubscribe(...events) {
    return this.sync('Unsubscribe', { events: events.flat() });
  }

  /** Return and clear all buffered server-push events. */
  drainEvents() {
    const evs = this._events.slice();
    this._events.length = 0;
    return evs;
  }

  // ------------------------------------------------------------------
  // Upload helpers
  // ------------------------------------------------------------------

  /**
   * Upload a local file using the chunked upload protocol.
   * @returns {Promise<number>} resource handle
   */
  async chunkedUpload(path, { fileType, resourceType, chunkSize = DEFAULT_CHUNK_SIZE } = {}) {
    const fs   = require('fs');
    const data = fs.readFileSync(path);
    return this.uploadData(data, {
      fileType,
      resourceType,
      name: require('path').basename(path),
      chunkSize,
    });
  }

  /**
   * Upload a Buffer using the chunked upload protocol.
   * @returns {Promise<number>} resource handle
   */
  async uploadData(data, { fileType, resourceType, name = 'upload', chunkSize = DEFAULT_CHUNK_SIZE } = {}) {
    const total  = data.length;
    const begin  = await this.sync('BeginUpload', { name, fileType, totalBytes: total });
    const uploadId = begin.uploadId;

    let seq = 0, pos = 0;
    while (pos < total) {
      const chunk = data.slice(pos, pos + chunkSize);
      await this.sync('UploadChunk', { uploadId, seq, data: chunk.toString('base64') });
      pos += chunk.length;
      seq++;
    }

    const commit = await this.sync('CommitUpload', { uploadId, type: resourceType });
    return commit.handle;
  }

  // ------------------------------------------------------------------
  // Connection management
  // ------------------------------------------------------------------

  close() {
    return new Promise(resolve => {
      this._sock.end(() => resolve());
    });
  }

  get connected() {
    return this._sock && !this._sock.destroyed;
  }

  // ------------------------------------------------------------------
  // Proxy: camelCase method dispatch
  //
  // Returns an async function for any unknown property name.
  // The property name is converted from camelCase (already JS-natural)
  // to the CamelCase wire protocol name.
  //
  // Usage:
  //   await rls.drawCircle({ centerX:400, centerY:300, radius:50, color:'RED' })
  //   const w = await rls.getScreenWidth()   // → { width: 800 }
  // ------------------------------------------------------------------
}

// Attach a Proxy so that any undefined property on RaylibClient instances
// becomes an auto-dispatched command function.
const _RaylibClientProxy = new Proxy(RaylibClient.prototype, {
  get(target, prop, receiver) {
    if (prop in target) return Reflect.get(target, prop, receiver);
    if (typeof prop !== 'string') return undefined;

    // JS callers use camelCase; snakeToCmd expects snake_case-style
    // but also handles camelCase → CamelCase since single-segment words
    // capitalize correctly.  We just need to uppercase the first letter.
    const cmdName = prop[0].toUpperCase() + prop.slice(1);
    let finalName = cmdName;
    for (const [from, to] of CMD_FIXES) finalName = finalName.replace(from, to);

    return function(args = null) {
      if (SYNC_CMDS.has(finalName)) return this.sync(finalName, args || undefined);
      this.cmd(finalName, args || undefined);
      return Promise.resolve(null);
    };
  },
});
Object.setPrototypeOf(RaylibClient.prototype, _RaylibClientProxy);

module.exports = { RaylibClient, RaylibError, snakeToCmd };
