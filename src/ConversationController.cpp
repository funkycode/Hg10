/*
 * ConversationController.cpp
 *
 *  Created on: 13 oct. 2014
 *      Author: pierre
 */


#include "ConversationController.hpp"
#include "ConversationManager.hpp"
#include "DropBoxConnectController.hpp"
#include "GoogleConnectController.hpp"
#include "FileTransfert.hpp"
#include "XMPPService.hpp"

#include "Image/HFRNetworkAccessManager.hpp"
#include "ConversationManager.hpp"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>


#include <bb/cascades/Application>
#include <bb/cascades/ThemeSupport>
#include <bb/cascades/ColorTheme>
#include <bb/cascades/Theme>
#include <bb/cascades/pickers/FilePicker>
#include <bb/system/SystemDialog>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/GroupDataModel>


ConversationController::ConversationController(QObject *parent) : QObject(parent),
            m_WebView(NULL),
            m_ListView(NULL),
            m_LinkActivity(NULL),
            m_HistoryCleared(false),
            m_IsRoom(false),
            m_UploadingAudio(false),
            m_FileTransfert(NULL) {

    bool check = connect(ConversationManager::get(), SIGNAL(historyLoaded()), this, SLOT(updateView()));
    Q_ASSERT(check);
    Q_UNUSED(check);

    check = connect(ConversationManager::get(), SIGNAL(messageReceived(const QString &, const QString &)), this, SLOT(pushMessage(const QString &, const QString &)));
    Q_ASSERT(check);

    check = connect(ConversationManager::get(), SIGNAL(chatStateNotify(int)), this, SLOT(chatStateUpdate(int)));
    Q_ASSERT(check);

    check = connect(ConversationManager::get(), SIGNAL(historyMessage(QString, QString)), this, SLOT(pushHistory(QString, QString)));
    Q_ASSERT(check);

    check = connect(XMPP::get(), SIGNAL(connectedXMPP()), this, SLOT(linkEstablished()));
    Q_ASSERT(check);

    check = connect(XMPP::get(), SIGNAL(connectionFailed()), this, SLOT(waitingLink()));
    Q_ASSERT(check);


}

bool ConversationController::isOwnMessage(const QString &from) {

    if(from.toLower() == ConversationManager::get()->getUser().toLower()) {
        return true;
    }

    return false;
}

void ConversationController::waitingLink() {
    if(m_LinkActivity == NULL)
        return;

    m_LinkActivity->start();
}

void ConversationController::linkEstablished() {
    if(m_LinkActivity == NULL)
        return;

    m_LinkActivity->stop();
}

void ConversationController::load(const QString &id, const QString &avatar, const QString &name) {
    if(avatar.mid(0,9).toLower() == "asset:///")
        m_DstAvatar = QDir::currentPath() + "/app/native/assets/" +  avatar.mid(9);
    else
        m_DstAvatar = avatar;

    XMPP::get()->askConnectionStatus();

    m_HistoryCleared = false;
    ConversationManager::get()->load(id, name);
}

