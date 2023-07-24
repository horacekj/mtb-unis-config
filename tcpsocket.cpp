#include "tcpsocket.h"

tcpsocket::tcpsocket(QObject *parent)
    : QObject{parent}
{
    tim = new QTimer(this);
}

void tcpsocket::doConnect(QString addr, int port)
{
    socket = new QTcpSocket(this);

    connect(socket, SIGNAL(connected()),this, SLOT(socket_connected()));
    connect(socket, SIGNAL(disconnected()),this, SLOT(socket_disconnected()));
    connect(socket, SIGNAL(bytesWritten(qint64)),this, SLOT(bytesWritten(qint64)));
    connect(socket, SIGNAL(readyRead()),this, SLOT(readyRead()));

    qDebug() << "connecting...";

    tim->setInterval(1000);

    qDebug() << QString(addr);
    qDebug() << QString::number(port);

    this->id = 0;

    // this is not blocking call
    socket->connectToHost(addr, port, QAbstractSocket::ReadWrite, QAbstractSocket::IPv4Protocol);

    // we need to wait...
    if(!socket->waitForConnected(1500))
    {
        qDebug() << "Error: " << socket->errorString();
    }
}

void tcpsocket::doDisconnect()
{
    if (isConnected) {
        isConnected = false;
        socket->disconnect();
        delete socket;
    }
}

void tcpsocket::socket_connected()
{
    qDebug() << "connected...";
    isConnected = true;
    emit connected();

    // Hey server, tell me about you.
    //socket->write("HEAD / HTTP/1.0\r\n\r\n\r\n\r\n");
}

void tcpsocket::socket_disconnected()
{
    qDebug() << "disconnected...";
    emit disconnected();
    isConnected = false;
    delete socket;
}

void tcpsocket::bytesWritten(qint64 bytes)
{
    qDebug() << bytes << " bytes written...";
}

void tcpsocket::readyRead()
{
    static QByteArray buf;
    QJsonDocument jsondoc;

    buf.append(socket->readAll());
    if (buf.contains('\n')) {
        QList<QByteArray> chunks = buf.split('\n');
        buf.clear();
        if (!chunks.last().endsWith('\n'))
            buf.append(chunks.takeLast()); // leave only last chunk
        foreach (QByteArray chunk, chunks) {
            if (chunk.length() > 5) { // ignore short messages
                //qDebug() << "found:";
                //qDebug() << qPrintable(chunk);
                jsondoc = QJsonDocument::fromJson(chunk);
                if (jsondoc.isObject() == false) {
                    qDebug() << "response is bad JSON, can\'t parse.";
                    continue;
                }
                qDebug() << "parsed:";
                qDebug() << qPrintable(jsondoc.toJson(QJsonDocument::Indented));
                QJsonObject lvl1 = jsondoc.object();
                if (lvl1.contains("command")) {
                    if (lvl1["command"] == "module_set_outputs") {
                        if (lvl1.contains("outputs"))
                            emit getModuleStateOut(lvl1);
                    }
                    if (lvl1["command"] == "module") {
                        if (lvl1.contains("module")) {
                            QJsonObject lvl2 = lvl1["module"].toObject();
                            if (lvl2.constBegin()->isObject()) {
                                QJsonObject lvl3 = lvl2.constBegin()->toObject();
                                if (lvl3.contains("state")) {
                                    QJsonObject lvl4 = lvl3["state"].toObject();
                                    emit getModuleStateOut(lvl4);
                                    emit getModuleStateIn(lvl4);
                                }
                            }
                        }
                    }

                    if (lvl1["command"] == "module_outputs_changed") {
                        if (lvl1.contains("module_outputs_changed")) {
                            QJsonObject lvl2 = lvl1["module_outputs_changed"].toObject();
                            emit getModuleStateOut(lvl2);
                        }
                    }
                    if (lvl1["command"] == "module_inputs_changed") {
                        if (lvl1.contains("module_inputs_changed")) {
                            QJsonObject lvl2 = lvl1["module_inputs_changed"].toObject();
                            emit getModuleStateIn(lvl2);
                        }
                    }

                }
            }
        }
        //qDebug() << "buf length: "+QString::number(buf.length());
    }



}

