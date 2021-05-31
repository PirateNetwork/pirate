#include "zsendcoinsdialog_popup.h"
#include "ui_zsendcoinsdialog_popup.h"

ZSendCoinsDialog_popup::ZSendCoinsDialog_popup(const PlatformStyle *_platformStyle, QWidget *parent) :
    ui(new Ui::ZSendCoinsDialog_popup),
    platformStyle(_platformStyle)
{
  ui->setupUi(this);

  connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(ClosePopup()));
  connect(ui->sendTransactionButton, SIGNAL(clicked()), this, SLOT(SendTransaction()));
}

ZSendCoinsDialog_popup::~ZSendCoinsDialog_popup()
{
  delete ui;
}


void ZSendCoinsDialog_popup::SetUnsignedTransaction (QString sTransaction)
{
  ui->teInput->setText(sTransaction);
  return;
}

bool ZSendCoinsDialog_popup::GetSignedTransaction(QString *qsResult)
{
  *qsResult="";

  QString qsText= ui->teResult->toPlainText();
  if (qsText.contains("sendrawtransaction "))
  {
    *qsResult=qsText.replace("sendrawtransaction ","");
    return true;
  }
  return false;
}

void ZSendCoinsDialog_popup::SendTransaction( )
{
  QString qsText= ui->teResult->toPlainText();
  if (!qsText.contains("sendrawtransaction "))
  {
    //Raw transaction not yet pasted. Nothing to send.
    return;
  }

  //Close the popup, but leave the transaction in the result dialog.
  //zsendcoinsdialog will retrieve the transaction with
  //GetSignedTransaction() and initiate transmission of the transaction
  this->close();
  return;
}

void ZSendCoinsDialog_popup::ClosePopup( )
{
  //Clear the text boxes
  ui->teInput->clear();
  ui->teResult->clear();

  //Close the popup
  this->close();
  return;
}