void ConversationController::updateView() {
    if(m_WebView == NULL) {
        qWarning() << "did not received the webview. quit.";
        return;
    }

    QSettings settings("Amonchakai", "Hg10");

    QFile htmlTemplateFile(QDir::currentPath() + "/app/native/assets/template.html");
    if(bb::cascades::Application::instance()->themeSupport()->theme()->colorTheme()->style() == bb::cascades::VisualStyle::Dark) {
        htmlTemplateFile.setFileName(QDir::currentPath() + "/app/native/assets/template_black.html");
    }
    QFile htmlEndTemplateFile(QDir::currentPath() + "/app/native/assets/template_end.html");

    QString ownAvatar = ConversationManager::get()->getAvatar();
    if(ownAvatar.mid(0,9).toLower() == "asset:///")
        ownAvatar = QDir::currentPath() + "/app/native/assets/" +  ownAvatar.mid(9);


    // -----------------------------------------------------------------------------------------------
    // customize template
    if (htmlTemplateFile.open(QIODevice::ReadOnly) && htmlEndTemplateFile.open(QIODevice::ReadOnly)) {
        QString htmlTemplate = htmlTemplateFile.readAll();
        QString endTemplate = htmlEndTemplateFile.readAll();

       // -----------------------------------------------------------------------------------------------
       // adjust font size
        if(settings.value("fontSize", 28).value<int>() != 28) {
            htmlTemplate.replace("font-size: 28px;", "font-size: " + QString::number(settings.value("fontSize").value<int>()) + "px;");
        }


       // -----------------------------------------------------------------------------------------------
       // choose background image
        {
            QString directory = QDir::homePath() + QLatin1String("/ApplicationData/Customization");
            QString filename;
            if(QFile::exists(directory + "/" + ConversationManager::get()->getAdressee() + ".xml")) {
                filename = directory + "/" + ConversationManager::get()->getAdressee() + ".xml";
            } else {
                if(QFile::exists(directory +"/default.xml")) {
                    filename = directory + "/default.xml";
                }
            }

            if(!filename.isEmpty()) {
                QFile file(filename);
                if (file.open(QIODevice::ReadOnly)) {
                    QTextStream stream(&file);
                    QString themeSettings = stream.readAll();

                    QRegExp wallpaper("<wallpaper url=\"([^\"]+)\"");
                    if(wallpaper.indexIn(themeSettings) != -1) {

                        if(bb::cascades::Application::instance()->themeSupport()->theme()->colorTheme()->style() == bb::cascades::VisualStyle::Dark) {
                            htmlTemplate.replace("html { background: #000000; height: 100%;", "html { height: 100%;");
                        }

                        emit wallpaperChanged("file://" + wallpaper.cap(1));
                    }

                    file.close();
                }
            } else emit wallpaperChanged("");

            if(QFile::exists(directory + "/" + ConversationManager::get()->getAdressee() + ".css")) {
                QFile file(directory + "/" + ConversationManager::get()->getAdressee() + ".css");

                if (file.open(QIODevice::ReadOnly)) {
                    QTextStream stream(&file);
                    QString themeSettings = stream.readAll();
                    file.close();

                    QString suffix;
                    if(bb::cascades::Application::instance()->themeSupport()->theme()->colorTheme()->style() == bb::cascades::VisualStyle::Dark) {
                        suffix = "_black";
                    }

                    htmlTemplate.replace("</style><link rel=\"stylesheet\" href=\"bubble" + suffix + ".css\">", themeSettings + "\n\r</style>");

                }

            }

        }

        // -----------------------------------------------------------------------------------------------
        // preload history

        const History& history = ConversationManager::get()->getHistory();
        {
            m_AudioMessages.clear();
            QFile audioFileHist(QDir::homePath() + QLatin1String("/ApplicationData/AudioMessages/AudioHistory.txt"));
            if (audioFileHist.open(QIODevice::ReadOnly)) {
                AudioMessage message;
                QDataStream stream(&audioFileHist);
                stream >> message;
                while(!message.m_DistUrl.isEmpty()) {
                    m_AudioMessages.push_back(message);

                    message.m_DistUrl = "";
                    stream >> message;
                }

                audioFileHist.close();
            }
        }

        QString body;
        for(int i = std::max(0, history.m_History.size()-10) ; i < history.m_History.size() ; ++i) {
            const TimeEvent &e = history.m_History.at(i);

            if(isOwnMessage(e.m_Who)) {
                body +=  QString("<div class=\"bubble-right\"><div class=\"bubble-right-avatar\"><img src=\"file:///" + ownAvatar + ".square.png" + "\" /></div>")
                                   + "<p>" + renderMessage(e.m_What) + "</p>"
                               + "</div>";

            } else {
                body +=  QString("<div class=\"bubble-left\"><div class=\"bubble-left-avatar\"><img src=\"file:///" + m_DstAvatar + ".square.png" + "\" /></div>")
                                   + "<p>" + renderMessage(e.m_What) + "</p>"
                               + "</div>";
            }
        }


        m_WebView->setHtml(htmlTemplate + body  + endTemplate, "file:///" + QDir::homePath() + "/../app/native/assets/");
        emit complete();
    }
}