void tcpsocket::subscribeModule(int module)
{
    /*
{
    "command": "module_subscribe"/"module_unsubscribe",
    "type": "request",
    "id": 12,
    "addresses": [10, 11, 20]
}
*/
    QJsonArray tmpArr;
    tmpArr.append(QJsonValue(module));
    QJsonObject tmpObj;
    tmpObj["command"] = QJsonValue("module_subscribe");
    tmpObj["type"] = QJsonValue("request");
    tmpObj["addresses"] =  tmpArr;
    tmpObj["id"] = QJsonValue(this->id++);
    sendJson(tmpObj);
    getOutputs(module); // get state now
}

void tcpsocket::getOutputs(int module)
{
    QJsonObject tmpObj;
    tmpObj["command"] = QJsonValue("module");
    tmpObj["type"] = QJsonValue("request");
    tmpObj["address"] = QJsonValue(module);
    tmpObj["state"] = QJsonValue(true);
    tmpObj["id"] = QJsonValue(this->id++);

    sendJson(tmpObj);
    return;
}

void tcpsocket::setOutputs(int module, int port, int state)
{
    QJsonObject tmpOut;
    QJsonObject tmpOutOne;

    tmpOutOne["type"] = QJsonValue("plain");
    tmpOutOne["value"] = QJsonValue(state);
    tmpOut[QString::number(port)] = tmpOutOne;

    QJsonObject tmpObj;
    tmpObj["type"] = "request";

    tmpObj["id"] = QJsonValue(this->id++);
    tmpObj["command"] = QJsonValue("module_set_outputs");
    tmpObj["address"] = QJsonValue(module);
    tmpObj["outputs"] = tmpOut;
    tmpObj["state"] = QJsonValue(false);

    sendJson(tmpObj);
    return;
}

void tcpsocket::setServoOuts(int module, int servo, int state)
{
    QJsonObject tmpOut;
    QJsonObject tmpOutOne;
    int out1 = 16+(servo-1)*2;
    int out2 = out1 + 1;
    int state1 = (state) ? 0 : 1; // inversion, state 1 = first output 0, second output 1
    int state2 = 1-state1;

    // output states
    tmpOutOne["type"] = QJsonValue("plain");
    tmpOutOne["value"] = QJsonValue(state1);
    tmpOut[QString::number(out1)] = tmpOutOne;
    tmpOutOne["type"] = QJsonValue("plain");
    tmpOutOne["value"] = QJsonValue(state2);
    tmpOut[QString::number(out2)] = tmpOutOne;

    // main json
    QJsonObject tmpObj;
    tmpObj["type"] = "request";
    tmpObj["id"] = QJsonValue(this->id++);
    tmpObj["command"] = QJsonValue("module_set_outputs");
    tmpObj["address"] = QJsonValue(module);
    tmpObj["outputs"] = tmpOut;
    tmpObj["state"] = QJsonValue(false); // dont report states

    sendJson(tmpObj);
}

void tcpsocket::setServoManual(int module, int servo, uint8_t position)
{
    // main json
    QJsonObject tmpObj;
    QJsonArray tmpArr;

    tmpArr.push_back(QJsonValue(3));
    tmpArr.push_back(QJsonValue(servo << 1));
    tmpArr.push_back(QJsonValue(position));

    tmpObj["type"] = "request";
    tmpObj["id"] = QJsonValue(this->id++);
    tmpObj["command"] = QJsonValue("module_specific_command");
    tmpObj["address"] = QJsonValue(module);
    tmpObj["data"] = tmpArr;

    sendJson(tmpObj);
}

void tcpsocket::setServoManualEnd(int module)
{
    // main json
    QJsonObject tmpObj;
    QJsonArray tmpArr;

    tmpArr.push_back(QJsonValue(3));
    tmpArr.push_back(QJsonValue(0));

    tmpObj["type"] = "request";
    tmpObj["id"] = QJsonValue(this->id++);
    tmpObj["command"] = QJsonValue("module_specific_command");
    tmpObj["address"] = QJsonValue(module);
    tmpObj["data"] = tmpArr;

    sendJson(tmpObj);
}

void tcpsocket::sendJson(QJsonObject json)
{
    if (isConnected) {
        QJsonDocument tmpJson(json);
        QByteArray req = tmpJson.toJson(QJsonDocument::Compact);
        //qDebug() << qPrintable(tmpJson.toJson(QJsonDocument::Compact));
        qDebug() << qPrintable(req);
        socket->write(req+QByteArray("\n\n"));
    }
}