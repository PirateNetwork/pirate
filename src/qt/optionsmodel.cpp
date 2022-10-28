// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "optionsmodel.h"

#include "komodounits.h"
#include "guiutil.h"

#include "amount.h"
#include "init.h"
#include "net.h"
#include "netbase.h"
#include "txdb.h" // for -dbcache defaults
#include "intro.h"
#include "main.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif

// #ifdef ENABLE_BIP70
// #include <QNetworkProxy>
// #endif
#include <QDebug>
#include <QSettings>
#include <QStringList>

OptionsModel::OptionsModel(QObject *parent, bool resetSettings) :
    QAbstractListModel(parent)
{
    Init(resetSettings);
}

void OptionsModel::addOverriddenOption(const std::string &option)
{
    strOverriddenByCommandLine += QString::fromStdString(option) + "=" + QString::fromStdString(GetArg(option, "")) + " ";
}

// Writes all missing QSettings with their default values
void OptionsModel::Init(bool resetSettings)
{
    if (resetSettings)
        Reset();

    checkAndMigrate();

    QSettings settings;

    // Ensure restart flag is unset on client startup
    setRestartRequired(false);

    // These are Qt-only settings:

    // Window
    if (!settings.contains("fHideTrayIcon"))
        settings.setValue("fHideTrayIcon", false);
    fHideTrayIcon = settings.value("fHideTrayIcon").toBool();
    Q_EMIT hideTrayIconChanged(fHideTrayIcon);

    if (!settings.contains("fMinimizeToTray"))
        settings.setValue("fMinimizeToTray", false);
    fMinimizeToTray = settings.value("fMinimizeToTray").toBool() && !fHideTrayIcon;

    if (!settings.contains("fMinimizeOnClose"))
        settings.setValue("fMinimizeOnClose", false);
    fMinimizeOnClose = settings.value("fMinimizeOnClose").toBool();

    // Display
    if (!settings.contains("nDisplayUnit"))
        settings.setValue("nDisplayUnit", KomodoUnits::ARRR);
    nDisplayUnit = settings.value("nDisplayUnit").toInt();

    if (!settings.contains("strThirdPartyTxUrls"))
        settings.setValue("strThirdPartyTxUrls", "");
    strThirdPartyTxUrls = settings.value("strThirdPartyTxUrls", "").toString();

    if (!settings.contains("strTheme"))
        settings.setValue("strTheme", "armada");
    strTheme = settings.value("strTheme", "armada").toString();

    // These are shared with the core or have a command-line parameter
    // and we want command-line parameters to overwrite the GUI settings.
    //
    // If setting doesn't exist create it with defaults.
    //
    // If gArgs.SoftSetArg() or gArgs.SoftSetBoolArg() return false we were overridden
    // by command-line and show this in the UI.

    // Main
    if (!settings.contains("nDatabaseCache"))
        settings.setValue("nDatabaseCache", (qint64)nDefaultDbCache);
    if (!SoftSetArg("-dbcache", settings.value("nDatabaseCache").toString().toStdString()))
        addOverriddenOption("-dbcache");

    if (!settings.contains("nThreadsScriptVerif"))
        settings.setValue("nThreadsScriptVerif", DEFAULT_SCRIPTCHECK_THREADS);
    if (!SoftSetArg("-par", settings.value("nThreadsScriptVerif").toString().toStdString()))
        addOverriddenOption("-par");

    if (!settings.contains("strDataDir"))
        settings.setValue("strDataDir", Intro::getDefaultDataDirectory());

    // Wallet
#ifdef ENABLE_WALLET
    if (!settings.contains("fTxDeleteEnabled"))
        settings.setValue("fTxDeleteEnabled", true);
    if (!SoftSetBoolArg("-deletetx", settings.value("fTxDeleteEnabled").toBool()))
        addOverriddenOption("-deletetx");

    if (!settings.contains("fSaplingConsolidationEnabled"))
        settings.setValue("fSaplingConsolidationEnabled", true);
    if (!SoftSetBoolArg("-consolidation", settings.value("fSaplingConsolidationEnabled").toBool()))
        addOverriddenOption("-consolidation");

    if (!settings.contains("fEnableReindex"))
        settings.setValue("fEnableReindex", false);
    if (!SoftSetBoolArg("-reindex", settings.value("fEnableReindex").toBool()))
        addOverriddenOption("-reindex");
    settings.setValue("fEnableReindex", false);

    if (!settings.contains("fEnableZSigning"))
        settings.setValue("fEnableZSigning", false);

    bool fEnableZSigning = settings.value("fEnableZSigning").toBool();
    if (fEnableZSigning==false) {
      settings.setValue("fEnableZSigning_ModeSpend", false);
      settings.setValue("fEnableZSigning_ModeSign",  false);
    } else {
      if (!settings.contains("fEnableZSigning_ModeSpend"))
      {
        settings.setValue("fEnableZSigning_ModeSpend",  true);
      }
      if (!settings.contains("fEnableZSigning_ModeSign"))
      {
        settings.setValue("fEnableZSigning_ModeSign",  false);
      }
    }

    if (!settings.contains("fEnableHexMemo"))
          settings.setValue("fEnableHexMemo", false);
    fEnableHexMemo = settings.value("fEnableHexMemo").toBool();

    if (!settings.contains("fEnableBootstrap"))
        settings.setValue("fEnableBootstrap", false);
    if (settings.value("fEnableBootstrap").toBool() == true) {
      if (!SoftSetArg("-bootstrap", std::string("2")))
          addOverriddenOption("-bootstrap");
    } else {
      if (!SoftSetArg("-bootstrap", std::string("0")))
          addOverriddenOption("-bootstrap");
    }
    settings.setValue("fEnableBootstrap", false);

    if (!settings.contains("fZapWalletTxes"))
        settings.setValue("fZapWalletTxes", false);
    if (settings.value("fZapWalletTxes").toBool() == true) {
      if (!SoftSetArg("-zapwallettxes", std::string("2"))) {
          addOverriddenOption("-zapwallettxes");
      } else {
          LogPrintf("%s: QT ZapWalletTxes setting -rescan=1\n", __func__);
          OverrideSetArg("-rescan", std::string("1"));
      }
    } else {
      if (!SoftSetArg("-zapwallettxes", std::string("0")))
          addOverriddenOption("-zapwallettxes");
    }
    settings.setValue("fZapWalletTxes", false);

#endif

    // Network

    if (!settings.contains("fListen"))
        settings.setValue("fListen", DEFAULT_LISTEN);
    if (!SoftSetBoolArg("-listen", settings.value("fListen").toBool()))
        addOverriddenOption("-listen");

    if (!settings.contains("fEncrypted"))
        settings.setValue("fEncrypted", false);
    if (settings.value("fEncrypted").toBool() == true) {
        if (!SoftSetArg("-tlsenforcement", std::string("1"))) {
            addOverriddenOption("-tlsenforcement");
        }
    } else {
        if (!SoftSetArg("-tlsenforcement", std::string("0"))) {
            addOverriddenOption("-tlsenforcement");
        }
    }

    if (!settings.contains("fUseProxy"))
        settings.setValue("fUseProxy", false);
    if (!settings.contains("addrProxy") || !settings.value("addrProxy").toString().contains(':'))
        settings.setValue("addrProxy", "127.0.0.1:9050");
    // Only try to set -proxy, if user has enabled fUseProxy
    if (settings.value("fUseProxy").toBool() && !SoftSetArg("-proxy", settings.value("addrProxy").toString().toStdString()))
        addOverriddenOption("-proxy");
    else if(!settings.value("fUseProxy").toBool() && !GetArg("-proxy", "").empty())
        addOverriddenOption("-proxy");

    if (!settings.contains("fUseSeparateProxyTor"))
        settings.setValue("fUseSeparateProxyTor", false);
    if (!settings.contains("addrSeparateProxyTor") || !settings.value("addrSeparateProxyTor").toString().contains(':'))
        settings.setValue("addrSeparateProxyTor", "127.0.0.1:9050");

    if (!settings.contains("controlIpTor") || !settings.value("controlIpTor").toString().contains(':'))
        settings.setValue("controlIpTor", "127.0.0.1:9051");
    // Only try to set -torcontrol, if user has enabled fUseSeparateProxyTor
    if (settings.value("fUseSeparateProxyTor").toBool() && !SoftSetArg("-torcontrol", settings.value("controlIpTor").toString().toStdString()))
        addOverriddenOption("-torcontrol");
    else if(!settings.value("fUseSeparateProxyTor").toBool() && !GetArg("-torcontrol", "").empty())
        addOverriddenOption("-torcontrol");

    if (!settings.contains("controlPasswordTor"))
        settings.setValue("controlPasswordTor", "");
    // Only try to set -torcontrol, if user has enabled fUseSeparateProxyTor
    if (settings.value("fUseSeparateProxyTor").toBool() && !SoftSetArg("-torpassword", settings.value("controlPasswordTor").toString().toStdString()))
        addOverriddenOption("-torpassword");
    else if(!settings.value("fUseSeparateProxyTor").toBool() && !GetArg("-torpassword", "").empty())
        addOverriddenOption("-torpassword");

    // Only try to set -onion, if user has enabled fUseSeparateProxyTor
    if (settings.value("fUseSeparateProxyTor").toBool() && !SoftSetArg("-onion", settings.value("addrSeparateProxyTor").toString().toStdString()))
        addOverriddenOption("-onion");
    else if(!settings.value("fUseSeparateProxyTor").toBool() && !GetArg("-onion", "").empty())
        addOverriddenOption("-onion");

    if (!settings.contains("fUseProxyI2P"))
        settings.setValue("fUseProxyI2P", false);

    if (!settings.contains("addrProxyI2P") || !settings.value("addrProxyI2P").toString().contains(':'))
        settings.setValue("addrProxyI2P", "127.0.0.1:7656");
    // Only try to set -i2psam, if user has enabled fUseProxyI2P
    if (settings.value("fUseProxyI2P").toBool() && !SoftSetArg("-i2psam", settings.value("addrProxyI2P").toString().toStdString()))
        addOverriddenOption("-i2psam");
    else if(!settings.value("fUseProxyI2P").toBool() && !GetArg("-i2psam", "").empty())
        addOverriddenOption("-i2psam");

    if (!settings.contains("fIncomingI2P"))
        settings.setValue("fIncomingI2P", false);

    if (settings.value("fIncomingI2P").toBool() && !SoftSetArg("-i2pacceptincoming", std::string("1")))
        addOverriddenOption("-i2pacceptincoming");
    else if(!settings.value("fIncomingI2P").toBool() && !SoftSetArg("-i2pacceptincoming", std::string("0")))
        addOverriddenOption("-i2pacceptincoming");

    // Disable ivp4 and/or ipv6
    if (!settings.contains("fIPv4Disable"))
        settings.setValue("fIPv4Disable", false);

    if (settings.value("fIPv4Disable").toBool() && !SoftSetArg("-disableipv4", std::string("1")))
        addOverriddenOption("-disableipv4");
    else if(!settings.value("fIPv4Disable").toBool() && !SoftSetArg("-disableipv4", std::string("0")))
        addOverriddenOption("-disableipv4");

    if (!settings.contains("fIPv6Disable"))
        settings.setValue("fIPv6Disable", false);

    if (settings.value("fIPv6Disable").toBool() && !SoftSetArg("-disableipv6", std::string("1")))
        addOverriddenOption("-disableipv6");
    else if(!settings.value("fIPv4Disable").toBool() && !SoftSetArg("-disableipv6", std::string("0")))
        addOverriddenOption("-disableipv6");

    // Display
    if (!settings.contains("language"))
        settings.setValue("language", "");
    if (!SoftSetArg("-lang", settings.value("language").toString().toStdString()))
        addOverriddenOption("-lang");

    language = settings.value("language").toString();
}

