// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "splashscreen.h"

#include "networkstyle.h"

#include "clientversion.h"
#include "init.h"
#include "util.h"
#include "ui_interface.h"
#include "version.h"
#include "guiconstants.h"

#include "newseed.h"
#include "ui_newseed.h"
#include "newwallet.h"
#include "ui_newwallet.h"
#include "restoreseed.h"
#include "ui_restoreseed.h"
#include "openwallet.h"
#include "ui_openwallet.h"

#include "support/allocators/secure.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/walletmanager.h"
#endif

#include <boost/filesystem.hpp>

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopWidget>
#include <QPainter>
#include <QRadialGradient>
#include <QStyle>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QScreen>

SplashScreen::SplashScreen(const NetworkStyle *networkStyle) :
    QWidget(), curAlignment(0), fZeroWalletStartup(false)
{
    // set reference point, paddings
    int paddingRight            = 125;
    int paddingTop              = 60;
    int titleVersionVSpace      = 25; // version down from title
    int titleCopyrightVSpace    = 25; // down from title

    float fontFactor            = 1.0;
    float devicePixelRatio      = 0.75;
#if QT_VERSION > 0x050100
    devicePixelRatio = static_cast<QGuiApplication*>(QCoreApplication::instance())->devicePixelRatio();
#endif

    // define text to place
    QString titleText       = tr("Treasure Chest");
    QString splashTitle     = tr("Treasure Chest");
    QString versionText     = QString("Version %1").arg(QString::fromStdString(FormatFullVersion()));
    QString copyrightText   = QString::fromUtf8(CopyrightHolders(strprintf("\xc2\xA9 %u-%u ", 2018, COPYRIGHT_YEAR)).c_str());
    QString titleAddText    = networkStyle->getTitleAddText();

    QString font            = QApplication::font().toString();

    // create a bitmap according to device pixelratio
    QSize splashSize(640*devicePixelRatio,456*devicePixelRatio);
    pixmap.load(":/backgrounds/splash");
    pixmap = pixmap.scaled(splashSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

#if QT_VERSION > 0x050100
    // change to HiDPI if it makes sense
    pixmap.setDevicePixelRatio(devicePixelRatio);
#endif

    QPainter pixPaint(&pixmap);

    // check font size and drawing with
    pixPaint.setPen(QColor(193, 157, 66));
    pixPaint.setFont(QFont(font, 45*fontFactor));
    QFontMetrics fm = pixPaint.fontMetrics();
    int titleTextWidth = fm.horizontalAdvance(splashTitle);
    if (titleTextWidth > 176) {
        fontFactor = fontFactor * 176 / titleTextWidth;
    }

    pixPaint.setFont(QFont(font, 60*fontFactor));
    fm = pixPaint.fontMetrics();
    titleTextWidth  = fm.horizontalAdvance(splashTitle);
    pixPaint.setFont(QFont(font, 25*fontFactor));

    // if the version string is too long, reduce size
    fm = pixPaint.fontMetrics();
    int versionTextWidth  = fm.horizontalAdvance(versionText);
    pixPaint.drawText(((pixmap.width()/devicePixelRatio))-(versionTextWidth)-30,((pixmap.height()/devicePixelRatio)/2)+85,versionText);

    fm = pixPaint.fontMetrics();
    int copyrightTextWidth  = fm.horizontalAdvance(copyrightText);
    pixPaint.drawText(((pixmap.width()/devicePixelRatio))-(copyrightTextWidth)-30,((pixmap.height()/devicePixelRatio)/2)+105,copyrightText);

    // draw additional text if special network
    if(!titleAddText.isEmpty()) {
        QFont boldFont = QFont(font, 10*fontFactor);
        boldFont.setWeight(QFont::Bold);
        pixPaint.setFont(boldFont);
        fm = pixPaint.fontMetrics();
        int titleAddTextWidth  = fm.horizontalAdvance(titleAddText);
        pixPaint.drawText(pixmap.width()/devicePixelRatio-titleAddTextWidth-10,15,titleAddText);
    }

    pixPaint.end();

    // Set window title
    setWindowTitle(titleText + " " + titleAddText);

    // Resize window and move to center of desktop, disallow resizing
    QRect r(QPoint(), QSize(pixmap.size().width()/devicePixelRatio,pixmap.size().height()/devicePixelRatio));
    resize(r.size());
    setFixedSize(r.size());
    move(QGuiApplication::primaryScreen()->geometry().center() - r.center());

    //Set Main Create Wallet Widget
    QVBoxLayout *seedLayout = new QVBoxLayout(this);
    seedLayout->setContentsMargins(0, 0, 0, 0);
    seed = new QWidget;
    seed->setObjectName("seed");
    seed->setStyleSheet( "QWidget#seed{ background-color : #202020; color: #ffffff; }" );
    seedLayout->addWidget(seed);
    seed->setVisible(false);

    //Create Internal Layout
    //QWidget *splashControls = new QWidget;
    layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setEnabled(false);
    //splashControls->setLayout(layout);

    //Add Layout to primary widget
    seed->setLayout(layout);

    //Add Icon to the top
    pirateIcon = new QLabel;
    QPixmap pic(":/icons/komodo");
    QSize pickSize(128,128);
    pic = pic.scaled(pickSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pirateIcon->setPixmap(pic);

    //Center Icon Vertically
    QWidget *vicon = new QWidget;
    QVBoxLayout *viconLayout = new QVBoxLayout();
    vicon->setLayout(viconLayout);
    viconLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding));
    viconLayout->addWidget(pirateIcon);
    viconLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding));


    //Center Icon Horizontally
    QWidget *hicon = new QWidget;
    QHBoxLayout *hiconLayout = new QHBoxLayout();
    hicon->setLayout(hiconLayout);
    hiconLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed));
    hiconLayout->addWidget(vicon);
    hiconLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed));


    //Add Icon to Internal Layout
    layout->addWidget(hicon);
    pirateIcon->setVisible(false);

    //New Wallwt Style Sheet
    QString styleSheet = "QWidget {background-color: #303030; color: #ffffff;} QFrame#outerFrame {background-color: #282828; color: #ffffff;} QFrame#innerFrame {background-color: #303030; color: #ffffff;} QGroupBox {background-color: #282828; color: #ffffff;} QLineEdit {background-color: #303030; color: #ffffff;} QPlainTextEdit {background-color: #303030; color: #ffffff;}";
    styleSheet = styleSheet + " QRadioButton { background-color: #303030;color: #ffffff; spacing: 5px;}";
    styleSheet = styleSheet + " QRadioButton::indicator {background-color: #000000; color: #ffffff; width: 8px;height: 8px; border: 4px solid #000000; border-radius: 8px;}";
    styleSheet = styleSheet + " QRadioButton::indicator:checked {background-color: rgba(25,225,25,1); color: #ffffff; width: 8px; height: 8px; border: 4px solid #000000; border-radius: 8px;}";

    //Add Create Seed
    newWallet = new NewWallet(networkStyle);
    newWallet->setObjectName("newWallet");
    newWallet->setStyleSheet(styleSheet);
    layout->addWidget(newWallet);
    newWallet->setVisible(false);

    //Restore Seed and New Seed Style Sheet
    styleSheet = "QWidget {background-color: #303030; color: #ffffff;} QGroupBox {background-color: #282828; color: #ffffff;} QLineEdit {background-color: #585858; color: #ffffff;} QPlainTextEdit {background-color: #585858; color: #ffffff;}";
    styleSheet = styleSheet + " QFrame#outerFrame {background-color: #303030; color: #ffffff;} QFrame#innerFrame {background-color: #282828; color: #ffffff;}";
    styleSheet = styleSheet + " QLabel#lblInvalid {color: #ff0000;}";

    //Add Restore Seed
    restoreSeed = new RestoreSeed(networkStyle);
    restoreSeed->setObjectName("restoreSeed");
    restoreSeed->setStyleSheet(styleSheet);
    layout->addWidget(restoreSeed);
    restoreSeed->setVisible(false);

    //Add New Seed display
    newSeed = new NewSeed(networkStyle);
    newSeed->setObjectName("newSeed");
    newSeed->setStyleSheet(styleSheet);
    layout->addWidget(newSeed);
    newSeed->setVisible(false);
    connect(newSeed->ui->cmbLanguage, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SplashScreen::on_newSeedLanguageChanged);

    //Add Open Wallet
    openWallet = new OpenWallet(networkStyle);
    openWallet->setObjectName("openWallet");
    openWallet->setStyleSheet(styleSheet);
    layout->addWidget(openWallet);
    openWallet->setVisible(false);

    //Add button layout
    QWidget *buttons = new QWidget;
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(9, 0, 9, 9);
    buttonLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed));
    buttons->setLayout(buttonLayout);

    //Add ok button
    btnTypeSelect = new QPushButton(this);
    btnTypeSelect->setText(tr("ok"));
    btnTypeSelect->setVisible(false);
    btnTypeSelect->setObjectName("btnTypeSelected");
    buttonLayout->addWidget(btnTypeSelect);

    btnRestore = new QPushButton(this);
    btnRestore->setText(tr("Restore"));
    btnRestore->setVisible(false);
    btnRestore->setObjectName("btnRestore");
    buttonLayout->addWidget(btnRestore);

    btnDone = new QPushButton(this);
    btnDone->setText(tr("Done"));
    btnDone->setVisible(false);
    btnDone->setObjectName("btnDone");
    buttonLayout->addWidget(btnDone);

    layout->addWidget(buttons);


    // Connect signals for seed creation bittons
    connect(btnTypeSelect, SIGNAL(clicked()), this, SLOT(on_btnTypeSelected_clicked()));
    connect(btnRestore, SIGNAL(clicked()), this, SLOT(on_btnRestore_clicked()));
    connect(btnDone, SIGNAL(clicked()), this, SLOT(on_btnDone_clicked()));
    connect(openWallet->ui->btnOpen, SIGNAL(clicked()), this, SLOT(on_btnOpen_clicked()));
    connect(openWallet->ui->btnQuit, SIGNAL(clicked()), this, SLOT(on_btnQuit_clicked()));

    subscribeToCoreSignals();
    installEventFilter(this);
}

