#ifndef KOMODO_QT_ZSignDIALOG_POPUP_H
#define KOMODO_QT_ZSignDIALOG_POPUP_H

#include <QDialog>
#include <QMessageBox>
#include <QString>
#include <QTimer>

class PlatformStyle;

namespace Ui {
    class ZSendCoinsDialog_popup;
}

class ZSendCoinsDialog_popup : public QDialog
{
    Q_OBJECT

public:
    explicit ZSendCoinsDialog_popup(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~ZSendCoinsDialog_popup();
    void SetUnsignedTransaction (QString sTransaction);
    bool GetSignedTransaction(QString *qsResult);

public Q_SLOTS:
    void SendTransaction( );
    void ClosePopup();

private:
    Ui::ZSendCoinsDialog_popup *ui;
    const PlatformStyle *platformStyle;

//private Q_SLOTS:
//    void on_signButton_clicked();

};

#endif // KOMODO_QT_ZSignDIALOG_POPUP_H