/** Helper function to copy contents from one QSettings to another.
 * By using allKeys this also covers nested settings in a hierarchy.
 */
static void CopySettings(QSettings& dst, const QSettings& src)
{
    for (const QString& key : src.allKeys()) {
        dst.setValue(key, src.value(key));
    }
}

/** Back up a QSettings to an ini-formatted file. */
static void BackupSettings(const fs::path& filename, const QSettings& src)
{
    qWarning() << "Backing up GUI settings to" << GUIUtil::boostPathToQString(filename);
    QSettings dst(GUIUtil::boostPathToQString(filename), QSettings::IniFormat);
    dst.clear();
    CopySettings(dst, src);
}

void OptionsModel::Reset()
{
    QSettings settings;

    // Backup old settings to chain-specific datadir for troubleshooting
    BackupSettings(GetDataDir(true) / "guisettings.ini.bak", settings);

    // Save the strDataDir setting
    QString dataDir = Intro::getDefaultDataDirectory();
    dataDir = settings.value("strDataDir", dataDir).toString();

    // Remove all entries from our QSettings object
    settings.clear();

    // Set strDataDir
    settings.setValue("strDataDir", dataDir);

    // Set that this was reset
    settings.setValue("fReset", true);

    // default setting for OptionsModel::StartAtStartup - disabled
    if (GUIUtil::GetStartOnSystemStartup())
        GUIUtil::SetStartOnSystemStartup(false);
}