SplashScreen::~SplashScreen()
{
}

bool SplashScreen::eventFilter(QObject * obj, QEvent * ev) {
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
        if(keyEvent->text()[0] == 'q') {
            StartShutdown();
        }

        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (openWallet->isVisible()) {
                on_btnOpen_clicked();
            }
        }
    }

    return QObject::eventFilter(obj, ev);
}

void SplashScreen::slotFinish(QWidget *mainWin)
{
    Q_UNUSED(mainWin);

    unsubscribeFromCoreSignals();
    /* If the window is minimized, hide() will be ignored. */
    /* Make sure we de-minimize the splashscreen window before hiding */
    if (isMinimized())
        showNormal();
    hide();
    deleteLater(); // No more need for this
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom|Qt::AlignHCenter),
        Q_ARG(QColor, QColor(193, 157, 66)));
}

static void showCreateWallet(SplashScreen *splash)
{
    splash->layout->setEnabled(true);
    splash->seed->setVisible(true);
    splash->pirateIcon->setVisible(true);
    splash->newWallet->setVisible(true);
    splash->btnTypeSelect->setVisible(true);
}

void SplashScreen::startZeroWalletFlow(const std::string& walletName)
{
    fZeroWalletStartup = true;
    zeroWalletName = walletName;
    // Same widget transition InitCreateWallet()'s signal handler drives
    // above for the pre-existing pwalletMain-based flow -- the choice of
    // "new" vs "restore" widgets is identical either way, only what happens
    // once the user picks one differs (see fZeroWalletStartup's own comment,
    // splashscreen.h).
    showCreateWallet(this);
}

