// Copyright (c) 2011-2016 The Bitcoin Core developers
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

#include "newseed.h"
#include "ui_newseed.h"
#include "newwallet.h"
#include "ui_newwallet.h"
#include "restoreseed.h"
#include "ui_restoreseed.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QPainter>
#include <QRadialGradient>
#include <QStyle>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

SplashScreen::SplashScreen(Qt::WindowFlags f, const NetworkStyle *networkStyle) :
    QWidget(0, f), curAlignment(0)
{
    // set reference point, paddings
    int paddingRight            = 125;
    int paddingTop              = 60;
    int titleVersionVSpace      = 25; // version down from title
    int titleCopyrightVSpace    = 25; // down from title

    float fontFactor            = 1.0;
    float devicePixelRatio      = 0.75;
#if QT_VERSION > 0x050100
    devicePixelRatio = ((QGuiApplication*)QCoreApplication::instance())->devicePixelRatio();
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
    // pixmap = QPixmap(splashSize);
    pixmap.load(":/backgrounds/splash");
    pixmap = pixmap.scaled(splashSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

#if QT_VERSION > 0x050100
    // change to HiDPI if it makes sense
    pixmap.setDevicePixelRatio(devicePixelRatio);
#endif

    QPainter pixPaint(&pixmap);
    // pixPaint.setPen(QColor(50, 50, 50));

    // draw a slightly radial gradient
    // QRadialGradient gradient(QPoint(0, 0), splashSize.width() / devicePixelRatio);
    // gradient.setColorAt(0, Qt::white); //top left color
    // gradient.setColorAt(1, QColor(188, 172, 94)); //bottom right color
    // QRect rGradient(QPoint(0, 0), splashSize);
    // pixPaint.fillRect(rGradient, gradient);

    // draw the pirate.icon, expected size of PNG: 1024x1024
    // QRect rectIcon(QPoint(20,40), QSize(160,160));
    //
    // const QSize requiredSize(1024,1024);
    // QPixmap icon(networkStyle->getAppIcon().pixmap(requiredSize));
    //
    // pixPaint.drawPixmap(rectIcon, icon);

    // check font size and drawing with
    pixPaint.setPen(QColor(193, 157, 66));
    pixPaint.setFont(QFont(font, 45*fontFactor));
    QFontMetrics fm = pixPaint.fontMetrics();
    int titleTextWidth = fm.width(splashTitle);
    if (titleTextWidth > 176) {
        fontFactor = fontFactor * 176 / titleTextWidth;
    }

    pixPaint.setFont(QFont(font, 60*fontFactor));
    fm = pixPaint.fontMetrics();
    titleTextWidth  = fm.width(splashTitle);
    // pixPaint.drawText(((pixmap.width()/devicePixelRatio)/2)-(titleTextWidth/2),paddingTop,splashTitle);

    pixPaint.setFont(QFont(font, 25*fontFactor));

    // if the version string is too long, reduce size
    fm = pixPaint.fontMetrics();
    int versionTextWidth  = fm.width(versionText);
    // if(versionTextWidth > titleTextWidth+paddingRight-10) {
    //     pixPaint.setFont(QFont(font, 10*fontFactor));
    //     titleVersionVSpace -= 5;
    // }
    pixPaint.drawText(((pixmap.width()/devicePixelRatio))-(versionTextWidth)-30,((pixmap.height()/devicePixelRatio)/2)+85,versionText);

    // draw copyright stuff
    // {
    //     pixPaint.setFont(QFont(font, 15*fontFactor));
    //     const int x = pixmap.width()/devicePixelRatio-titleTextWidth-paddingRight;
    //     const int y = paddingTop+titleCopyrightVSpace-10;
    //     QRect copyrightRect(x, y, pixmap.width() + x - paddingRight + 10, pixmap.height() - y);
    //     pixPaint.drawText(copyrightRect, Qt::AlignLeft | Qt::AlignTop, copyrightText);
    // }

    fm = pixPaint.fontMetrics();
    int copyrightTextWidth  = fm.width(copyrightText);
    pixPaint.drawText(((pixmap.width()/devicePixelRatio))-(copyrightTextWidth)-30,((pixmap.height()/devicePixelRatio)/2)+105,copyrightText);

    // draw additional text if special network
    if(!titleAddText.isEmpty()) {
        QFont boldFont = QFont(font, 10*fontFactor);
        boldFont.setWeight(QFont::Bold);
        pixPaint.setFont(boldFont);
        fm = pixPaint.fontMetrics();
        int titleAddTextWidth  = fm.width(titleAddText);
        pixPaint.drawText(pixmap.width()/devicePixelRatio-titleAddTextWidth-10,15,titleAddText);
    }

    pixPaint.end();

    // Set window title
    setWindowTitle(titleText + " " + titleAddText);

    // Resize window and move to center of desktop, disallow resizing
    QRect r(QPoint(), QSize(pixmap.size().width()/devicePixelRatio,pixmap.size().height()/devicePixelRatio));
    resize(r.size());
    setFixedSize(r.size());
    move(QApplication::desktop()->screenGeometry().center() - r.center());

    //Set Main Create Wallet Widget
    QVBoxLayout *seedLayout= new QVBoxLayout(this);
    seed = new QWidget;
    seed->setObjectName("seed");
    seed->setStyleSheet( "QWidget#seed{ background-color : #000000; color: #ffffff; }" );
    seedLayout->addWidget(seed);
    seed->setVisible(false);

    //Create Internal Layout
    layout = new QVBoxLayout(this);
    layout->setEnabled(false);

    //Add Icon to the top
    pirateIcon = new QLabel;
    QPixmap pic(":/icons/komodo");
    QSize pickSize(128,128);
    pic = pic.scaled(pickSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pirateIcon->setPixmap(pic);

    //Center Icon Vertically
    QVBoxLayout *viconLayout = new QVBoxLayout(this);
    viconLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding));
    viconLayout->addWidget(pirateIcon);
    viconLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding));

    //Center Icon Horizontally
    QHBoxLayout *hiconLayout = new QHBoxLayout(this);
    hiconLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed));
    hiconLayout->addLayout(viconLayout);
    hiconLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed));

    //Add Icon to Internal Layout
    layout->addLayout(hiconLayout);
    pirateIcon->setVisible(false);

    //New Wallwt Style Sheet
    QString styleSheet = "QWidget {background-color: #303030; color: #ffffff;} QFrame#outerFrame {background-color: #282828; color: #ffffff;} QFrame#innerFrame {background-color: #303030; color: #ffffff;} QGroupBox {background-color: #282828; color: #ffffff;} QLineEdit {background-color: #303030; color: #ffffff;} QPlainTextEdit {background-color: #303030; color: #ffffff;}";
    styleSheet = styleSheet + " QRadioButton { background-color: #303030;color: #ffffff; spacing: 5px;}";
    styleSheet = styleSheet + " QRadioButton::indicator {background-color: #000000; color: #ffffff; width: 8px;height: 8px; border: 4px solid #000000; border-radius: 8px;}";
    styleSheet = styleSheet + " QRadioButton::indicator:checked {background-color: rgba(25,225,25); color: #ffffff; width: 8px; height: 8px; border: 4px solid #000000; border-radius: 8px;}";

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

    //Add button layout
    QHBoxLayout *buttonLayout = new QHBoxLayout(this);
    buttonLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed));

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

    layout->addLayout(buttonLayout);

    //Add Layout to primary widget
    seed->setLayout(layout);

    // Connect signals for seed creation bittons
    connect(btnTypeSelect, SIGNAL(clicked()), this, SLOT(on_btnTypeSelected_clicked()));
    connect(btnRestore, SIGNAL(clicked()), this, SLOT(on_btnRestore_clicked()));
    connect(btnDone, SIGNAL(clicked()), this, SLOT(on_btnDone_clicked()));

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