void ConversationController::refreshHistory(const QString &id, const QString &avatar, const QString &name) {
    ConversationManager::get()->deleteHistory();
    //ConversationManager::get()->clear();
    updateView();

    m_HistoryCleared = false;
    ConversationManager::get()->load(id, name);
}

bool ConversationController::isImage(const QString &url) {

    QString ext;
    if(url.length() > 3)
        ext = url.mid(url.size()-3,3);

    if(ext.toLower() == "gif") return true;
    if(ext.toLower() == "png") return true;
    if(ext.toLower() == "jpg") return true;
    if(ext.toLower() == "jpeg") return true;
    if(ext.toLower() == "bmp") return true;
    if(ext.toLower() == "tga") return true;

    return false;
}


void ConversationController::getContentBehindLink(const QString &message) {

    return;

    QSettings settings("Amonchakai", "Hg10");

    QNetworkRequest request(QUrl(message.toAscii()));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    request.setRawHeader("Authorization", ("Bearer " + settings.value("access_token").value<QString>()).toAscii());

    QNetworkReply* reply = HFRNetworkAccessManager::get()->get(request);
    bool ok = connect(reply, SIGNAL(finished()), this, SLOT(checkReplyGetContent()));
    Q_ASSERT(ok);
    Q_UNUSED(ok);
}


void ConversationController::checkReplyGetContent() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());

    QString response;
    if (reply) {
        if (reply->error() == QNetworkReply::NoError) {
            const int available = reply->bytesAvailable();
            if (available > 0) {
                const QByteArray buffer(reply->readAll());
                response = QString::fromUtf8(buffer);

                qDebug() << response;
            }

        } else {
            qDebug() << "reply... " << reply->errorString();
        }

        reply->deleteLater();
    }
}

QString ConversationController::renderMessage(const QString &message, bool showImg) {
    QRegExp url("(http[s]*://[^ ]+)");
    //url.setMinimal(true);
    url.setCaseSensitivity(Qt::CaseInsensitive);

    if(message.indexOf("https://plus.google.com/photos/albums/") != -1) {
        getContentBehindLink(message);
    }


    int pos = 0;
    int lastPos = 0;
    QString nMessage;

    while((pos = url.indexIn(message, lastPos)) != -1) {
        nMessage += message.mid(lastPos, pos-lastPos);

        if(showImg && isImage(url.cap(1))) {
            nMessage += "<img src=\"" + url.cap(1) + "\" onclick=\"sendURL(\'OPEN_IMAGE:" + url.cap(1) + "\');\" />";
        } else {
            bool audioMessage = false;
            QString localAudioUrl;

            for(int i = m_AudioMessages.size()-1 ; i >=0 && !audioMessage && !m_AudioMessages.isEmpty() ; --i) {
                if(m_AudioMessages.at(i).m_DistUrl == url.cap(1)) {
                    audioMessage = true;
                    localAudioUrl = m_AudioMessages.at(i).m_LocalUrl;
                }
            }

            if(audioMessage) {
                QString icon = QDir::currentPath() + "/app/native/assets/images/";
                if(bb::cascades::Application::instance()->themeSupport()->theme()->colorTheme()->style() == bb::cascades::VisualStyle::Dark) {
                    icon += "sound_white.png";
                } else {
                    icon += "sound.png";
                }
                nMessage = QString("<img src=\"") + icon + "\" height=\"100px\" width=\"auto\" onclick=\"sendURL(\'PLAY_SOUND:"  + localAudioUrl + "\');\" />";
            } else {
                nMessage += "<a href=\"" + url.cap(1) + "\">" + url.cap(1).mid(0, 20) + "..." + "</a>";
            }
        }

        lastPos = pos + url.matchedLength();
    }
    nMessage += message.mid(lastPos);

    nMessage.replace("\\n", "<br/>");

    return nMessage;

}