static void showNewPhrase(SplashScreen *splash)
{
    splash->newSeed->setVisible(true);
    splash->btnDone->setVisible(true);
    // fZeroWalletStartup selects which of the two flows' own seed-phrase
    // storage to read from -- see its own comment (splashscreen.h).
    std::string phrase = splash->fZeroWalletStartup ? splash->zeroWalletSeedPhrase
                                                     : pwalletMain->recoverySeedPhrase;
    splash->newSeed->ui->txtSeed->setPlainText(QString::fromStdString(phrase));
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress, bool resume_possible)
{
	if (splash->isVisible()) {
		InitMessage(splash, title + std::string("\n") + (resume_possible ? _("(press q to shutdown and continue later)") : _("press q to shutdown")) + strprintf("\n%d", nProgress) + "%");
	}
}

static void NeedUnlockWallet(SplashScreen *splash)
{
    splash->layout->setEnabled(true);
    splash->seed->setVisible(true);
    splash->pirateIcon->setVisible(true);
    splash->openWallet->ui->lblIncorrect->setVisible(false);
    splash->openWallet->setVisible(true);
}

void SplashScreen::on_btnTypeSelected_clicked()
{
    if(!this->newWallet->ui->radioNewWallet->isChecked() && !this->newWallet->ui->radioRestoreWallet->isChecked())
        return;

    this->newWallet->setVisible(false);
    this->btnTypeSelect->setVisible(false);

    if (this->newWallet->ui->radioRestoreWallet->isChecked()) {
        this->restoreSeed->ui->lblInvalid->setVisible(false);
        this->restoreSeed->setVisible(true);
        this->btnRestore->setVisible(true);
    } else if (fZeroWalletStartup) {
        // No pre-existing pwalletMain to seed here (true zero-wallet
        // startup, see fZeroWalletStartup's own comment) -- create the
        // wallet outright instead of just flagging createType and waiting
        // for init.cpp's busy-wait to notice.
        std::string strError;
        if (!CWalletManager::Get().CreateWallet(zeroWalletName, strError, zeroWalletSeedPhrase)) {
            QMessageBox::critical(this, tr("Wallet creation failed"), QString::fromStdString(strError));
            StartShutdown();
            return;
        }
        showNewPhrase(this);
    } else {
        pwalletMain->createType = RANDOM;
    }
}

