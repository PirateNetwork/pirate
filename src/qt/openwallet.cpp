// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "openwallet.h"

#include "ui_openwallet.h"

OpenWallet::OpenWallet(const NetworkStyle *networkStyle) :
    ui(new Ui::OpenWalletForm)
{
    ui->setupUi(this);
}

OpenWallet::~OpenWallet()
{

}
