import { Command } from "commander";
import dotenv from "dotenv";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";
import { DeviceClient, type MouseButton, type QueueAction } from "@esp32-hid/device-client";

dotenv.config({ path: fileURLToPath(new URL("../../../.env", import.meta.url)) });

function device() {
  return new DeviceClient({
    baseUrl: process.env.ESP32_API_URL ?? "http://192.168.4.1",
    token: process.env.ESP32_API_SECRET ?? "",
    timeoutMs: Number(process.env.ESP32_API_TIMEOUT_MS ?? 8000)
  });
}
async function run(task: () => Promise<unknown>) {
  try {
    process.stdout.write(JSON.stringify(await task(), null, 2) + "\n");
  } catch (error) {
    process.stderr.write("error: " + (error instanceof Error ? error.message : String(error)) + "\n");
    process.exitCode = 1;
  }
}
const program = new Command();
program.name("esp32-hid").description("ESP32 BLE keyboard and mouse control").version("0.1.0");
program.command("status").action(() => run(() => device().status()));
program.command("type").argument("<text>").option("--instant").option("--delay <ms>", "inter-key delay", "35")
  .action((text, opts) => run(() => device().typeText(text, { instant: Boolean(opts.instant), delay_ms: Number(opts.delay) })));
program.command("key").argument("<key>").action(key => run(() => device().pressKey(key)));
program.command("hotkey").argument("<combination>").action(value => run(() => device().hotkey(value)));
program.command("mode").argument("<name>").action(name => run(() => device().setMode(name.toUpperCase())));
program.command("logs").action(() => run(() => device().logs()));
program.command("log-level").argument("<level>").choices(["NONE","ERROR","WARN","INFO","DEBUG","TRACE"])
  .action(level => run(() => device().setLogLevel(level)));
program.command("bluetooth-advertise").description("Restart advertising; host OS controls reconnect")
  .action(() => run(() => device().restartAdvertising()));

const mouse = program.command("mouse");
mouse.command("move").argument("<dx>").argument("<dy>").option("--wheel <delta>", "wheel delta", "0")
  .action((dx, dy, opts) => run(() => device().mouseMove(Number(dx), Number(dy), Number(opts.wheel))));
mouse.command("click").argument("[button]").choices(["left","right","middle","back","forward"])
  .action((button = "left") => run(() => device().mouseClick(button as MouseButton)));
mouse.command("wheel").argument("<delta>").action(delta => run(() => device().mouseWheel(Number(delta))));

program.command("queue").argument("<action>").choices(["pause","resume","stop"])
  .action(action => run(() => device().queue(action as QueueAction)));

const record = program.command("record");
record.command("start").argument("<name>").action(name => run(() => device().startRecording(name)));
record.command("stop").action(() => run(() => device().stopRecording()));
record.command("status").action(() => run(() => device().recordingStatus()));

const macro = program.command("macro");
macro.command("list").action(() => run(() => device().listMacros()));
macro.command("play").argument("<name>").action(name => run(() => device().playMacro(name)));
macro.command("delete").argument("<name>").action(name =>
  run(() => device().request("macros/delete", { method: "POST", body: { name } })));

program.parseAsync(process.argv);