void ConversationController::pushMessage(const QString &from, const QString &message) {
    if(m_WebView == NULL) {
        qWarning() << "did not received the webview. quit.";
        return;
    }

    qDebug() << "push message via JS...";

    QString ownAvatar = ConversationManager::get()->getAvatar();
    if(ownAvatar.mid(0,9).toLower() == "asset:///")
        ownAvatar = QDir::currentPath() + "/app/native/assets/" +  ownAvatar.mid(9);


    bool ownMessage = isOwnMessage(from);
    QString lmessage = renderMessage(message, true);
    lmessage.replace("\"","\\\"");

    if(ownMessage)
        m_WebView->evaluateJavaScript("pushMessage(1, \"" + lmessage +"\", \"file:///" + ownAvatar + ".square.png" + "\");");
    else {
        if(ConversationManager::get()->isAdressee(from))
            m_WebView->evaluateJavaScript("pushMessage(0, \"" + lmessage +"\", \"file:///" + m_DstAvatar + ".square.png\");");
    }
}



void ConversationController::pushHistory(const QString &from, const QString &message) {
    if(m_WebView == NULL) {
        qWarning() << "did not received the webview. quit.";
        return;
    }


    if(!m_HistoryCleared) {
        m_HistoryCleared = true;
        m_WebView->evaluateJavaScript("clearHistory();");
    }

    QString ownAvatar = ConversationManager::get()->getAvatar();
    if(ownAvatar.mid(0,9).toLower() == "asset:///")
        ownAvatar = QDir::currentPath() + "/app/native/assets/" +  ownAvatar.mid(9);

    bool ownMessage = isOwnMessage(from);
    QString lmessage = renderMessage(message, true);
    lmessage.replace("\"","\\\"");

    if(ownMessage)
        m_WebView->evaluateJavaScript("pushHistory(0, 1, \"" + lmessage +"\", \"file:///" + ownAvatar + ".square.png" + "\");");
    else
        m_WebView->evaluateJavaScript("pushHistory(0, 0, \"" + lmessage +"\", \"file:///" + m_DstAvatar + ".square.png\");");
}

void ConversationController::send(const QString& message) {
    qDebug() << "CALL!";
    if(message.isEmpty())
        return;

    ConversationManager::get()->sendMessage(message);

    if(m_WebView == NULL) {
        qWarning() << "did not received the webview. quit.";
        return;
    }

    QString ownAvatar = ConversationManager::get()->getAvatar();
    if(ownAvatar.mid(0,9).toLower() == "asset:///")
        ownAvatar = QDir::currentPath() + "/app/native/assets/" +  ownAvatar.mid(9);

    QString lmessage = renderMessage(message, true);
    lmessage.replace("\"","\\\"");

    m_WebView->evaluateJavaScript("pushMessage(1, \"" + lmessage +"\", \"file:///" + ownAvatar + ".square.png" + "\");");
}


void ConversationController::sendAudioData(const QString &message) {
    m_UploadingAudio = true;
    sendData(message);
}

void ConversationController::sendData(const QString &file) {
    // send data via XMPP
    //ConversationManager::get()->sendData(file);

    if(file.isEmpty())
        return;

    // is it a connection to google?
    if(m_FileTransfert == NULL) {
        QSettings settings("Amonchakai", "Hg10");
        if(!settings.value("DropBoxEnabled", false).toBool())
            initGoogleDrive();
        else
            initDropbox();

    }

    if(m_FileTransfert == NULL)
        return;

    m_FileTransfert->putFile(file);
}


void ConversationController::chatStateUpdate(int state) {
    m_WebView->evaluateJavaScript("chatStateUpdate(" + QString::number(state) + ");");
}


QString ConversationController::getNextAudioName() {
    QString directory = QDir::homePath() + QLatin1String("/ApplicationData/AudioMessages/");
    QString name;
    if (QFile::exists(directory)) {
        QDir dir(directory);
        dir.setNameFilters(QStringList() << "*.m4a");
        dir.setFilter(QDir::Files);
        int index = 0;
        foreach(QString dirFile, dir.entryList()) {
            index = std::max(index, dirFile.mid(8,dirFile.length()-12).toInt());
        }
        name = "message_" + QString::number(index+1) + ".m4a";

    } else {
        QDir dir;
        dir.mkpath(directory);

        name = "message_1.m4a";
    }

    m_AudioFileName = directory + name;

    return m_AudioFileName;
}