int OptionsModel::rowCount(const QModelIndex & parent) const
{
    return OptionIDRowCount;
}

// read QSettings values and return them
QVariant OptionsModel::data(const QModelIndex & index, int role) const
{
    if(role == Qt::EditRole)
    {
        QSettings settings;
        switch(index.row())
        {
        case StartAtStartup:
            return GUIUtil::GetStartOnSystemStartup();
        case HideTrayIcon:
            return fHideTrayIcon;
        case MinimizeToTray:
            return fMinimizeToTray;
        case MapPortUPnP:
#ifdef USE_UPNP
            return settings.value("fUseUPnP");
#else
            return false;
#endif
        case MinimizeOnClose:
            return fMinimizeOnClose;

        // default proxy
        case ProxyUse:
            return settings.value("fUseProxy");
        case ProxyIP: {
            // contains IP at index 0 and port at index 1
            QStringList strlIpPort = settings.value("addrProxy").toString().split(":", QString::SkipEmptyParts);
            return strlIpPort.at(0);
        }
        case ProxyPort: {
            // contains IP at index 0 and port at index 1
            QStringList strlIpPort = settings.value("addrProxy").toString().split(":", QString::SkipEmptyParts);
            return strlIpPort.at(1);
        }

        // separate Tor proxy
        case ProxyUseTor:
            return settings.value("fUseSeparateProxyTor");
        case ProxyIPTor: {
            // contains IP at index 0 and port at index 1
            QStringList strlIpPort = settings.value("addrSeparateProxyTor").toString().split(":", QString::SkipEmptyParts);
            return strlIpPort.at(0);
        }
        case ProxyPortTor: {
            // contains IP at index 0 and port at index 1
            QStringList strlIpPort = settings.value("addrSeparateProxyTor").toString().split(":", QString::SkipEmptyParts);
            return strlIpPort.at(1);
        }

        case ControlIPTor: {
            // contains IP at index 0 and port at index 1
            QStringList strlIpPort = settings.value("controlIpTor").toString().split(":", QString::SkipEmptyParts);
            return strlIpPort.at(0);
        }
        case ControlPortTor: {
            // contains IP at index 0 and port at index 1
            QStringList strlIpPort = settings.value("controlIpTor").toString().split(":", QString::SkipEmptyParts);
            return strlIpPort.at(1);
        }
        case ControlPasswordTor: {
            return settings.value("controlPasswordTor").toString();
        }

        // I2P proxy
        case ProxyUseI2P:
            return settings.value("fUseProxyI2P");
        case IncomingI2P:
            return settings.value("fIncomingI2P");
        case ProxyIPI2P: {
            // contains IP at index 0 and port at index 1
            QStringList strlIpPort = settings.value("addrProxyI2P").toString().split(":", QString::SkipEmptyParts);
            return strlIpPort.at(0);
        }
        case ProxyPortI2P: {
            // contains IP at index 0 and port at index 1
            QStringList strlIpPort = settings.value("addrProxyI2P").toString().split(":", QString::SkipEmptyParts);
            return strlIpPort.at(1);
        }

        // Disable ivp4 and/or ipv6
        case IPv4Disable:
            return settings.value("fIPv4Disable");
        case IPv6Disable:
            return settings.value("fIPv6Disable");


#ifdef ENABLE_WALLET
        case EnableDeleteTx:
            return settings.value("fTxDeleteEnabled");
        case SaplingConsolidationEnabled:
            return settings.value("fSaplingConsolidationEnabled");
        case EnableReindex:
            return settings.value("fEnableReindex");
        case EnableZSigning:
            //Enable offline transactions in the wallet:
            return settings.value("fEnableZSigning");
        case EnableZSigning_Spend:
            //  Offline transaction role: Create offline transactions for 'viewing only' addresses
            return settings.value("fEnableZSigning_ModeSpend");
        case EnableZSigning_Sign:
            //  Offline transaction role: Sign offline transactions
            return settings.value("fEnableZSigning_ModeSign");
        case EnableHexMemo:
            return fEnableHexMemo;
        case EnableBootstrap:
            return settings.value("fEnableBootstrap");
        case ZapWalletTxes:
            return settings.value("fZapWalletTxes");
#endif
        case Theme:
            return strTheme;
        case DisplayUnit:
            return nDisplayUnit;
        case ThirdPartyTxUrls:
            return strThirdPartyTxUrls;
        case Language:
            return settings.value("language");
        case DatabaseCache:
            return settings.value("nDatabaseCache");
        case ThreadsScriptVerif:
            return settings.value("nThreadsScriptVerif");
        case Listen:
            return settings.value("fListen");
        case EncryptedP2P:
            return settings.value("fEncrypted");
        default:
            return QVariant();
        }
    }
    return QVariant();
}

