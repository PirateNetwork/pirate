/****************************************************************************
** Meta object code from reading C++ file 'walletview.h'
**
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "qt/walletview.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'walletview.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.9.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_WalletView_t {
    QByteArrayData data[41];
    char stringdata0[573];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_WalletView_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_WalletView_t qt_meta_stringdata_WalletView = {
    {
QT_MOC_LITERAL(0, 0, 10), // "WalletView"
QT_MOC_LITERAL(1, 11, 21), // "showNormalIfMinimized"
QT_MOC_LITERAL(2, 33, 0), // ""
QT_MOC_LITERAL(3, 34, 7), // "message"
QT_MOC_LITERAL(4, 42, 5), // "title"
QT_MOC_LITERAL(5, 48, 5), // "style"
QT_MOC_LITERAL(6, 54, 23), // "encryptionStatusChanged"
QT_MOC_LITERAL(7, 78, 6), // "status"
QT_MOC_LITERAL(8, 85, 22), // "hdEnabledStatusChanged"
QT_MOC_LITERAL(9, 108, 9), // "hdEnabled"
QT_MOC_LITERAL(10, 118, 19), // "incomingTransaction"
QT_MOC_LITERAL(11, 138, 4), // "date"
QT_MOC_LITERAL(12, 143, 4), // "unit"
QT_MOC_LITERAL(13, 148, 7), // "CAmount"
QT_MOC_LITERAL(14, 156, 6), // "amount"
QT_MOC_LITERAL(15, 163, 4), // "type"
QT_MOC_LITERAL(16, 168, 7), // "address"
QT_MOC_LITERAL(17, 176, 5), // "label"
QT_MOC_LITERAL(18, 182, 23), // "outOfSyncWarningClicked"
QT_MOC_LITERAL(19, 206, 16), // "gotoOverviewPage"
QT_MOC_LITERAL(20, 223, 15), // "gotoHistoryPage"
QT_MOC_LITERAL(21, 239, 20), // "gotoReceiveCoinsPage"
QT_MOC_LITERAL(22, 260, 17), // "gotoSendCoinsPage"
QT_MOC_LITERAL(23, 278, 4), // "addr"
QT_MOC_LITERAL(24, 283, 18), // "gotoZSendCoinsPage"
QT_MOC_LITERAL(25, 302, 18), // "gotoSignMessageTab"
QT_MOC_LITERAL(26, 321, 20), // "gotoVerifyMessageTab"
QT_MOC_LITERAL(27, 342, 21), // "processNewTransaction"
QT_MOC_LITERAL(28, 364, 6), // "parent"
QT_MOC_LITERAL(29, 371, 5), // "start"
QT_MOC_LITERAL(30, 377, 13), // "encryptWallet"
QT_MOC_LITERAL(31, 391, 12), // "backupWallet"
QT_MOC_LITERAL(32, 404, 16), // "changePassphrase"
QT_MOC_LITERAL(33, 421, 12), // "unlockWallet"
QT_MOC_LITERAL(34, 434, 20), // "usedSendingAddresses"
QT_MOC_LITERAL(35, 455, 22), // "usedReceivingAddresses"
QT_MOC_LITERAL(36, 478, 23), // "usedReceivingZAddresses"
QT_MOC_LITERAL(37, 502, 22), // "updateEncryptionStatus"
QT_MOC_LITERAL(38, 525, 12), // "showProgress"
QT_MOC_LITERAL(39, 538, 9), // "nProgress"
QT_MOC_LITERAL(40, 548, 24) // "requestedSyncWarningInfo"

    },
    "WalletView\0showNormalIfMinimized\0\0"
    "message\0title\0style\0encryptionStatusChanged\0"
    "status\0hdEnabledStatusChanged\0hdEnabled\0"
    "incomingTransaction\0date\0unit\0CAmount\0"
    "amount\0type\0address\0label\0"
    "outOfSyncWarningClicked\0gotoOverviewPage\0"
    "gotoHistoryPage\0gotoReceiveCoinsPage\0"
    "gotoSendCoinsPage\0addr\0gotoZSendCoinsPage\0"
    "gotoSignMessageTab\0gotoVerifyMessageTab\0"
    "processNewTransaction\0parent\0start\0"
    "encryptWallet\0backupWallet\0changePassphrase\0"
    "unlockWallet\0usedSendingAddresses\0"
    "usedReceivingAddresses\0usedReceivingZAddresses\0"
    "updateEncryptionStatus\0showProgress\0"
    "nProgress\0requestedSyncWarningInfo"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_WalletView[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
      28,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       6,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,  154,    2, 0x06 /* Public */,
       3,    3,  155,    2, 0x06 /* Public */,
       6,    1,  162,    2, 0x06 /* Public */,
       8,    1,  165,    2, 0x06 /* Public */,
      10,    6,  168,    2, 0x06 /* Public */,
      18,    0,  181,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      19,    0,  182,    2, 0x0a /* Public */,
      20,    0,  183,    2, 0x0a /* Public */,
      21,    0,  184,    2, 0x0a /* Public */,
      22,    1,  185,    2, 0x0a /* Public */,
      22,    0,  188,    2, 0x2a /* Public | MethodCloned */,
      24,    1,  189,    2, 0x0a /* Public */,
      24,    0,  192,    2, 0x2a /* Public | MethodCloned */,
      25,    1,  193,    2, 0x0a /* Public */,
      25,    0,  196,    2, 0x2a /* Public | MethodCloned */,
      26,    1,  197,    2, 0x0a /* Public */,
      26,    0,  200,    2, 0x2a /* Public | MethodCloned */,
      27,    3,  201,    2, 0x0a /* Public */,
      30,    1,  208,    2, 0x0a /* Public */,
      31,    0,  211,    2, 0x0a /* Public */,
      32,    0,  212,    2, 0x0a /* Public */,
      33,    0,  213,    2, 0x0a /* Public */,
      34,    0,  214,    2, 0x0a /* Public */,
      35,    0,  215,    2, 0x0a /* Public */,
      36,    0,  216,    2, 0x0a /* Public */,
      37,    0,  217,    2, 0x0a /* Public */,
      38,    2,  218,    2, 0x0a /* Public */,
      40,    0,  223,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::QString, QMetaType::UInt,    4,    3,    5,
    QMetaType::Void, QMetaType::Int,    7,
    QMetaType::Void, QMetaType::Int,    9,
    QMetaType::Void, QMetaType::QString, QMetaType::Int, 0x80000000 | 13, QMetaType::QString, QMetaType::QString, QMetaType::QString,   11,   12,   14,   15,   16,   17,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   23,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   23,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   23,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   23,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QModelIndex, QMetaType::Int, QMetaType::Int,   28,   29,    2,
    QMetaType::Void, QMetaType::Bool,    7,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::Int,    4,   39,
    QMetaType::Void,

       0        // eod
};