void SplashScreen::on_btnRestore_clicked()
{
      //Remove multiple white spaces between the words and remove any white spaces at the beginning & end
      QString qPhrase = this->restoreSeed->ui->txtSeed->toPlainText().simplified().trimmed();
      std::string phrase = qPhrase.toStdString();

      std::stringstream stream(phrase);
      unsigned int iCount = std::distance(std::istream_iterator<std::string>(stream), std::istream_iterator<std::string>());
      if (
         (iCount != 12) &&
         (iCount != 18) &&
         (iCount != 24)
         ) {
        QMessageBox msgBox;
        msgBox.setStyleSheet("QLabel{min-width: 350px;}");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);

        msgBox.setText("Invalid length");
        msgBox.setInformativeText("The seed phrase must consist of 12, 18 or 24 words.");
        int ret = msgBox.exec();
        return;
      }


      uint32_t langCode = (uint32_t)restoreSeed->selectedLanguage();
      // fZeroWalletStartup: no pre-existing pwalletMain to validate against
      // (true zero-wallet startup) -- CWallet::IsValidPhrase() only ever
      // constructs a throwaway HDSeed and checks that, never touching the
      // instance it's called on (see its own implementation, wallet.cpp), so
      // checking against a bare HDSeed directly here is equivalent, without
      // needing a CWallet at all.
      bool fValid = fZeroWalletStartup ? HDSeed().IsValidPhrase(phrase, langCode)
                                        : pwalletMain->IsValidPhrase(phrase, langCode);
      if (fValid) {
          this->restoreSeed->ui->lblInvalid->setVisible(false);
          if (fZeroWalletStartup) {
              // Unlike the pre-existing flow below, this creates the wallet
              // immediately rather than just flagging createType -- there is
              // no blocked init thread polling it to hand off to.
              std::string strError, seedPhraseOut;
              SecureString securePhrase;
              securePhrase.reserve(phrase.size() + 1);
              securePhrase = phrase.c_str();
              if (!CWalletManager::Get().CreateWallet(zeroWalletName, strError, seedPhraseOut, securePhrase, langCode)) {
                  // Opus-audit-caught: LoadWallet() (which CreateWallet()
                  // delegates to) already created and committed the file
                  // before this failure -- CreateWallet()'s own
                  // "file already exists" check means simply letting the
                  // user retry, as this used to, fails identically forever.
                  // Discard the failed attempt (deactivate it -- it became
                  // active automatically as the first wallet loaded --
                  // unload it, then delete the now-orphaned, seedless file)
                  // so a retry actually gets a fresh start.
                  std::string strDiscardError;
                  CWalletManager::Get().SetActiveWallet("", strDiscardError);
                  CWalletManager::Get().UnloadWallet(zeroWalletName, strDiscardError);
                  boost::system::error_code ec;
                  boost::filesystem::remove(GetDataDir() / zeroWalletName, ec);
                  this->restoreSeed->ui->lblInvalid->setVisible(true);
                  return;
              }
              this->seed->setVisible(false);
              Q_EMIT walletCreated();
          } else {
              pwalletMain->recoverySeedLangCode = langCode;
              pwalletMain->recoverySeedPhrase = phrase;
              pwalletMain->createType = RECOVERY;
              //Hide the dialog. The program execution will continue
              this->seed->setVisible(false);
          }
      } else {
          //Keep the dialog on the screen, to give the user another attempt at entering a valid seed.
          this->restoreSeed->ui->lblInvalid->setVisible(true);
      }
}