// write QSettings values
bool OptionsModel::setData(const QModelIndex & index, const QVariant & value, int role)
{
    bool successful = true; /* set to false on parse error */
    if(role == Qt::EditRole)
    {
        QSettings settings;
        switch(index.row())
        {
        case StartAtStartup:
            successful = GUIUtil::SetStartOnSystemStartup(value.toBool());
            break;
        case HideTrayIcon:
            fHideTrayIcon = value.toBool();
            settings.setValue("fHideTrayIcon", fHideTrayIcon);
    		Q_EMIT hideTrayIconChanged(fHideTrayIcon);
            break;
        case MinimizeToTray:
            fMinimizeToTray = value.toBool();
            settings.setValue("fMinimizeToTray", fMinimizeToTray);
            break;
        case MinimizeOnClose:
            fMinimizeOnClose = value.toBool();
            settings.setValue("fMinimizeOnClose", fMinimizeOnClose);
            break;

        // default proxy
        case ProxyUse:
            if (settings.value("fUseProxy") != value) {
                settings.setValue("fUseProxy", value);
                setRestartRequired(true);
            }
            break;
        case ProxyIP:
            {
                // contains current IP at index 0 and current port at index 1
                QStringList strlIpPort = settings.value("addrProxy").toString().split(":", QString::SkipEmptyParts);
                // if that key doesn't exist or has a changed IP
                if (!settings.contains("addrProxy") || strlIpPort.at(0) != value.toString()) {
                    // construct new value from new IP and current port
                    QString strNewValue = value.toString() + ":" + strlIpPort.at(1);
                    settings.setValue("addrProxy", strNewValue);
                    setRestartRequired(true);
                }
            }
            break;
        case ProxyPort:
            {
                // contains current IP at index 0 and current port at index 1
                QStringList strlIpPort = settings.value("addrProxy").toString().split(":", QString::SkipEmptyParts);
                // if that key doesn't exist or has a changed port
                if (!settings.contains("addrProxy") || strlIpPort.at(1) != value.toString()) {
                    // construct new value from current IP and new port
                    QString strNewValue = strlIpPort.at(0) + ":" + value.toString();
                    settings.setValue("addrProxy", strNewValue);
                    setRestartRequired(true);
                }
            }
            break;

        // separate Tor proxy
        case ProxyUseTor:
            if (settings.value("fUseSeparateProxyTor") != value) {
                settings.setValue("fUseSeparateProxyTor", value);
                setRestartRequired(true);
            }
            break;
        case ProxyIPTor:
            {
                // contains current IP at index 0 and current port at index 1
                QStringList strlIpPort = settings.value("addrSeparateProxyTor").toString().split(":", QString::SkipEmptyParts);
                // if that key doesn't exist or has a changed IP
                if (!settings.contains("addrSeparateProxyTor") || strlIpPort.at(0) != value.toString()) {
                    // construct new value from new IP and current port
                    QString strNewValue = value.toString() + ":" + strlIpPort.at(1);
                    settings.setValue("addrSeparateProxyTor", strNewValue);
                    setRestartRequired(true);
                }
            }
            break;
        case ProxyPortTor:
            {
                // contains current IP at index 0 and current port at index 1
                QStringList strlIpPort = settings.value("addrSeparateProxyTor").toString().split(":", QString::SkipEmptyParts);
                // if that key doesn't exist or has a changed port
                if (!settings.contains("addrSeparateProxyTor") || strlIpPort.at(1) != value.toString()) {
                    // construct new value from current IP and new port
                    QString strNewValue = strlIpPort.at(0) + ":" + value.toString();
                    settings.setValue("addrSeparateProxyTor", strNewValue);
                    setRestartRequired(true);
                }
            }
            break;
        case ControlIPTor:
            {
                // contains current IP at index 0 and current port at index 1
                QStringList strlIpPort = settings.value("controlIpTor").toString().split(":", QString::SkipEmptyParts);
                // if that key doesn't exist or has a changed IP
                if (!settings.contains("controlIpTor") || strlIpPort.at(0) != value.toString()) {
                    // construct new value from new IP and current port
                    QString strNewValue = value.toString() + ":" + strlIpPort.at(1);
                    settings.setValue("controlIpTor", strNewValue);
                    setRestartRequired(true);
                }
            }
            break;
        case ControlPortTor:
            {
                // contains current IP at index 0 and current port at index 1
                QStringList strlIpPort = settings.value("controlIpTor").toString().split(":", QString::SkipEmptyParts);
                // if that key doesn't exist or has a changed port
                if (!settings.contains("controlIpTor") || strlIpPort.at(1) != value.toString()) {
                    // construct new value from current IP and new port
                    QString strNewValue = strlIpPort.at(0) + ":" + value.toString();
                    settings.setValue("controlIpTor", strNewValue);
                    setRestartRequired(true);
                }
            }
            break;
        case ControlPasswordTor:
            if (settings.value("controlPasswordTor") != value) {
                settings.setValue("controlPasswordTor", value);
                setRestartRequired(true);
            }
            break;


        // I2P proxy
        case ProxyUseI2P:
            if (settings.value("fUseProxyI2P") != value) {
                settings.setValue("fUseProxyI2P", value);
                setRestartRequired(true);
            }
            break;
        case IncomingI2P:
            if (settings.value("fIncomingI2P") != value) {
                settings.setValue("fIncomingI2P", value);
                setRestartRequired(true);
            }
            break;
        case ProxyIPI2P:
            {
                // contains current IP at index 0 and current port at index 1
                QStringList strlIpPort = settings.value("addrProxyI2P").toString().split(":", QString::SkipEmptyParts);
                // if that key doesn't exist or has a changed IP
                if (!settings.contains("addrProxyI2P") || strlIpPort.at(0) != value.toString()) {
                    // construct new value from new IP and current port
                    QString strNewValue = value.toString() + ":" + strlIpPort.at(1);
                    settings.setValue("addrProxyI2P", strNewValue);
                    setRestartRequired(true);
                }
            }
            break;
        case ProxyPortI2P:
            {
                // contains current IP at index 0 and current port at index 1
                QStringList strlIpPort = settings.value("addrProxyI2P").toString().split(":", QString::SkipEmptyParts);
                // if that key doesn't exist or has a changed port
                if (!settings.contains("addrProxyI2P") || strlIpPort.at(1) != value.toString()) {
                    // construct new value from current IP and new port
                    QString strNewValue = strlIpPort.at(0) + ":" + value.toString();
                    settings.setValue("addrProxyI2P", strNewValue);
                    setRestartRequired(true);
                }
            }
            break;

        // Disable ivp4 and/or ipv6
        case IPv4Disable:
            if (settings.value("fIPv4Disable") != value) {
                settings.setValue("fIPv4Disable", value);
                setRestartRequired(true);
            }
            break;

        case IPv6Disable:
            if (settings.value("fIPv6Disable") != value) {
                settings.setValue("fIPv6Disable", value);
                setRestartRequired(true);
            }
            break;

#ifdef ENABLE_WALLET
        case EnableDeleteTx:
            if (settings.value("fTxDeleteEnabled") != value) {
                settings.setValue("fTxDeleteEnabled", value);
                setRestartRequired(true);
            }
            break;
        case SaplingConsolidationEnabled:
            if (settings.value("fSaplingConsolidationEnabled") != value) {
                settings.setValue("fSaplingConsolidationEnabled", value);
                setRestartRequired(true);
            }
            break;
        case EnableReindex:
            if (settings.value("fEnableReindex") != value) {
                settings.setValue("fEnableReindex", value);
                setRestartRequired(true);
            }
            break;
        case EnableZSigning:
            if (settings.value("fEnableZSigning") != value) {
                settings.setValue("fEnableZSigning", value);
                setRestartRequired(true);
            }
            break;
        case EnableZSigning_Sign:
            if (settings.value("fEnableZSigning_ModeSign") != value) {
                settings.setValue("fEnableZSigning_ModeSign", value);
                setRestartRequired(true);
            }
            break;
        case EnableZSigning_Spend:
            if (settings.value("fEnableZSigning_ModeSpend") != value) {
                settings.setValue("fEnableZSigning_ModeSpend", value);
                setRestartRequired(true);
            }
            break;
        case EnableHexMemo:
            setHexMemo(value);
            break;
        case EnableBootstrap:
            if (settings.value("fEnableBootstrap") != value) {
                settings.setValue("fEnableBootstrap", value);
                setRestartRequired(true);
            }
            break;
        case ZapWalletTxes:
            if (settings.value("fZapWalletTxes") != value) {
                settings.setValue("fZapWalletTxes", value);
                setRestartRequired(true);
            }
            break;
#endif
        case DisplayUnit:
            setDisplayUnit(value);
            break;
        case Theme:
            if (strTheme != value.toString()) {
                strTheme = value.toString();
                settings.setValue("strTheme", strTheme);
            }
            break;
        case ThirdPartyTxUrls:
            if (strThirdPartyTxUrls != value.toString()) {
                strThirdPartyTxUrls = value.toString();
                settings.setValue("strThirdPartyTxUrls", strThirdPartyTxUrls);
                setRestartRequired(true);
            }
            break;
        case Language:
            if (settings.value("language") != value) {
                settings.setValue("language", value);
                setRestartRequired(true);
            }
            break;
        case DatabaseCache:
            if (settings.value("nDatabaseCache") != value) {
                settings.setValue("nDatabaseCache", value);
                setRestartRequired(true);
            }
            break;
        case ThreadsScriptVerif:
            if (settings.value("nThreadsScriptVerif") != value) {
                settings.setValue("nThreadsScriptVerif", value);
                setRestartRequired(true);
            }
            break;
        case Listen:
            if (settings.value("fListen") != value) {
                settings.setValue("fListen", value);
                setRestartRequired(true);
            }
            break;
        case EncryptedP2P:
            if (settings.value("fEncrypted") != value) {
                settings.setValue("fEncrypted", value);
                setRestartRequired(true);
            }
            break;

        default:
            break;
        }
    }

    Q_EMIT dataChanged(index, index);

    return successful;
}

