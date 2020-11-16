/****************************************************************************
** Meta object code from reading C++ file 'sendcoinsentry.h'
**
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "qt/sendcoinsentry.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'sendcoinsentry.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.9.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SendCoinsEntry_t {
    QByteArrayData data[18];
    char stringdata0[323];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_SendCoinsEntry_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_SendCoinsEntry_t qt_meta_stringdata_SendCoinsEntry = {
    {
QT_MOC_LITERAL(0, 0, 14), // "SendCoinsEntry"
QT_MOC_LITERAL(1, 15, 11), // "removeEntry"
QT_MOC_LITERAL(2, 27, 0), // ""
QT_MOC_LITERAL(3, 28, 15), // "SendCoinsEntry*"
QT_MOC_LITERAL(4, 44, 5), // "entry"
QT_MOC_LITERAL(5, 50, 19), // "useAvailableBalance"
QT_MOC_LITERAL(6, 70, 16), // "payAmountChanged"
QT_MOC_LITERAL(7, 87, 28), // "subtractFeeFromAmountChanged"
QT_MOC_LITERAL(8, 116, 5), // "clear"
QT_MOC_LITERAL(9, 122, 26), // "checkSubtractFeeFromAmount"
QT_MOC_LITERAL(10, 149, 33), // "hideCheckboxSubtractFeeFromAm..."
QT_MOC_LITERAL(11, 183, 13), // "deleteClicked"
QT_MOC_LITERAL(12, 197, 26), // "useAvailableBalanceClicked"
QT_MOC_LITERAL(13, 224, 20), // "on_payTo_textChanged"
QT_MOC_LITERAL(14, 245, 7), // "address"
QT_MOC_LITERAL(15, 253, 28), // "on_addressBookButton_clicked"
QT_MOC_LITERAL(16, 282, 22), // "on_pasteButton_clicked"
QT_MOC_LITERAL(17, 305, 17) // "updateDisplayUnit"

    },
    "SendCoinsEntry\0removeEntry\0\0SendCoinsEntry*\0"
    "entry\0useAvailableBalance\0payAmountChanged\0"
    "subtractFeeFromAmountChanged\0clear\0"
    "checkSubtractFeeFromAmount\0"
    "hideCheckboxSubtractFeeFromAmount\0"
    "deleteClicked\0useAvailableBalanceClicked\0"
    "on_payTo_textChanged\0address\0"
    "on_addressBookButton_clicked\0"
    "on_pasteButton_clicked\0updateDisplayUnit"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SendCoinsEntry[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
      13,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   79,    2, 0x06 /* Public */,
       5,    1,   82,    2, 0x06 /* Public */,
       6,    0,   85,    2, 0x06 /* Public */,
       7,    0,   86,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       8,    0,   87,    2, 0x0a /* Public */,
       9,    0,   88,    2, 0x0a /* Public */,
      10,    0,   89,    2, 0x0a /* Public */,
      11,    0,   90,    2, 0x08 /* Private */,
      12,    0,   91,    2, 0x08 /* Private */,
      13,    1,   92,    2, 0x08 /* Private */,
      15,    0,   95,    2, 0x08 /* Private */,
      16,    0,   96,    2, 0x08 /* Private */,
      17,    0,   97,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   14,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void SendCoinsEntry::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        SendCoinsEntry *_t = static_cast<SendCoinsEntry *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->removeEntry((*reinterpret_cast< SendCoinsEntry*(*)>(_a[1]))); break;
        case 1: _t->useAvailableBalance((*reinterpret_cast< SendCoinsEntry*(*)>(_a[1]))); break;
        case 2: _t->payAmountChanged(); break;
        case 3: _t->subtractFeeFromAmountChanged(); break;
        case 4: _t->clear(); break;
        case 5: _t->checkSubtractFeeFromAmount(); break;
        case 6: _t->hideCheckboxSubtractFeeFromAmount(); break;
        case 7: _t->deleteClicked(); break;
        case 8: _t->useAvailableBalanceClicked(); break;
        case 9: _t->on_payTo_textChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 10: _t->on_addressBookButton_clicked(); break;
        case 11: _t->on_pasteButton_clicked(); break;
        case 12: _t->updateDisplayUnit(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 0:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< SendCoinsEntry* >(); break;
            }
            break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< SendCoinsEntry* >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            typedef void (SendCoinsEntry::*_t)(SendCoinsEntry * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SendCoinsEntry::removeEntry)) {
                *result = 0;
                return;
            }
        }
        {
            typedef void (SendCoinsEntry::*_t)(SendCoinsEntry * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SendCoinsEntry::useAvailableBalance)) {
                *result = 1;
                return;
            }
        }
        {
            typedef void (SendCoinsEntry::*_t)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SendCoinsEntry::payAmountChanged)) {
                *result = 2;
                return;
            }
        }
        {
            typedef void (SendCoinsEntry::*_t)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SendCoinsEntry::subtractFeeFromAmountChanged)) {
                *result = 3;
                return;
            }
        }
    }
}

const QMetaObject SendCoinsEntry::staticMetaObject = {
    { &QStackedWidget::staticMetaObject, qt_meta_stringdata_SendCoinsEntry.data,
      qt_meta_data_SendCoinsEntry,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *SendCoinsEntry::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SendCoinsEntry::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SendCoinsEntry.stringdata0))
        return static_cast<void*>(this);
    return QStackedWidget::qt_metacast(_clname);
}

int SendCoinsEntry::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QStackedWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 13)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 13)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    }
    return _id;
}

// SIGNAL 0
void SendCoinsEntry::removeEntry(SendCoinsEntry * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void SendCoinsEntry::useAvailableBalance(SendCoinsEntry * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void SendCoinsEntry::payAmountChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void SendCoinsEntry::subtractFeeFromAmountChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
