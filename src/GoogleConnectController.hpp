/*
 * GoogleConnectController.hpp
 *
 *  Created on: 19 oct. 2014
 *      Author: pierre
 */

#ifndef GOOGLECONNECTCONTROLLER_HPP_
#define GOOGLECONNECTCONTROLLER_HPP_

#include <QtCore/QObject>
#include <bb/cascades/WebView>
#include <QSettings>

class GoogleConnectController : public QObject {
    Q_OBJECT;


private:
    bb::cascades::WebView               *m_WebView;
    QSettings                           *m_Settings;

    QStringList                          m_MessagesID;
    QStringList                          m_ThreadsID;
    QStringList                          m_Messages;
    QList<int>                           m_HistoryID;
    QList<int>                           m_IdxMessageToPush;
    QStringList                          m_Froms;

    QString                              m_LastThread;
    int                                  m_HistoryIndex;
    int                                  m_NBMessageExpected;
    QString                              m_LastSynchId;

    bool                                 m_StopListing;

    QString                              m_WithButNoKey;

public:
     GoogleConnectController            (QObject *parent = 0);
     virtual ~GoogleConnectController   ()                      {};


     void getToken              ();
     void renewToken            ();
     void parse                 (const QString &message);
     void parseRefresh          (const QString &message);


private:
     void checkOrder            (bool flush = false);

public Q_SLOTS:
    void logInRequest           ();
    void save                   (const QString &key);
    void checkReply             ();
    void checkRefresh           ();

    void getMessages            (const QString &with, int nbMessages);
    void getRemainingMessages   (QString lastMessageId);

    void getMessageList         ();
    void getMessageReply        ();


    void setWebView             (QObject *webView);
    void webviewTitleChanged    (const QString &title);

Q_SIGNALS:

    void closeConnect();

    void messageLoaded          (const QString &from, const QString &what, const QString &messageId);
    void synchCompleted         ();

};




#endif /* GOOGLECONNECTCONTROLLER_HPP_ */