// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "restoreseed.h"

#include "ui_restoreseed.h"

RestoreSeed::RestoreSeed(const NetworkStyle *networkStyle) :
    ui(new Ui::RestoreSeedForm)
{
    ui->setupUi(this);
}

RestoreSeed::~RestoreSeed()
{

}