void ConversationController::initDropbox() {
    DropBoxConnectController *dropbox = new DropBoxConnectController(this);

    bool check = connect(dropbox, SIGNAL(uploaded()), this, SLOT(uploaded()));
    Q_ASSERT(check);
    Q_UNUSED(check);

    check = connect(dropbox, SIGNAL(shared(const QString &)), this, SLOT(shared(const QString &)));
    Q_ASSERT(check);

    check = connect(dropbox, SIGNAL(uploading(int)), this, SLOT(fowardUploadingProcess(int)));
    Q_ASSERT(check);

    m_FileTransfert = dropbox;
}

void ConversationController::initGoogleDrive() {

    GoogleConnectController *transfert= ConversationManager::get()->getFileTransfert();

    if(transfert == NULL) {
        return;
    }

    bool check = connect(transfert, SIGNAL(uploaded()), this, SLOT(uploaded()));
    Q_ASSERT(check);
    Q_UNUSED(check);

    check = connect(transfert, SIGNAL(shared(const QString &)), this, SLOT(shared(const QString &)));
    Q_ASSERT(check);

    check = connect(transfert, SIGNAL(uploading(int)), this, SLOT(fowardUploadingProcess(int)));
    Q_ASSERT(check);



    m_FileTransfert = dynamic_cast<FileTransfert*>(transfert);
}

void ConversationController::uploaded() {
    m_FileTransfert->share();
}

void ConversationController::shared(const QString &url) {
    if(!m_UploadingAudio)
        emit receivedUrl(url);
    else {
        m_UploadingAudio = false;
        QString icon = QDir::currentPath() + "/app/native/assets/images/";
        if(bb::cascades::Application::instance()->themeSupport()->theme()->colorTheme()->style() == bb::cascades::VisualStyle::Dark) {
            icon += "sound_white.png";
        } else {
            icon += "sound.png";
        }
        QString nMessage = QString("<img src=\"") + icon + "\" height=\"100px\" width=\"auto\" onclick=\"sendURL(\'PLAY_SOUND:"  + m_AudioFileName + "\');\" />";
        nMessage.replace("\"","\\\"");

        QString ownAvatar = ConversationManager::get()->getAvatar();
        if(ownAvatar.mid(0,9).toLower() == "asset:///")
            ownAvatar = QDir::currentPath() + "/app/native/assets/" +  ownAvatar.mid(9);

        m_WebView->evaluateJavaScript("pushMessage(1, \"" + nMessage +"\", \"file:///" + ownAvatar + ".square.png" + "\");");

        emit receivedUrl("");

        ConversationManager::get()->sendMessage(url);

        QFile audioFileHist(QDir::homePath() + QLatin1String("/ApplicationData/AudioMessages/AudioHistory.txt"));
        if (audioFileHist.open(QIODevice::Append)) {
            AudioMessage message;
            message.m_DistUrl = url;
            message.m_LocalUrl = m_AudioFileName;

            QDataStream stream(&audioFileHist);
            stream << message;

            audioFileHist.close();
        }
    }


}

void ConversationController::fowardUploadingProcess(int status) {
    emit uploading(status);
}



void ConversationController::setWallpaper() {
    using namespace bb::cascades;
    using namespace bb::cascades::pickers;

    FilePicker* filePicker = new FilePicker();
    filePicker->setType(FileType::Picture);
    filePicker->setTitle("Select Picture");
    filePicker->setMode(FilePickerMode::Picker);
    filePicker->open();

    // Connect the fileSelected() signal with the slot.
    QObject::connect(filePicker,
        SIGNAL(fileSelected(const QStringList&)),
        this,
        SLOT(onWallpaperSelected(const QStringList&)));

    // Connect the canceled() signal with the slot.
    QObject::connect(filePicker,
        SIGNAL(canceled()),
        this,
        SLOT(onWallpaperSelectCanceled()));
}

