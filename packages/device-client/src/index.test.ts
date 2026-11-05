import assert from "node:assert/strict";
import test, { afterEach } from "node:test";
import { DeviceApiError, DeviceClient } from "./index.js";

const originalFetch = globalThis.fetch;

afterEach(() => {
  globalThis.fetch = originalFetch;
});

test("typeText sends an authenticated keyboard command", async () => {
  let capturedUrl = "";
  let capturedInit: RequestInit | undefined;
  globalThis.fetch = (async (input: RequestInfo | URL, init?: RequestInit) => {
    capturedUrl = String(input);
    capturedInit = init;
    return new Response(JSON.stringify({ queued: true }), {
      status: 202,
      headers: { "content-type": "application/json" }
    });
  }) as typeof fetch;

  const client = new DeviceClient({
    baseUrl: "http://192.168.4.1/",
    token: "test-device-secret"
  });
  const result = await client.typeText("hello", { delay_ms: 20 });

  assert.deepEqual(result, { queued: true });
  assert.equal(capturedUrl, "http://192.168.4.1/api/keyboard/type");
  assert.equal(new Headers(capturedInit?.headers).get("authorization"), "Bearer test-device-secret");
  assert.deepEqual(JSON.parse(String(capturedInit?.body)), {
    type: "type",
    value: "hello",
    delay_ms: 20
  });
});

test("status is intentionally readable without the control token", async () => {
  let capturedHeaders: Headers | undefined;
  globalThis.fetch = (async (_input: RequestInfo | URL, init?: RequestInit) => {
    capturedHeaders = new Headers(init?.headers);
    return new Response(JSON.stringify({ ble_keyboard_ready: true }), { status: 200 });
  }) as typeof fetch;

  const client = new DeviceClient({
    baseUrl: "http://192.168.4.1",
    token: "test-device-secret"
  });
  const result = await client.status();

  assert.equal(result.ble_keyboard_ready, true);
  assert.equal(capturedHeaders?.has("authorization"), false);
});

test("mouse commands use the same authenticated device API", async () => {
  const calls: Array<{ url: string; body: unknown }> = [];
  globalThis.fetch = (async (input: RequestInfo | URL, init?: RequestInit) => {
    calls.push({
      url: String(input),
      body: init?.body ? JSON.parse(String(init.body)) : undefined
    });
    return new Response(JSON.stringify({ queued: true }), { status: 202 });
  }) as typeof fetch;

  const client = new DeviceClient({
    baseUrl: "http://192.168.4.1",
    token: "test-device-secret"
  });
  await client.mouseMove(10, -5);
  await client.mouseClick("right");
  await client.mouseWheel(-4);

  assert.deepEqual(calls, [
    { url: "http://192.168.4.1/api/mouse/move", body: { dx: 10, dy: -5, wheel: 0 } },
    { url: "http://192.168.4.1/api/mouse/click", body: { button: "right" } },
    { url: "http://192.168.4.1/api/mouse/wheel", body: { delta: -4 } }
  ]);
});

test("non-success responses retain status and structured error payload", async () => {
  globalThis.fetch = (async () => new Response(JSON.stringify({ error: "queue_full" }), {
    status: 429,
    headers: { "content-type": "application/json" }
  })) as typeof fetch;

  const client = new DeviceClient({
    baseUrl: "http://192.168.4.1",
    token: "test-device-secret"
  });

  await assert.rejects(
    () => client.queue("pause"),
    (error: unknown) => {
      assert.ok(error instanceof DeviceApiError);
      assert.equal(error.status, 429);
      assert.deepEqual(error.payload, { error: "queue_full" });
      assert.equal(error.message, "queue_full");
      return true;
    }
  );
});
