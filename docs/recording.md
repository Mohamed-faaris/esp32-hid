# Command macro recording

Recording captures commands deliberately submitted through this ESP32 controller's API, CLI, web or MCP interfaces. It is not a keylogger for the paired computer.

## Workflow
1. Start a named recording.
2. Submit keyboard, mouse, sleep or sequence actions.
3. Stop the recording to save it in NVS.
4. List saved macros, play one through the normal queue, or delete it.

## Limits
- 24 recorded actions per macro.
- 24 queued commands.
- Macro name: 1–14 letters, digits, underscore or hyphen.
- Macro JSON size is bounded.
- Logs and telemetry do not include macro text payloads.
