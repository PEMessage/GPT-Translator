#include "controller.h"

using namespace std;

Setting::Setting(QObject * parent): QObject{parent}
{
    _configPath = QStandardPaths::locate(QStandardPaths::AppConfigLocation, "config.json", QStandardPaths::LocateFile);
    if(_configPath == ""){
        if(QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation).length() > 0){
            QString path = QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation).at(0);
            QDir dir(path);
            if (!dir.exists()){
                dir.mkdir(path);
                qDebug() << "mkdir:" << path;
            }
            _configPath = path + "/config.json";

        }else{
            assert("no writeable location found");
            return;
        }
    }else{
        loadConfig();
    }

    QFile configFile(_configPath);
    qDebug() << QStandardPaths::displayName(QStandardPaths::AppConfigLocation);

    if(!configFile.exists()){
        updateConfig();
    }
}

bool Setting::loadConfig()
{
   QFile file(_configPath);
   string content = "";
   if(file.open(QIODevice::ReadOnly)){
       content = file.readAll().toStdString();
       file.close();
   }
   QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(content).toUtf8());
    if (!doc.isNull()) {
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            _apiKey = obj.value("apiKey").toString();
            _model = obj.value("model").toString();
            return true;
        }
    }
    return false;
}

void Setting::updateConfig()
{
    QString s = "{\"apiKey\":\"" + _apiKey + "\",\"model\":\"" + _model + "\"}";
    QFile file(_configPath);
    if(file.open(QIODevice::WriteOnly)){
        QTextStream out(&file);
        out << s;
        file.close();
    }
}


Controller::Controller(QObject *parent)
    : QObject{parent}
{
    networkManager = new QNetworkAccessManager(this);

}

QJsonObject Controller::createMessage(const QString& role,const QString& content){
    QJsonObject message;
    message.insert("role",role);
    message.insert("content",content);
    return message;
}


std::tuple<QString, bool> Controller::_getContent(QString &str)
{
    QJsonDocument doc = QJsonDocument::fromJson(str.toUtf8());
    if (!doc.isNull()) {
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
             QString text = "";
            if(obj.contains("error")){
                text = obj.value("error").toObject().value("message").toString();
                return std::make_tuple(text, false);
            }else{
                text = obj.value("choices").toArray().at(0).toObject().value("delta").toObject().value("content").toString();
                return std::make_tuple(text, true);
            }
        }
    }
    return std::make_tuple("", false);
}

std::tuple<QString, bool> Controller::_parseResponse(QByteArray &ba)
{
    QString data;
    bool error = false;
    QStringList lines = QString::fromUtf8(ba).split("data:");
    for (const QString &line : lines) {
        QString eventData = line.trimmed();;
        qDebug() <<eventData;
        QString text;
        bool haveError;
        std::tie(text, haveError) = _getContent(eventData);
        data += text;
        error |= haveError;
    }
    return std::make_tuple(data, error);

}


void Controller::streamReceived()
{

    QByteArray response = reply->readAll();
    QString text;
    bool haveError;
    std::tie(text, haveError) = _parseResponse(response);
    _data += text;
    responseData(_data);

}

void Controller::sendMessage(QString &str)
{

    QUrl apiUrl("https://api.openai.com/v1/chat/completions");
      QNetworkRequest request(apiUrl);
      request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
      request.setRawHeader("Authorization", QString::fromStdString("Bearer %1").arg("").toUtf8());
      request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork); // Events shouldn't be cached

      QJsonObject requestData;
      QJsonArray messages;
      requestData.insert("model", "gpt-3.5-turbo");
      requestData.insert("stream", true);
      qDebug() << _transToLang;
      QString systemcmd = QString::fromStdString("Translate anything that I say to %1. Only return the translate result. Don’t interpret it.").arg(_transToLang);
      messages.append(createMessage("system",systemcmd));
      messages.append(createMessage("user",str));

      requestData.insert("messages", messages);
      QJsonDocument requestDoc(requestData);
      QByteArray requestDataBytes = requestDoc.toJson();
      qDebug() << requestDataBytes;

      _data = "";
      responseData(_data);
      isRequesting(true);
      reply = networkManager->post(request, requestDataBytes);
      connect(reply, SIGNAL(readyRead()), this, SLOT(streamReceived()));


      connect(reply, &QNetworkReply::finished,this, [=]() {
          isRequesting(false);
          qDebug() << "finished";
          QByteArray response = reply->readAll();
          QString text;
          bool haveError;
          std::tie(text, haveError) = _parseResponse(response);
          _data += text;
          responseData(_data);
          if (reply->error() == QNetworkReply::NoError) {
              responseError("");
          } else {
              if(reply->error() > 0 && reply->error() < 100){
                  if(reply->error() != QNetworkReply::OperationCanceledError){
                     responseError("network error");
                  }

              }else if(reply->error() > 100 && reply->error() < 200){
                  responseError("proxy error");
              }else if(reply->error() > 200 && reply->error() < 300){
                  responseError("content error");
              }else if(reply->error() > 300 && reply->error() < 400){
                  responseError("protocol error");
              }else if(reply->error() > 400 && reply->error() < 500){
                  responseError("server error");
              }
              qDebug() << reply->error() ;
              qDebug() << "网络错误："+  reply->errorString();
          }
          reply->deleteLater();
      });
}


void Controller::abort()
{
    reply->abort();
}