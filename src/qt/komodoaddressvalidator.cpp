// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "komodoaddressvalidator.h"

#include "base58.h"
#include "key_io.h"

/* Base58 characters are:
     "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

  This is:
  - All numbers except for '0'
  - All upper-case letters except for 'I' and 'O'
  - All lower-case letters except for 'l'
*/

KomodoAddressEntryValidator::KomodoAddressEntryValidator(QObject *parent, bool allowZAddresses) :
    QValidator(parent),
    _allowZAddresses(allowZAddresses)
{
}

QValidator::State KomodoAddressEntryValidator::validate(QString &input, int &pos) const
{
    Q_UNUSED(pos);

    // Empty address is "intermediate" input
    if (input.isEmpty())
        return QValidator::Intermediate;

    // Correction
    for (int idx = 0; idx < input.size();)
    {
        bool removeChar = false;
        QChar ch = input.at(idx);
        // Corrections made are very conservative on purpose, to avoid
        // users unexpectedly getting away with typos that would normally
        // be detected, and thus sending to the wrong address.
        switch(ch.unicode())
        {
        // Qt categorizes these as "Other_Format" not "Separator_Space"
        case 0x200B: // ZERO WIDTH SPACE
        case 0xFEFF: // ZERO WIDTH NO-BREAK SPACE
            removeChar = true;
            break;
        default:
            break;
        }

        // Remove whitespace
        if (ch.isSpace())
            removeChar = true;

        // To next character
        if (removeChar)
            input.remove(idx, 1);
        else
            ++idx;
    }

    // Validation
    QValidator::State state = QValidator::Acceptable;
    for (int idx = 0; idx < input.size(); ++idx)
    {
        int ch = input.at(idx).unicode();

        if (((ch >= '0' && ch<='9') ||
            (ch >= 'a' && ch<='z') ||
            (ch >= 'A' && ch<='Z')) &&
            ch != 'I' && ch != 'O') // Characters invalid in both Base58 and Bech32
        {
            // Alphanumeric and not a 'forbidden' character
        }
        else
        {
            state = QValidator::Invalid;
        }
    }

    return state;
}

KomodoAddressCheckValidator::KomodoAddressCheckValidator(QObject *parent, bool allowZAddresses) :
    QValidator(parent),
    _allowZAddresses(allowZAddresses)
{
}

QValidator::State KomodoAddressCheckValidator::validate(QString &input, int &pos) const
{
    Q_UNUSED(pos);
    // Validate the passed Komodo address
    if (_allowZAddresses) {

        // by default we assume SAPLING_BRANCH_ID to allow both type of z-addresses SPROUT and SAPLING,
        // bcz we can't get CurrentEpochBranchId(chainActive.Height(), Params().GetConsensus()) here.
        // this affects only validator in QValidateLineEdit, so, nothing other hurts.

        if (IsValidPaymentAddressString(input.toStdString(), SAPLING_BRANCH_ID))
            return QValidator::Acceptable;
    }

    if (IsValidDestinationString(input.toStdString())) {
        return QValidator::Acceptable;
    }

    return QValidator::Invalid;
}
