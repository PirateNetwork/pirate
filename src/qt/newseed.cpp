// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "newseed.h"

#include "ui_newseed.h"

NewSeed::NewSeed(const NetworkStyle *networkStyle) :
    ui(new Ui::NewSeedForm)
{
    ui->setupUi(this);
}

NewSeed::~NewSeed()
{

}
