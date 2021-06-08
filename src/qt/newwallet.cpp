// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "newwallet.h"

#include "ui_newwallet.h"

NewWallet::NewWallet(const NetworkStyle *networkStyle) :
    ui(new Ui::CreateWalletForm)
{
    ui->setupUi(this);
}

NewWallet::~NewWallet()
{

}