/** Updates current unit in memory, settings and emits displayUnitChanged(newUnit) signal */
void OptionsModel::setDisplayUnit(const QVariant &value)
{
    if (!value.isNull())
    {
        QSettings settings;
        nDisplayUnit = value.toInt();
        settings.setValue("nDisplayUnit", nDisplayUnit);
        Q_EMIT displayUnitChanged(nDisplayUnit);
    }
}

/** Updates current unit in memory, settings and emits displayUnitChanged(newUnit) signal */
void OptionsModel::setHexMemo(const QVariant &value)
{
    if (!value.isNull())
    {
        QSettings settings;
        fEnableHexMemo = value.toBool();
        settings.setValue("fEnableHexMemo", fEnableHexMemo);
        Q_EMIT optionHexMemo(fEnableHexMemo);
    }
}

// #ifdef ENABLE_BIP70
// bool OptionsModel::getProxySettings(QNetworkProxy& proxy) const
// {
//     // Directly query current base proxy, because
//     // GUI settings can be overridden with -proxy.
//     proxyType curProxy;
//     if (GetProxy(NET_IPV4, curProxy)) {
//         proxy.setType(QNetworkProxy::Socks5Proxy);
//         proxy.setHostName(QString::fromStdString(curProxy.proxy.ToStringIP()));
//         proxy.setPort(curProxy.proxy.GetPort());
//
//         return true;
//     }
//     else
//         proxy.setType(QNetworkProxy::NoProxy);
//
//     return false;
// }
// #endif