void WalletView::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        WalletView *_t = static_cast<WalletView *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->showNormalIfMinimized(); break;
        case 1: _t->message((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< uint(*)>(_a[3]))); break;
        case 2: _t->encryptionStatusChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 3: _t->hdEnabledStatusChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 4: _t->incomingTransaction((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< const CAmount(*)>(_a[3])),(*reinterpret_cast< const QString(*)>(_a[4])),(*reinterpret_cast< const QString(*)>(_a[5])),(*reinterpret_cast< const QString(*)>(_a[6]))); break;
        case 5: _t->outOfSyncWarningClicked(); break;
        case 6: _t->gotoOverviewPage(); break;
        case 7: _t->gotoHistoryPage(); break;
        case 8: _t->gotoReceiveCoinsPage(); break;
        case 9: _t->gotoSendCoinsPage((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 10: _t->gotoSendCoinsPage(); break;
        case 11: _t->gotoZSendCoinsPage((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 12: _t->gotoZSendCoinsPage(); break;
        case 13: _t->gotoSignMessageTab((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 14: _t->gotoSignMessageTab(); break;
        case 15: _t->gotoVerifyMessageTab((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 16: _t->gotoVerifyMessageTab(); break;
        case 17: _t->processNewTransaction((*reinterpret_cast< const QModelIndex(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3]))); break;
        case 18: _t->encryptWallet((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 19: _t->backupWallet(); break;
        case 20: _t->changePassphrase(); break;
        case 21: _t->unlockWallet(); break;
        case 22: _t->usedSendingAddresses(); break;
        case 23: _t->usedReceivingAddresses(); break;
        case 24: _t->usedReceivingZAddresses(); break;
        case 25: _t->updateEncryptionStatus(); break;
        case 26: _t->showProgress((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 27: _t->requestedSyncWarningInfo(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            typedef void (WalletView::*_t)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WalletView::showNormalIfMinimized)) {
                *result = 0;
                return;
            }
        }
        {
            typedef void (WalletView::*_t)(const QString & , const QString & , unsigned int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WalletView::message)) {
                *result = 1;
                return;
            }
        }
        {
            typedef void (WalletView::*_t)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WalletView::encryptionStatusChanged)) {
                *result = 2;
                return;
            }
        }
        {
            typedef void (WalletView::*_t)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WalletView::hdEnabledStatusChanged)) {
                *result = 3;
                return;
            }
        }
        {
            typedef void (WalletView::*_t)(const QString & , int , const CAmount & , const QString & , const QString & , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WalletView::incomingTransaction)) {
                *result = 4;
                return;
            }
        }
        {
            typedef void (WalletView::*_t)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WalletView::outOfSyncWarningClicked)) {
                *result = 5;
                return;
            }
        }
    }
}

const QMetaObject WalletView::staticMetaObject = {
    { &QStackedWidget::staticMetaObject, qt_meta_stringdata_WalletView.data,
      qt_meta_data_WalletView,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *WalletView::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *WalletView::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_WalletView.stringdata0))
        return static_cast<void*>(this);
    return QStackedWidget::qt_metacast(_clname);
}

int WalletView::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QStackedWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 28)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 28;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 28)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 28;
    }
    return _id;
}

// SIGNAL 0
void WalletView::showNormalIfMinimized()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void WalletView::message(const QString & _t1, const QString & _t2, unsigned int _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void WalletView::encryptionStatusChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void WalletView::hdEnabledStatusChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void WalletView::incomingTransaction(const QString & _t1, int _t2, const CAmount & _t3, const QString & _t4, const QString & _t5, const QString & _t6)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)), const_cast<void*>(reinterpret_cast<const void*>(&_t4)), const_cast<void*>(reinterpret_cast<const void*>(&_t5)), const_cast<void*>(reinterpret_cast<const void*>(&_t6)) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void WalletView::outOfSyncWarningClicked()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
