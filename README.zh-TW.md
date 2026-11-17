# Steam Frame Dictation（語音聽寫）

[English](README.md) | 繁體中文

https://github.com/user-attachments/assets/c349ab1f-26a2-4fca-aa1a-0caed856ae62

在 Steam Frame 上離線語音聽寫。

採用 MIT 授權。請見 [LICENSE](LICENSE) 與[第三方授權聲明](third_party/README.md)。

## 安裝

在主控台執行：

```sh
curl -fsSL https://raw.githubusercontent.com/khvn26/steam-frame-dictation/main/install.sh | bash
```

安裝程式會把執行檔、Whisper 函式庫與模型複製到：

```text
~/.local/share/steam-frame-dictation
```

並安裝、啟用使用者服務：

```text
~/.config/systemd/user/frame-dictation.service
```

查看版本：

```bash
~/.local/share/steam-frame-dictation/bin/steam-frame-dictation --version
```

解除安裝：

```bash
~/.local/share/steam-frame-dictation/bin/steam-frame-dictation --uninstall
```

## 使用方式

- 平常處於閒置狀態。
- 游標停在任何文字輸入欄位時，按 aux（側鍵）開始聽寫。
- 聽到提示音後就可以開始說話。
- 可以短暫停頓，聽寫會持續把文字送出。
- 停頓太久會結束聽寫，結束時會再響一次提示音。
- 聽寫中再按一次 aux 會立刻結束，送出所有已錄下的文字並按下 Return 鍵。

聽寫進行時，服務會調低系統音量，結束後恢復原本的音量。

服務執行期間會獨占 aux 輸入裝置。也就是說，目前 aux 原本的功能（例如指標／透視捷徑）在服務啟用時無法使用。

## 中文與其他語言

安裝後的服務只聽寫英文。要聽寫其他語言，需要多語的 Whisper 模型、`--language`，以及讓虛擬鍵盤打不出的文字改用貼上的 `--paste-command`。以中文為例：

```bash
D=~/.local/share/steam-frame-dictation
curl -L --fail -o $D/models/whisper/ggml-base.bin https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin
systemctl --user edit frame-dictation.service
```

在編輯器裡覆寫服務指令（空的 `ExecStart=` 用來清掉預設指令）：

```ini
[Service]
ExecStart=
ExecStart=%h/.local/share/steam-frame-dictation/bin/steam-frame-dictation --whisper-model %h/.local/share/steam-frame-dictation/models/whisper/ggml-base.bin --language zh --whisper-threads 4 --whisper-audio-ctx 768 --actionable-pause-seconds 0.6 --paste-command %h/.local/share/steam-frame-dictation/bin/sfd-x11-copy --quiet
```

接著執行 `systemctl --user restart frame-dictation.service`。這份覆寫設定存在 `~/.config/systemd/user/frame-dictation.service.d/`，重新安裝後仍會保留，`--uninstall` 也不會移除它。

在一台 Steam Frame 上測試中文的觀察：

- `ggml-base.bin`（148 MB）4 個執行緒時每個片段約 1.6 秒，2 個執行緒約 3.2 秒。多語的 `tiny` 模型比較快，但準確度明顯較差。
- 片段太短時辨識較差；把 `--actionable-pause-seconds` 調長可以讓 Whisper 每段有更多上下文，代價是文字比較晚出現。
- 在 Steam 介面可以貼上。遊戲內的文字欄位跑在另一個 X 顯示上，可能讀不到剪貼簿。
- 中文可以用 `--whisper-prompt "以下是繁體中文的句子。"` 引導輸出偏向繁體字。

## 設計目標

- 閒置時 CPU 使用量為 0。
- 記憶體用量低。
- 盡量不需要暖機：閒置時模型仍保持載入。
- 讓使用者盡早看到文字。前面的片段在背景轉寫、輸入的同時，錄音持續進行。
- 錄音與轉寫的節奏讓人可以自然地說話。
- 在轉寫速度與準確度之間維持舒適的平衡。

## 效能

以下是在一台 Steam Frame 上手動測試的觀察結果，使用純 CPU 的 Whisper `tiny.en`、2 個執行緒、audio context 768、token 上限 32：

