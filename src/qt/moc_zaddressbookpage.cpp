/****************************************************************************
** Meta object code from reading C++ file 'zaddressbookpage.h'
**
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "qt/zaddressbookpage.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'zaddressbookpage.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.9.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_ZAddressBookPage_t {
    QByteArrayData data[20];
    char stringdata0[288];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_ZAddressBookPage_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_ZAddressBookPage_t qt_meta_stringdata_ZAddressBookPage = {
    {
QT_MOC_LITERAL(0, 0, 16), // "ZAddressBookPage"
QT_MOC_LITERAL(1, 17, 9), // "sendCoins"
QT_MOC_LITERAL(2, 27, 0), // ""
QT_MOC_LITERAL(3, 28, 4), // "addr"
QT_MOC_LITERAL(4, 33, 4), // "done"
QT_MOC_LITERAL(5, 38, 6), // "retval"
QT_MOC_LITERAL(6, 45, 24), // "on_deleteAddress_clicked"
QT_MOC_LITERAL(7, 70, 21), // "on_newAddress_clicked"
QT_MOC_LITERAL(8, 92, 23), // "onCopyZSendManyToAction"
QT_MOC_LITERAL(9, 116, 25), // "onCopyZSendManyFromAction"
QT_MOC_LITERAL(10, 142, 22), // "on_copyAddress_clicked"
QT_MOC_LITERAL(11, 165, 17), // "onCopyLabelAction"
QT_MOC_LITERAL(12, 183, 12), // "onEditAction"
QT_MOC_LITERAL(13, 196, 23), // "on_exportButton_clicked"
QT_MOC_LITERAL(14, 220, 16), // "selectionChanged"
QT_MOC_LITERAL(15, 237, 14), // "contextualMenu"
QT_MOC_LITERAL(16, 252, 5), // "point"
QT_MOC_LITERAL(17, 258, 16), // "selectNewAddress"
QT_MOC_LITERAL(18, 275, 6), // "parent"
QT_MOC_LITERAL(19, 282, 5) // "begin"

    },
    "ZAddressBookPage\0sendCoins\0\0addr\0done\0"
    "retval\0on_deleteAddress_clicked\0"
    "on_newAddress_clicked\0onCopyZSendManyToAction\0"
    "onCopyZSendManyFromAction\0"
    "on_copyAddress_clicked\0onCopyLabelAction\0"
    "onEditAction\0on_exportButton_clicked\0"
    "selectionChanged\0contextualMenu\0point\0"
    "selectNewAddress\0parent\0begin"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_ZAddressBookPage[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
      13,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   79,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       4,    1,   82,    2, 0x0a /* Public */,
       6,    0,   85,    2, 0x08 /* Private */,
       7,    0,   86,    2, 0x08 /* Private */,
       8,    0,   87,    2, 0x08 /* Private */,
       9,    0,   88,    2, 0x08 /* Private */,
      10,    0,   89,    2, 0x08 /* Private */,
      11,    0,   90,    2, 0x08 /* Private */,
      12,    0,   91,    2, 0x08 /* Private */,
      13,    0,   92,    2, 0x08 /* Private */,
      14,    0,   93,    2, 0x08 /* Private */,
      15,    1,   94,    2, 0x08 /* Private */,
      17,    3,   97,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    5,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QPoint,   16,
    QMetaType::Void, QMetaType::QModelIndex, QMetaType::Int, QMetaType::Int,   18,   19,    2,

       0        // eod
};

void ZAddressBookPage::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        ZAddressBookPage *_t = static_cast<ZAddressBookPage *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->sendCoins((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 1: _t->done((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 2: _t->on_deleteAddress_clicked(); break;
        case 3: _t->on_newAddress_clicked(); break;
        case 4: _t->onCopyZSendManyToAction(); break;
        case 5: _t->onCopyZSendManyFromAction(); break;
        case 6: _t->on_copyAddress_clicked(); break;
        case 7: _t->onCopyLabelAction(); break;
        case 8: _t->onEditAction(); break;
        case 9: _t->on_exportButton_clicked(); break;
        case 10: _t->selectionChanged(); break;
        case 11: _t->contextualMenu((*reinterpret_cast< const QPoint(*)>(_a[1]))); break;
        case 12: _t->selectNewAddress((*reinterpret_cast< const QModelIndex(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            typedef void (ZAddressBookPage::*_t)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ZAddressBookPage::sendCoins)) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject ZAddressBookPage::staticMetaObject = {
    { &QDialog::staticMetaObject, qt_meta_stringdata_ZAddressBookPage.data,
      qt_meta_data_ZAddressBookPage,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *ZAddressBookPage::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ZAddressBookPage::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZAddressBookPage.stringdata0))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int ZAddressBookPage::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 13)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 13)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 13;
    }
    return _id;
}

// SIGNAL 0
void ZAddressBookPage::sendCoins(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
