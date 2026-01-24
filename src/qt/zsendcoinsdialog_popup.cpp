#include "zsendcoinsdialog_popup.h"
#include "ui_zsendcoinsdialog_popup.h"
#include <QClipboard>
#include <QApplication>

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

  QString qsText = ui->teResult->toPlainText();
  qsText = qsText.trimmed();
  
  // Remove quotation marks if present
  if (qsText.startsWith("\"") && qsText.endsWith("\""))
  {
    qsText = qsText.mid(1, qsText.length() - 2);
  }
  
  // Check if we have valid hex data
  if (!qsText.isEmpty())
  {
    std::string hexInput = qsText.toStdString();
    // Verify it's hex
    if (hexInput.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
    {
      *qsResult = qsText;
      return true;
    }
  }
  return false;
}

void ZSendCoinsDialog_popup::SendTransaction( )
{
  QString qsText = ui->teResult->toPlainText();
  qsText = qsText.trimmed();
  
  // Remove quotation marks if present
  if (qsText.startsWith("\"") && qsText.endsWith("\""))
  {
    qsText = qsText.mid(1, qsText.length() - 2);
  }
  
  // Check if we have valid hex data
  if (qsText.isEmpty())
  {
    //Raw transaction not yet pasted. Nothing to send.
    return;
  }
  
  std::string hexInput = qsText.toStdString();
  if (hexInput.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
  {
    //Invalid hex. Nothing to send.
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

void ZSendCoinsDialog_popup::SetOfflineMode(bool offline)
{
  // Hide the broadcast button in offline mode
  ui->sendTransactionButton->setVisible(!offline);
}

void ZSendCoinsDialog_popup::on_copyInputButton_clicked()
{
  QClipboard *clipboard = QApplication::clipboard();
  clipboard->setText(ui->teInput->toPlainText());
}

void ZSendCoinsDialog_popup::on_pasteResultButton_clicked()
{
  QClipboard *clipboard = QApplication::clipboard();
  ui->teResult->setPlainText(clipboard->text());
}

void ZSendCoinsDialog_popup::on_copyResultButton_clicked()
{
  QClipboard *clipboard = QApplication::clipboard();
  clipboard->setText(ui->teResult->toPlainText());
}