void SplashScreen::on_btnDone_clicked()
{
    this->seed->setVisible(false);
    if (fZeroWalletStartup) {
        // The wallet was already created back in on_btnTypeSelected_clicked()
        // -- this button just confirms the user has recorded the phrase.
        Q_EMIT walletCreated();
    } else {
        pwalletMain->createType = COMPLETE;
    }
}

void SplashScreen::on_btnOpen_clicked()
{
  SecureString passPhrase;
  passPhrase.reserve(MAX_PASSPHRASE_SIZE);
  passPhrase.assign(openWallet->ui->passPhraseEdit->text().toStdString().c_str());

  // Attempt to overwrite text so that they do not linger around in memory
  openWallet->ui->passPhraseEdit->setText(QString(" ").repeated(openWallet->ui->passPhraseEdit->text().size()));
  openWallet->ui->passPhraseEdit->clear();

  if (pwalletMain->OpenWallet(passPhrase)) {
      // OpenWallet() no longer captures this itself (see its own comment,
      // wallet/wallet.cpp) -- init.cpp's automatic KDF-upgrade check and its
      // -zapwallettxes reopen both still need it for pwalletMain specifically,
      // which this startup dialog always operates on. Released before being
      // replaced so a previously captured passphrase isn't left behind,
      // unreachable and never freed, in mlock()'d memory.
      delete strOpeningWalletPassphrase;
      strOpeningWalletPassphrase = new SecureString(passPhrase);
      seed->setVisible(false);
      openWallet->setVisible(false);
      return;
  }

  openWallet->ui->lblIncorrect->setVisible(true);
}

void SplashScreen::on_newSeedLanguageChanged(int index)
{
    std::string phrase;
    if (pwalletMain && pwalletMain->GetSeedPhrase(phrase, (uint32_t)index)) {
        newSeed->ui->txtSeed->setPlainText(QString::fromStdString(phrase));
    }
}

void SplashScreen::on_btnQuit_clicked()
{
    StartShutdown();
}

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.InitNeedUnlockWallet.connect(boost::bind(NeedUnlockWallet, this));
    uiInterface.InitCreateWallet.connect(boost::bind(showCreateWallet, this));
    uiInterface.InitShowPhrase.connect(boost::bind(showNewPhrase, this));
    uiInterface.InitMessage.connect(boost::bind(InitMessage, this, std::placeholders::_1));
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.InitNeedUnlockWallet.disconnect(boost::bind(NeedUnlockWallet, this));
    uiInterface.InitCreateWallet.disconnect(boost::bind(showCreateWallet, this));
    uiInterface.InitShowPhrase.disconnect(boost::bind(showNewPhrase, this));
    uiInterface.InitMessage.disconnect(boost::bind(InitMessage, this, std::placeholders::_1));
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(5, 5, -5, -5);
    painter.setPen(curColor);
    painter.drawText(r, curAlignment, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event)
{
    StartShutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
