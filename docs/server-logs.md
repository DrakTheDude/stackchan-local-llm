# Reading the server's logs

The server logs in Chinese. That is not a bug and this project does not translate
it — the [patch kit](../server/patches/) fixes the Chinese **the model sees and
the user hears**, because those reach a person who did not ask for them. The log
is read by you, once, when something is wrong, and rewriting every log line would
mean re-doing it on every upstream bump for no gain.

So: a glossary instead. These are the real strings, from a running server.

```bash
docker compose logs -f xiaozhi          # follow
docker compose logs xiaozhi | grep ...  # search
```

---

## If you learn five lines, learn these

**`当前支持的函数列表: [...]`** — *the list of functions currently available*.
Printed on every connection, and it is the **complete set of tools the model can
call**. If a tool you configured is not in this list, the model cannot use it and
the problem is your [MCP config](mcp.md), not the model. This is the single most
useful line in the file.

**`大模型收到用户消息: ...`** — *the model received the user message*. What was
actually sent to the model, after transcription.

> ⚠️ On the first turn this prints your **wake word**. That is not a display
> quirk — the phrase really is handed to the model as if you had said it. Choose
> a wake word for what it says, not only for how well it triggers.

**`识别文本: ...`** — *recognised text*: what the speech recogniser heard. When he
answers the wrong question, this is the line that tells you whether he misheard
you or misunderstood you. They need completely different fixes.

**`发送第一段语音`** — *sending the first segment of speech*. The moment he starts
talking. Time from `识别文本` to here is the latency you actually feel; see
[model-floor.md](model-floor.md).

**`为记忆总结创建了专用LLM: LocalLLM, 类型: openai`** — *created a dedicated LLM
for memory summarisation*. 🔴 **Check this one after any config change.** If it
names anything other than your local model, the summariser has inherited an
upstream cloud default and every conversation is being posted to an API. It is
the trap the config file warns about, and this line is how you confirm it.

---

## Connection and handshake

| line | meaning |
|---|---|
| `请求设备ID` / `请求ClientID` | *device ID / client ID requested* — the robot identifying itself |
| `查找型号 X 的固件，找到 N 个候选` | *looking for firmware for model X, found N candidates*. `0` is normal — this server is not an OTA host |
| `设备 X 固件已是最新` | *device firmware is already up to date* |
| `未配置MQTT网关，为设备 X 下发WebSocket配置` | *no MQTT gateway configured, issuing WebSocket config* — normal, this is the transport this project uses |
| `收到hello消息：{...}` | *hello received* — the robot's handshake. Includes which body he is wearing |
| `收到listen消息：{...}` | *listen message received* — he started or stopped listening |
| `收到abort消息` | *abort received* — you interrupted him |
| `客户端断开连接` | *client disconnected* |
| `连接资源已释放` / `超时检查任务已退出` | *connection resources released* / *timeout-check task exited* — normal teardown |
| `上面的地址是websocket协议地址，请勿用浏览器访问` | *the address above is a websocket address, do not open it in a browser*. Worth reading before you try |

## Speech in and out

| line | meaning |
|---|---|
| `识别文本` | *recognised text* — the transcript |
| `语音生成成功` | *speech generated successfully* — TTS worked |
| `发送第一段语音` | *sending the first segment of speech* |
| `发送音频消息: SentenceType.LAST` | *sending audio message* — `LAST` marks the end of an utterance |
| `配置输出音频采样率为` | *output sample rate configured as* |
| `识别到明确的退出命令` | *explicit exit command recognised* — he heard a goodbye and is closing the session |

## The model and its tools

| line | meaning |
|---|---|
| `当前支持的函数列表` | *currently supported function list* — see above |
| `客户端设备支持的工具数量` | *number of tools the client device supports* — the robot's own, from [robot-tools.md](robot-tools.md) |
| `初始化服务端MCP客户端: X` | *initialising server-side MCP client X* — one of yours, from `.mcp_server_settings.json` |
| `服务端MCP客户端已连接，可用工具: [...]` | *connected, available tools* — **what that server actually offered**. An empty list here with a successful connection is the `transport` trap in [mcp.md](mcp.md) |
| `服务端MCP客户端已关闭: X` | *closed* |
| `执行工具` / `执行服务端MCP工具` | *executing tool* / *executing server-side MCP tool* |
| `客户端mcp工具调用` / `发送客户端mcp工具调用请求` | *client MCP tool call* — a tool on the robot itself |
| `发送MCP消息失败` | *failed to send MCP message* |
| `使用快速提示词` / `构建增强提示词成功，长度` | *using the fast prompt* / *enhanced prompt built, length N* — which persona path was taken |

## Startup and housekeeping

| line | meaning |
|---|---|
| `模块初始化完成` / `快速初始化组件` | *module initialisation complete* / *fast component init* |
| `工具处理器清理完成` | *tool handler cleanup complete* |
| `启动全局GC管理器，间隔300秒` | *global GC manager started, 300s interval* |
| `为记忆总结创建了专用LLM` | see above — **the one to check** |

## Lines that look alarming and are not

| line | why it is fine |
|---|---|
| `声纹识别URL未配置，声纹识别将被禁用` | *voiceprint URL not configured, voiceprint disabled*. Speaker identification is an optional feature this project does not use |
| `声纹识别功能启用但配置不完整` | *voiceprint enabled but configuration incomplete*. Same feature, same answer |
| `查找型号 ... 找到 0 个候选` | *0 candidates*. Correct: OTA is not served from here |
| `生成并保存聊天标题失败` | *failed to generate and save the chat title*. Cosmetic — it names conversations in a web UI this project does not run |
| `重试0次` | *retried 0 times*. It succeeded first time; the counter is printed either way |

---

## A note on what is *not* translated here

Anything the model reads or the speaker says **is** in English, and is asserted to
stay that way — the patch kit's replacements fail the build rather than silently
restoring Chinese. That includes a few places no prompt can reach: the goodbye
spoken on exit, the memory summariser, and the few-shot examples, which were the
ones that took longest to find. A demonstration beats an instruction, so a
Chinese *example* outvotes an English instruction every time.

The logs are the deliberate exception. If a line you need is missing from this
page, it is worth adding.
