/****************************************************************************
** Meta object code from reading C++ file 'walletframe.h'
**
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "qt/walletframe.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'walletframe.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.9.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_WalletFrame_t {
    QByteArrayData data[20];
    char stringdata0[330];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_WalletFrame_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_WalletFrame_t qt_meta_stringdata_WalletFrame = {
    {
QT_MOC_LITERAL(0, 0, 11), // "WalletFrame"
QT_MOC_LITERAL(1, 12, 24), // "requestedSyncWarningInfo"
QT_MOC_LITERAL(2, 37, 0), // ""
QT_MOC_LITERAL(3, 38, 16), // "gotoOverviewPage"
QT_MOC_LITERAL(4, 55, 15), // "gotoHistoryPage"
QT_MOC_LITERAL(5, 71, 20), // "gotoReceiveCoinsPage"
QT_MOC_LITERAL(6, 92, 17), // "gotoSendCoinsPage"
QT_MOC_LITERAL(7, 110, 4), // "addr"
QT_MOC_LITERAL(8, 115, 18), // "gotoZSendCoinsPage"
QT_MOC_LITERAL(9, 134, 18), // "gotoSignMessageTab"
QT_MOC_LITERAL(10, 153, 20), // "gotoVerifyMessageTab"
QT_MOC_LITERAL(11, 174, 13), // "encryptWallet"
QT_MOC_LITERAL(12, 188, 6), // "status"
QT_MOC_LITERAL(13, 195, 12), // "backupWallet"
QT_MOC_LITERAL(14, 208, 16), // "changePassphrase"
QT_MOC_LITERAL(15, 225, 12), // "unlockWallet"
QT_MOC_LITERAL(16, 238, 20), // "usedSendingAddresses"
QT_MOC_LITERAL(17, 259, 22), // "usedReceivingAddresses"
QT_MOC_LITERAL(18, 282, 23), // "usedReceivingZAddresses"
QT_MOC_LITERAL(19, 306, 23) // "outOfSyncWarningClicked"

    },
    "WalletFrame\0requestedSyncWarningInfo\0"
    "\0gotoOverviewPage\0gotoHistoryPage\0"
    "gotoReceiveCoinsPage\0gotoSendCoinsPage\0"
    "addr\0gotoZSendCoinsPage\0gotoSignMessageTab\0"
    "gotoVerifyMessageTab\0encryptWallet\0"
    "status\0backupWallet\0changePassphrase\0"
    "unlockWallet\0usedSendingAddresses\0"
    "usedReceivingAddresses\0usedReceivingZAddresses\0"
    "outOfSyncWarningClicked"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_WalletFrame[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
      20,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,  114,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       3,    0,  115,    2, 0x0a /* Public */,
       4,    0,  116,    2, 0x0a /* Public */,
       5,    0,  117,    2, 0x0a /* Public */,
       6,    1,  118,    2, 0x0a /* Public */,
       6,    0,  121,    2, 0x2a /* Public | MethodCloned */,
       8,    1,  122,    2, 0x0a /* Public */,
       8,    0,  125,    2, 0x2a /* Public | MethodCloned */,
       9,    1,  126,    2, 0x0a /* Public */,
       9,    0,  129,    2, 0x2a /* Public | MethodCloned */,
      10,    1,  130,    2, 0x0a /* Public */,
      10,    0,  133,    2, 0x2a /* Public | MethodCloned */,
      11,    1,  134,    2, 0x0a /* Public */,
      13,    0,  137,    2, 0x0a /* Public */,
      14,    0,  138,    2, 0x0a /* Public */,
      15,    0,  139,    2, 0x0a /* Public */,
      16,    0,  140,    2, 0x0a /* Public */,
      17,    0,  141,    2, 0x0a /* Public */,
      18,    0,  142,    2, 0x0a /* Public */,
      19,    0,  143,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    7,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    7,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    7,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    7,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   12,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void WalletFrame::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        WalletFrame *_t = static_cast<WalletFrame *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->requestedSyncWarningInfo(); break;
        case 1: _t->gotoOverviewPage(); break;
        case 2: _t->gotoHistoryPage(); break;
        case 3: _t->gotoReceiveCoinsPage(); break;
        case 4: _t->gotoSendCoinsPage((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 5: _t->gotoSendCoinsPage(); break;
        case 6: _t->gotoZSendCoinsPage((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 7: _t->gotoZSendCoinsPage(); break;
        case 8: _t->gotoSignMessageTab((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 9: _t->gotoSignMessageTab(); break;
        case 10: _t->gotoVerifyMessageTab((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 11: _t->gotoVerifyMessageTab(); break;
        case 12: _t->encryptWallet((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 13: _t->backupWallet(); break;
        case 14: _t->changePassphrase(); break;
        case 15: _t->unlockWallet(); break;
        case 16: _t->usedSendingAddresses(); break;
        case 17: _t->usedReceivingAddresses(); break;
        case 18: _t->usedReceivingZAddresses(); break;
        case 19: _t->outOfSyncWarningClicked(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            typedef void (WalletFrame::*_t)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WalletFrame::requestedSyncWarningInfo)) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject WalletFrame::staticMetaObject = {
    { &QFrame::staticMetaObject, qt_meta_stringdata_WalletFrame.data,
      qt_meta_data_WalletFrame,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *WalletFrame::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *WalletFrame::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_WalletFrame.stringdata0))
        return static_cast<void*>(this);
    return QFrame::qt_metacast(_clname);
}

int WalletFrame::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QFrame::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 20)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 20;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 20)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 20;
    }
    return _id;
}

// SIGNAL 0
void WalletFrame::requestedSyncWarningInfo()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
