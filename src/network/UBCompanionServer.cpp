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

#include "UBCompanionServer.h"

#include <QBuffer>
#include <QColor>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPainter>
#include <QRandomGenerator>
#include <QRectF>

#include "board/UBBoardController.h"
#include "board/UBDrawingController.h"
#include "core/UB.h"
#include "domain/UBGraphicsScene.h"
#include "domain/UBItem.h"
#include "document/UBDocumentProxy.h"

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

UBCompanionServer::UBCompanionServer(QObject *parent)
    : QObject(parent)
{
    mPin = generatePin();
}

UBCompanionServer::~UBCompanionServer()
{
    stop();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool UBCompanionServer::start(quint16 port)
{
    if (mServer && mServer->isListening())
        return true;

    delete mServer;
    mServer = new QWebSocketServer(QStringLiteral("OpenBoard Companion"),
                                   QWebSocketServer::NonSecureMode, this);

    if (!mServer->listen(QHostAddress::Any, port)) {
        delete mServer;
        mServer = nullptr;
        return false;
    }

    connect(mServer, &QWebSocketServer::newConnection,
            this, &UBCompanionServer::onNewConnection);

    emit statusChanged();
    return true;
}

void UBCompanionServer::stop()
{
    if (!mServer)
        return;

    mServer->close();

    // Copy the set before iterating; closing a socket triggers onClientDisconnected()
    // which modifies mAuthenticated, so we must not iterate the live set.
    const QSet<QWebSocket *> toClose = mAuthenticated;
    mAuthenticated.clear();
    for (QWebSocket *client : toClose)
        client->close();
    emit statusChanged();
}

bool UBCompanionServer::isRunning() const
{
    return mServer && mServer->isListening();
}

quint16 UBCompanionServer::port() const
{
    return mServer ? mServer->serverPort() : 0;
}

QString UBCompanionServer::pin() const
{
    return mPin;
}

void UBCompanionServer::regeneratePin()
{
    mPin = generatePin();
    // Existing unauthenticated connections keep the old server object; nothing
    // special needed – they just won't be able to supply the new PIN until they
    // reconnect.
    emit statusChanged();
}

int UBCompanionServer::connectedClientCount() const
{
    return mAuthenticated.size();
}

void UBCompanionServer::setBoardController(UBBoardController *boardController)
{
    if (mBoardController == boardController)
        return;

    if (mBoardController) {
        disconnect(mBoardController, nullptr, this, nullptr);
    }

    mBoardController = boardController;

    if (mBoardController) {
        connect(mBoardController, &UBBoardController::activeSceneChanged,
                this, &UBCompanionServer::onActiveSceneChanged);
        connect(mBoardController, &UBBoardController::pageSelectionChanged,
                this, &UBCompanionServer::onPageSelectionChanged);
        connect(mBoardController, &UBBoardController::zoomChanged,
                this, &UBCompanionServer::onZoomChanged);
        connect(mBoardController, &UBBoardController::penColorChanged,
                this, &UBCompanionServer::onPenColorChanged);
    }
}

// ---------------------------------------------------------------------------
// Slots – board events
// ---------------------------------------------------------------------------

void UBCompanionServer::onActiveSceneChanged()
{
    sendPreview();
    sendState();

    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("page_changed");
    msg[QStringLiteral("page")] = currentPage();
    msg[QStringLiteral("total")] = pageCount();
    broadcastToAuthenticated(msg);
}

void UBCompanionServer::onPageSelectionChanged(int /*index*/)
{
    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("page_changed");
    msg[QStringLiteral("page")] = currentPage();
    msg[QStringLiteral("total")] = pageCount();
    broadcastToAuthenticated(msg);
}

void UBCompanionServer::onZoomChanged(qreal zoom)
{
    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("state");
    msg[QStringLiteral("zoom")] = zoom;
    broadcastToAuthenticated(msg);
}

void UBCompanionServer::onPenColorChanged()
{
    sendState();
}

// ---------------------------------------------------------------------------
// Slots – WebSocket lifecycle
// ---------------------------------------------------------------------------

void UBCompanionServer::onNewConnection()
{
    if (!mServer)
        return;

    while (mServer->hasPendingConnections()) {
        QWebSocket *socket = mServer->nextPendingConnection();
        if (!socket)
            continue;

        connect(socket, &QWebSocket::textMessageReceived,
                this, &UBCompanionServer::onClientMessage);
        connect(socket, &QWebSocket::disconnected,
                this, &UBCompanionServer::onClientDisconnected);

        // Ask the client to authenticate
        QJsonObject challenge;
        challenge[QStringLiteral("type")] = QStringLiteral("auth_required");
        challenge[QStringLiteral("hint")] = QStringLiteral("Enter the PIN shown in OpenBoard");
        sendJson(socket, challenge);
    }
}

void UBCompanionServer::onClientMessage(const QString &message)
{
    QWebSocket *socket = qobject_cast<QWebSocket *>(sender());
    if (!socket)
        return;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    QJsonObject obj = doc.object();

    // --- Authentication handshake ---
    if (!mAuthenticated.contains(socket)) {
        QString suppliedPin = obj.value(QStringLiteral("pin")).toString();
        if (suppliedPin == mPin) {
            mAuthenticated.insert(socket);
            QJsonObject ok;
            ok[QStringLiteral("type")] = QStringLiteral("auth_ok");
            sendJson(socket, ok);

            // Send full state on successful auth
            sendPreview(socket);
            sendState(socket);
            emit statusChanged();
        } else {
            QJsonObject fail;
            fail[QStringLiteral("type")] = QStringLiteral("auth_fail");
            sendJson(socket, fail);
        }
        return;
    }

    // --- Authenticated command ---
    if (obj.contains(QStringLiteral("cmd")))
        handleCommand(socket, obj);
}

void UBCompanionServer::onClientDisconnected()
{
    QWebSocket *socket = qobject_cast<QWebSocket *>(sender());
    if (!socket)
        return;

    mAuthenticated.remove(socket);
    socket->deleteLater();
    emit statusChanged();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void UBCompanionServer::broadcastToAuthenticated(const QJsonObject &obj)
{
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    const QString text = QString::fromUtf8(data);
    for (QWebSocket *client : qAsConst(mAuthenticated))
        client->sendTextMessage(text);
}

void UBCompanionServer::sendJson(QWebSocket *socket, const QJsonObject &obj)
{
    if (!socket)
        return;
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    socket->sendTextMessage(QString::fromUtf8(data));
}

void UBCompanionServer::sendPreview(QWebSocket *socket)
{
    if (!mBoardController)
        return;

    auto scene = mBoardController->activeScene();
    if (!scene)
        return;

    QRectF sceneRect = scene->normalizedSceneRect();
    if (sceneRect.isEmpty())
        sceneRect = QRectF(0, 0, 1280, 960);

    // Render scene to an image
    QImage image(kThumbnailWidth, kThumbnailHeight, QImage::Format_RGB32);
    image.fill(Qt::white);

    QRectF targetRect(0, 0, kThumbnailWidth, kThumbnailHeight);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    scene->setRenderingContext(UBGraphicsScene::NonScreen);
    scene->setRenderingQuality(UBItem::RenderingQualityHigh, UBItem::CacheNotAllowed);
    scene->render(&painter, targetRect, sceneRect);
    scene->setRenderingContext(UBGraphicsScene::Screen);
    scene->setRenderingQuality(UBItem::RenderingQualityHigh, UBItem::CacheAllowed);
    painter.end();

    // Encode as JPEG → base64
    QByteArray jpegData;
    QBuffer buf(&jpegData);
    buf.open(QIODevice::WriteOnly);
    image.save(&buf, "JPEG", kJpegQuality);
    buf.close();

    const QString base64 = QString::fromLatin1(jpegData.toBase64());

    QJsonObject msg;
    msg[QStringLiteral("type")]  = QStringLiteral("preview");
    msg[QStringLiteral("page")]  = currentPage();
    msg[QStringLiteral("total")] = pageCount();
    msg[QStringLiteral("image")] = base64;

    if (socket) {
        sendJson(socket, msg);
    } else {
        broadcastToAuthenticated(msg);
    }
}

void UBCompanionServer::sendState(QWebSocket *socket)
{
    if (!mBoardController)
        return;

    // Tool name
    const int toolInt = UBDrawingController::drawingController()->stylusTool();
    QString toolName;
    switch (toolInt) {
    case (int)UBStylusTool::Pen:     toolName = QStringLiteral("pen");     break;
    case (int)UBStylusTool::Marker:  toolName = QStringLiteral("marker");  break;
    case (int)UBStylusTool::Eraser:  toolName = QStringLiteral("eraser");  break;
    case (int)UBStylusTool::Pointer: toolName = QStringLiteral("pointer"); break;
    default:                         toolName = QStringLiteral("pen");     break;
    }

    // Current pen color (use light-background color as canonical)
    const QColor color = mBoardController->penColorOnLightBackground();

    QJsonObject msg;
    msg[QStringLiteral("type")]  = QStringLiteral("state");
    msg[QStringLiteral("tool")]  = toolName;
    msg[QStringLiteral("color")] = color.name();
    msg[QStringLiteral("zoom")]  = mBoardController->currentZoom();

    if (socket) {
        sendJson(socket, msg);
    } else {
        broadcastToAuthenticated(msg);
    }
}

void UBCompanionServer::handleCommand(QWebSocket *sender, const QJsonObject &cmd)
{
    if (!mBoardController)
        return;

    const QString command = cmd.value(QStringLiteral("cmd")).toString();

    if (command == QLatin1String("next_page")) {
        mBoardController->nextScene();
    } else if (command == QLatin1String("prev_page")) {
        mBoardController->previousScene();
    } else if (command == QLatin1String("goto_page")) {
        int page = cmd.value(QStringLiteral("page")).toInt(1);
        // pages are 1-based in the protocol, 0-based internally
        mBoardController->setActiveSceneIndex(page - 1);
    } else if (command == QLatin1String("set_tool")) {
        const QString tool = cmd.value(QStringLiteral("tool")).toString();
        int toolInt = (int)UBStylusTool::Pen;
        if (tool == QLatin1String("marker"))       toolInt = (int)UBStylusTool::Marker;
        else if (tool == QLatin1String("eraser"))  toolInt = (int)UBStylusTool::Eraser;
        else if (tool == QLatin1String("pointer")) toolInt = (int)UBStylusTool::Pointer;
        UBDrawingController::drawingController()->setStylusTool(toolInt);
        sendState();
    } else if (command == QLatin1String("set_color")) {
        const QString colorStr = cmd.value(QStringLiteral("color")).toString();
        QColor color(colorStr);
        if (color.isValid()) {
            mBoardController->setPenColorOnLightBackground(color);
            mBoardController->setPenColorOnDarkBackground(color);
            sendState();
        }
    } else if (command == QLatin1String("clear")) {
        mBoardController->clearScene();
        sendPreview();
    } else if (command == QLatin1String("pointer")) {
        // Translate normalised (0..1) coordinates to scene coordinates and
        // synthesise a mouse move on the control view.
        if (mBoardController->controlView()) {
            const qreal nx = cmd.value(QStringLiteral("x")).toDouble();
            const qreal ny = cmd.value(QStringLiteral("y")).toDouble();

            QWidget *view = mBoardController->controlView();
            const int px = static_cast<int>(nx * view->width());
            const int py = static_cast<int>(ny * view->height());

            QPoint widgetPos(px, py);
            QMouseEvent moveEvent(QEvent::MouseMove, widgetPos, Qt::NoButton,
                                  Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(view, &moveEvent);
        }
    } else if (command == QLatin1String("get_state")) {
        sendPreview(sender);
        sendState(sender);
        QJsonObject pageMsg;
        pageMsg[QStringLiteral("type")]  = QStringLiteral("page_changed");
        pageMsg[QStringLiteral("page")]  = currentPage();
        pageMsg[QStringLiteral("total")] = pageCount();
        sendJson(sender, pageMsg);
    }
}

QString UBCompanionServer::generatePin() const
{
    // 6-digit numeric PIN (100000–999999, always 6 visible digits)
    quint32 raw = QRandomGenerator::global()->bounded(100000u, 1000000u);
    return QString::number(raw);
}

int UBCompanionServer::pageCount() const
{
    if (!mBoardController)
        return 1;
    return mBoardController->pageCount();
}

int UBCompanionServer::currentPage() const
{
    if (!mBoardController)
        return 1;
    return mBoardController->activeSceneIndex() + 1; // 1-based
}