void ConversationController::onWallpaperSelected(const QStringList& list) {
    sender()->deleteLater();

    using namespace bb::cascades;
    using namespace bb::system;

    SystemDialog *dialog = new SystemDialog("All of them", "This one");

    dialog->setTitle(tr("Wallpaper"));
    dialog->setBody(tr("Set the wallpaper for which contact?"));

    bool success = connect(dialog,
         SIGNAL(finished(bb::system::SystemUiResult::Type)),
         this,
         SLOT(onPromptFinishedSetWallpaper(bb::system::SystemUiResult::Type)));

    if (success) {
        // Signal was successfully connected.
        // Now show the dialog box in your UI.
        m_NewWallpaper = list.first();
        dialog->show();
    } else {
        // Failed to connect to signal.
        // This is not normal in most cases and can be a critical
        // situation for your app! Make sure you know exactly why
        // this has happened. Add some code to recover from the
        // lost connection below this line.
        dialog->deleteLater();
    }
}

void ConversationController::onPromptFinishedSetWallpaper(bb::system::SystemUiResult::Type type) {
    QString directory = QDir::homePath() + QLatin1String("/ApplicationData/Customization");
    if (!QFile::exists(directory)) {
        QDir dir;
        dir.mkpath(directory);
    }

    // set the wallpaper on all chats
    if(type == bb::system::SystemUiResult::ConfirmButtonSelection) {


        QFile file(directory + "/default.xml");

        if (file.open(QIODevice::WriteOnly)) {
            QTextStream stream(&file);
            stream << "<root>";
            stream << QString("<wallpaper url=\"") + m_NewWallpaper + "\" />";
            stream << "</root>";
            file.close();
        }

    // set the wallpaper only for one user
    } else if (type == bb::system::SystemUiResult::CancelButtonSelection) {
        QFile file(directory + "/" + ConversationManager::get()->getAdressee() + ".xml" );

        if (file.open(QIODevice::WriteOnly)) {
            QTextStream stream(&file);
            stream << "<root>";
            stream << QString("<wallpaper url=\"") + m_NewWallpaper + "\" />";
            stream << "</root>";
            file.close();
        }
    }

}

void ConversationController::onWallpaperSelectCanceled() {
    sender()->deleteLater();
}

void ConversationController::closeCard() {
    XMPP::get()->closeCard();
}






void ConversationController::loadActionMenu(int id) {
    using namespace bb::cascades;

    if(m_ListView == NULL)
        return;


    GroupDataModel* dataModel = dynamic_cast<GroupDataModel*>(m_ListView->dataModel());
    if (dataModel) {
        dataModel->clear();
    } else {
        qDebug() << "create new model";
        dataModel = new GroupDataModel(
                QStringList() << "image"
                              << "category"
                              << "caption"
                              << "action"
                 );
        m_ListView->setDataModel(dataModel);
    }



    // ----------------------------------------------------------------------------------------------
    // push data to the view

    QString filename =  tr("/app/native/assets/data/action_list.xml");
    if(id == 1)
        filename =  tr("/app/native/assets/data/emojies_list.xml");



    QFile actionFile(QDir::currentPath() + filename);


    QRegExp caption("title=\"([^\"]+)\"");
    QRegExp category("category=\"([^\"]+)\"");
    QRegExp image("url=\"([^\"]*)\"");
    QRegExp actionReg("action=\"([0-9]+)\"");

    if (actionFile.open(QIODevice::ReadOnly)) {
        QString actionList = actionFile.readAll();

        int pos = 0;
        while(pos != -1) {
            ActionComposerItem *action = NULL;
            pos = caption.indexIn(actionList, pos);
            if(pos != -1) {
                action = new ActionComposerItem(this);
                action->setCaption(caption.cap(1));
                pos += caption.matchedLength();
            } else break;

            pos = category.indexIn(actionList, pos);
            pos += category.matchedLength();
            action->setCategory(category.cap(1));

            pos = image.indexIn(actionList, pos);
            pos += image.matchedLength();
            action->setImage(image.cap(1));

            pos = actionReg.indexIn(actionList, pos);
            pos += actionReg.matchedLength();
            action->setAction(actionReg.cap(1).toInt());

            dataModel->insert(action);

        }
    }

}


