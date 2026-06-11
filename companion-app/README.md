# OpenBoard Companion App

A lightweight browser-based companion that gives you **full remote control and live preview** of an OpenBoard whiteboard session from any phone, tablet, or laptop on the same network.

---

## Features

| Feature | Description |
|---|---|
| Live preview | Real-time JPEG thumbnail of the active board page |
| Page navigation | Previous / next / jump-to-page buttons |
| Page strip | Scrollable thumbnail strip of all pages |
| Tool switching | Pen, Marker, Eraser, Pointer |
| Color picker | Change the pen/marker colour remotely |
| Pointer overlay | Touch/click on the preview to move the pointer on the board |
| Clear board | Erase the current page |
| PIN authentication | 6-digit PIN prevents unauthorised access |

---

## Getting started

### 1. Enable the companion server in OpenBoard

The companion WebSocket server starts automatically with OpenBoard and listens on **port 4444**. On startup, the current PIN is logged to the console:

```
UBCompanionServer: listening on port 4444 | PIN: 123456
```

> A future UI will display the PIN and a QR code directly in the toolbar.

### 2. Open the companion app

Open `companion-app/index.html` in any modern browser on a device connected to the **same local network** as the OpenBoard computer.

> Alternatively, host it on a simple HTTP server:
> ```bash
> cd companion-app
> python3 -m http.server 8080
> ```
> Then navigate to `http://<openboard-ip>:8080` on your device.

### 3. Connect

1. Enter the **IP address** of the OpenBoard computer (e.g. `192.168.1.42`).
2. Confirm the **port** (`4444` by default).
3. Enter the **6-digit PIN** shown in the OpenBoard console.
4. Tap **Connect**.

---

## WebSocket protocol

### Server → Client

| Message | Fields | Description |
|---|---|---|
| `auth_required` | `hint` | Sent immediately on connect |
| `auth_ok` | — | Authentication succeeded |
| `auth_fail` | — | Wrong PIN |
| `preview` | `page`, `total`, `image` (base64 JPEG) | Board thumbnail |
| `state` | `tool`, `color`, `zoom` | Current tool/colour/zoom |
| `page_changed` | `page`, `total` | Active page changed |

### Client → Server

| Command | Extra fields | Description |
|---|---|---|
| `next_page` | — | Go to next page |
| `prev_page` | — | Go to previous page |
| `goto_page` | `page` (1-based int) | Jump to page |
| `set_tool` | `tool` (`pen`\|`marker`\|`eraser`\|`pointer`) | Change tool |
| `set_color` | `color` (`#rrggbb`) | Change pen/marker colour |
| `clear` | — | Clear current page |
| `pointer` | `x`, `y` (normalised 0–1) | Move pointer on board |
| `get_state` | — | Request full state refresh |

Authentication: send `{ "pin": "123456" }` after receiving `auth_required`.

---

## Security

- The server binds to **all interfaces** (`0.0.0.0`) so it is reachable on your LAN.
- Only clients that supply the correct PIN receive board events or can issue commands.
- For a public-facing deployment, place OpenBoard behind a firewall or use a reverse proxy with TLS.
- To change the PIN at runtime: call `UBApplication::companionServer->regeneratePin()` (a future settings dialog will expose this in the UI).
