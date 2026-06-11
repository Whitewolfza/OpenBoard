/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef UBCOMPANIONSERVER_H
#define UBCOMPANIONSERVER_H

#include <QObject>
#include <QSet>
#include <QString>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QJsonObject>
#include <QTimer>
#include <QImage>

class UBBoardController;

/**
 * @brief WebSocket server that allows a companion app to observe and control
 *        the OpenBoard whiteboard in real time.
 *
 * Protocol (JSON messages)
 * -------------------------
 * Server → Client (events):
 *   { "type": "auth_required", "hint": "Enter PIN shown in OpenBoard" }
 *   { "type": "auth_ok" }
 *   { "type": "auth_fail" }
 *   { "type": "preview",      "page": <int>, "total": <int>, "image": "<base64 jpeg>" }
 *   { "type": "state",        "tool": "<name>", "color": "<#rrggbb>", "zoom": <double> }
 *   { "type": "page_changed", "page": <int>, "total": <int> }
 *
 * Client → Server (commands, only accepted after successful authentication):
 *   { "cmd": "next_page" }
 *   { "cmd": "prev_page" }
 *   { "cmd": "goto_page",  "page": <int> }
 *   { "cmd": "set_tool",   "tool": "pen"|"marker"|"eraser"|"pointer" }
 *   { "cmd": "set_color",  "color": "<#rrggbb>" }
 *   { "cmd": "clear" }
 *   { "cmd": "pointer",    "x": <0..1>, "y": <0..1> }
 *   { "cmd": "get_state" }
 */
class UBCompanionServer : public QObject
{
    Q_OBJECT

public:
    explicit UBCompanionServer(QObject *parent = nullptr);
    ~UBCompanionServer();

    /** Start listening on the given port (default 4444). Returns true on success. */
    bool start(quint16 port = 4444);

    /** Stop the server and disconnect all clients. */
    void stop();

    bool isRunning() const;
    quint16 port() const;

    /** The current PIN that clients must supply to authenticate. */
    QString pin() const;

    /** Generate a new random PIN (invalidates existing unauthenticated sessions). */
    void regeneratePin();

    /** Number of currently authenticated clients. */
    int connectedClientCount() const;

    void setBoardController(UBBoardController *boardController);

signals:
    /** Emitted when the running state or client count changes (for UI updates). */
    void statusChanged();

public slots:
    // Slots wired to UBBoardController signals
    void onActiveSceneChanged();
    void onPageSelectionChanged(int index);
    void onZoomChanged(qreal zoom);
    void onPenColorChanged();

private slots:
    void onNewConnection();
    void onClientMessage(const QString &message);
    void onClientDisconnected();

private:
    /** Send a JSON object to every authenticated client. */
    void broadcastToAuthenticated(const QJsonObject &obj);

    /** Send a JSON object to a single socket. */
    static void sendJson(QWebSocket *socket, const QJsonObject &obj);

    /** Build and send a preview (thumbnail) of the active scene. */
    void sendPreview(QWebSocket *socket = nullptr);

    /** Build and send the current tool/color/zoom state. */
    void sendState(QWebSocket *socket = nullptr);

    /** Handle a parsed JSON command received from a client. */
    void handleCommand(QWebSocket *sender, const QJsonObject &cmd);

    QString generatePin() const;
    int pageCount() const;
    int currentPage() const;

    QWebSocketServer *mServer = nullptr;
    UBBoardController *mBoardController = nullptr;

    /** Clients that have passed PIN authentication. */
    QSet<QWebSocket *> mAuthenticated;

    QString mPin;

    static constexpr int kThumbnailWidth  = 800;
    static constexpr int kThumbnailHeight = 600;
    static constexpr int kJpegQuality     = 75;
};

#endif // UBCOMPANIONSERVER_H