//Note: setRestartRequired only called while the settings are
//      evaluated & updated in ::setData().
//      This is too late to provide a user prompt to cancel/
//      prevent the update.
void OptionsModel::setRestartRequired(bool fRequired)
{
    QSettings settings;
    settings.setValue("fRestartRequired", fRequired);
    return;
}

//Note: Calling isRestartRequired() before applying the
//      changes will not give a correct result while
//      editing the options.
bool OptionsModel::isRestartRequired() const
{
    QSettings settings;
    return settings.value("fRestartRequired", false).toBool();
}

void OptionsModel::checkAndMigrate()
{
    // Migration of default values
    // Check if the QSettings container was already loaded with this client version
    QSettings settings;
    static const char strSettingsVersionKey[] = "nSettingsVersion";
    int settingsVersion = settings.contains(strSettingsVersionKey) ? settings.value(strSettingsVersionKey).toInt() : 0;
    if (settingsVersion < CLIENT_VERSION)
    {
        // -dbcache was bumped from 100 to 300 in 0.13
        // see https://github.com/komodo/komodo/pull/8273
        // force people to upgrade to the new value if they are using 100MB
        if (settingsVersion < 130000 && settings.contains("nDatabaseCache") && settings.value("nDatabaseCache").toLongLong() == 100)
            settings.setValue("nDatabaseCache", (qint64)nDefaultDbCache);

        settings.setValue(strSettingsVersionKey, CLIENT_VERSION);
    }
}