| 指標 | 觀察值 |
| --- | --- |
| 啟動後記憶體 | systemd 回報約 104 MiB |
| 聽寫後記憶體 | 約 130–140 MiB，依計算方式而異 |
| 每個送出片段的轉寫時間 | 最近一次實測約 1.1–1.3 秒 |
| 音訊片段範例 | 1.96 秒音訊 → 處理 1.09 秒；11.05 秒 → 1.28 秒 |
| 幾乎閒置的工作階段 CPU 用量 | 17 分鐘內約 10.4 CPU 秒，包含數次聽寫 |

這些是開發過程中的量測，不是針對每個版本的嚴謹效能測試。CPU 平均值**不是**推論進行中的 CPU 使用率，也不是續航力的量測。閒置時的工作量很小但並非真的為零：輸入迴圈最多每 500 毫秒醒來一次，而且麥克風關閉時模型仍留在記憶體中。

轉寫時間不包含送出片段所需的停頓（預設 0.35 秒）、佇列等待與鍵盤輸出，並不保證從說話到出字的整體時間。較長的片段、其他工作負載、溫度狀況，以及模型或設定的變更，都會影響延遲與準確度。

## 建置

建置並安裝原生執行檔：

```bash
cd ~/voice-dictation/native
make install
```

會安裝到：

```text
~/voice-dictation/bin/steam-frame-dictation
```

執行檔直接連結下列位置內附的 whisper.cpp 函式庫：

```text
~/voice-dictation/lib/whisper
~/voice-dictation/include
```

## 服務

使用者服務位於：

```text
~/.config/systemd/user/frame-dictation.service
```

常用指令：

```bash
systemctl --user status frame-dictation.service
journalctl --user -u frame-dictation.service -f
systemctl --user restart frame-dictation.service
systemctl --user stop frame-dictation.service
systemctl --user start frame-dictation.service
```

重新建置之後：

```bash
systemctl --user restart frame-dictation.service
```

目前服務執行的指令：

```text
~/.local/share/steam-frame-dictation/bin/steam-frame-dictation --whisper-model ~/.local/share/steam-frame-dictation/models/whisper/ggml-tiny.en.bin --whisper-threads 2 --whisper-audio-ctx 768 --whisper-max-tokens 32 --quiet
```

## 調校後的預設值

大部分是靠反覆嘗試與手動測試得來：

- aux／側鍵觸發：evdev 鍵碼 `353`
- Whisper 模型：`models/whisper/ggml-tiny.en.bin`
- Whisper 執行緒：服務中為 `2`
- Whisper audio context：服務中為 `768`
- Whisper token 上限：服務中為 `32`
- 送出片段的停頓：`0.35s`
- 結束聽寫的停頓：`3.0s`
- 音訊格式：`16000 Hz`、單聲道、有號 16 位元 PCM
- 區塊大小：`1600 bytes`
- 預錄（preroll）：`0.3s`
- 錄音暖機：`0.2s`
- 靜音門檻：RMS `100`
- 無語音逾時：`8.0s`
- 片段過濾：最短 `0.60s`、RMS `150`
- 播放音量壓低（ducking）：啟用，目標音量 `0.15`
- 安靜模式：服務中啟用，不輸出執行記錄

## 命令列參數

```bash
~/.local/share/steam-frame-dictation/bin/steam-frame-dictation --help
```

系統語系為繁體中文（`zh_TW`、`zh_HK`）時，`--help` 會顯示繁體中文說明。

### 一般

| 參數 | 行為／預設值 |
| --- | --- |
| `--help`、`-h` | 顯示說明後結束。 |
| `--version`、`-V` | 顯示建置版本後結束。 |
| `--uninstall` | 停止並停用使用者服務，移除服務單元檔與 `~/.local/share/steam-frame-dictation`。不會動到原始碼目錄；不處理自訂的安裝路徑。 |
| `--quiet` | 不輸出執行記錄（包含錯誤）。直接執行時預設關閉；安裝的服務預設開啟。未加此參數時，聽寫出的文字會寫進記錄。 |
| `--dry-run` | 不建立虛擬鍵盤、不輸入文字。仍會獨占觸發裝置、錄音、轉寫、播放提示音並壓低音量（除非另外停用）。 |

### 輸出