static void showNewPhrase(SplashScreen *splash)
{
    splash->newSeed->setVisible(true);
    splash->btnDone->setVisible(true);
    splash->newSeed->ui->txtSeed->setPlainText(QString::fromStdString(recoverySeedPhrase));
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress, bool resume_possible)
{
	if (splash->isVisible()) {
		InitMessage(splash, title + std::string("\n") + (resume_possible ? _("(press q to shutdown and continue later)") : _("press q to shutdown")) + strprintf("\n%d", nProgress) + "%");
	}
}

#ifdef ENABLE_WALLET
void SplashScreen::ConnectWallet(CWallet* wallet)
{
    wallet->ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2, false));
    connectedWallets.push_back(wallet);
}
#endif

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
    } else {
        pwalletMain->createType = RANDOM;
    }
}

void SplashScreen::on_btnRestore_clicked()
{
      std::string phrase = this->restoreSeed->ui->txtSeed->toPlainText().toStdString();

      if (pwalletMain->IsValidPhrase(phrase)) {
          recoverySeedPhrase = phrase;
          pwalletMain->createType = RECOVERY;
      } else {
          this->restoreSeed->ui->lblInvalid->setVisible(true);
      }
      this->seed->setVisible(false);
}

void SplashScreen::on_btnDone_clicked()
{
    this->seed->setVisible(false);
    pwalletMain->createType = COMPLETE;
}

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.InitCreateWallet.connect(boost::bind(showCreateWallet,this));
    uiInterface.InitShowPhrase.connect(boost::bind(showNewPhrase,this));
    uiInterface.InitMessage.connect(boost::bind(InitMessage, this, _1));
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2, _3));
#ifdef ENABLE_WALLET
    uiInterface.LoadWallet.connect(boost::bind(&SplashScreen::ConnectWallet, this, _1));
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.InitMessage.disconnect(boost::bind(InitMessage, this, _1));
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2, _3));
#ifdef ENABLE_WALLET
    for (CWallet* const & pwallet : connectedWallets) {
        pwallet->ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2, false));
    }
#endif
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