| 參數 | 行為／預設值 |
| --- | --- |
| `--paste-command CMD` | 從標準輸入讀取文字並放進剪貼簿的 shell 指令；預設無。虛擬鍵盤只能打出美式鍵盤的 ASCII 字元，未設定此參數時其他字元會被略過。設定後，含有這類字元的片段會交給 `CMD` 並以 Ctrl+V 貼上；純 ASCII 的片段仍逐鍵輸入。Frame 本身沒有剪貼簿工具，因此安裝內容附有 `bin/sfd-x11-copy`；其他環境可用 `wl-copy` 或 `xclip -selection clipboard`。貼上會覆蓋剪貼簿原本的內容。 |

### 輸入與錄音

| 參數 | 行為／預設值 |
| --- | --- |
| `--device PATH` | 觸發用的 evdev 裝置；`/dev/input/by-path/platform-gpio-keys-event`。執行期間會被獨占。 |
| `--key N` | 觸發鍵碼；`353`（aux）。 |
| `--uinput PATH` | 虛擬鍵盤裝置；`/dev/uinput`。 |
| `--source NAME` | 傳給 `pw-record` 的 PipeWire 目標；預設為系統麥克風。 |
| `--rate N` | 錄音取樣率；`16000` Hz。**請維持 16000**：Whisper 需要 16 kHz 音訊，程式不會重新取樣。 |
| `--chunk-bytes N` | PCM 讀取大小；`1600` bytes。音訊為單聲道有號 16 位元 PCM。 |
| `--preroll-seconds N` | 偵測到語音之前保留的音訊長度；`0.3` 秒。 |
| `--recorder-warmup-seconds N` | 開始提示音之前的錄音暖機時間；`0.2` 秒。 |

### 分段與過濾

| 參數 | 行為／預設值 |
| --- | --- |
| `--actionable-pause-seconds N` | 靜音多久就送出一個片段；`0.35` 秒。 |
| `--pause-seconds N` | 說話後靜音多久就結束聽寫；`3.0` 秒。 |
| `--no-speech-timeout N` | 一直沒偵測到語音時結束聽寫；`8.0` 秒。 |
| `--silence-threshold N` | 判定為語音的 PCM RMS 門檻；`100`。 |
| `--min-segment-seconds N` | 短於此長度的片段不轉寫、直接丟棄；`0.6` 秒。 |
| `--min-transcribe-rms N` | 整段 PCM RMS 低於此值的片段直接丟棄；`150`。 |

RMS 的單位是有號 16 位元 PCM 的振幅，不是 dB。片段長度包含前後保留的音訊；這些過濾條件不是語音辨識的信心分數。

### Whisper

| 參數 | 行為／預設值 |
| --- | --- |
| `--whisper-model PATH` | 模型檔。編譯時的預設值：`/home/steamos/voice-dictation/models/whisper/ggml-tiny.en.bin`。服務會明確傳入安裝後的模型路徑。 |
| `--language CODE` | 說話的語言，使用 Whisper 語言代碼（`en`、`de`、`zh`⋯⋯），或用 `auto` 逐片段自動偵測；`en`。`en` 以外的語言需要多語模型（例如以 `ggml-base.bin` 取代 `ggml-tiny.en.bin`）。 |
| `--whisper-prompt TEXT` | 傳給 Whisper 的起始提示，用來引導用詞與書寫風格；預設無。例如給一句繁體中文，可讓 `zh` 的輸出偏向繁體字。 |
| `--whisper-threads N` | 推論執行緒數；`4`（服務中為 `2`）。 |
| `--whisper-audio-ctx N` | Audio context 大小；`0` 表示完整 context（服務中為 `768`）。 |
| `--whisper-max-tokens N` | 每個片段的 token 上限；`0` 表示不限（服務中為 `32`）。上限太低可能截斷輸出。 |

### 音效回饋

| 參數 | 行為／預設值 |
| --- | --- |
| `--duck-volume N` | 聆聽時的播放音量上限（絕對值）；`0.15`，`1.0` 代表 100%。不是目前音量的倍率。 |
| `--no-duck` | 停用音量壓低；預設啟用。 |
| `--start-sound PATH` | 開始提示音；`/home/steamos/.local/share/Steam/steamui/sounds/recording_start.wav`。 |
| `--end-sound PATH` | 結束提示音；`/home/steamos/.local/share/Steam/steamui/sounds/recording_stop.wav`。 |
| `--no-sounds` | 停用提示音；預設啟用。短停頓送出片段時一律不出聲。 |
