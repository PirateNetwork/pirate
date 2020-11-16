/****************************************************************************
** Meta object code from reading C++ file 'addressbookpage.h'
**
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "qt/addressbookpage.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'addressbookpage.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.9.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_AddressBookPage_t {
    QByteArrayData data[18];
    char stringdata0[237];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_AddressBookPage_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_AddressBookPage_t qt_meta_stringdata_AddressBookPage = {
    {
QT_MOC_LITERAL(0, 0, 15), // "AddressBookPage"
QT_MOC_LITERAL(1, 16, 9), // "sendCoins"
QT_MOC_LITERAL(2, 26, 0), // ""
QT_MOC_LITERAL(3, 27, 4), // "addr"
QT_MOC_LITERAL(4, 32, 4), // "done"
QT_MOC_LITERAL(5, 37, 6), // "retval"
QT_MOC_LITERAL(6, 44, 24), // "on_deleteAddress_clicked"
QT_MOC_LITERAL(7, 69, 21), // "on_newAddress_clicked"
QT_MOC_LITERAL(8, 91, 22), // "on_copyAddress_clicked"
QT_MOC_LITERAL(9, 114, 17), // "onCopyLabelAction"
QT_MOC_LITERAL(10, 132, 12), // "onEditAction"
QT_MOC_LITERAL(11, 145, 23), // "on_exportButton_clicked"
QT_MOC_LITERAL(12, 169, 16), // "selectionChanged"
QT_MOC_LITERAL(13, 186, 14), // "contextualMenu"
QT_MOC_LITERAL(14, 201, 5), // "point"
QT_MOC_LITERAL(15, 207, 16), // "selectNewAddress"
QT_MOC_LITERAL(16, 224, 6), // "parent"
QT_MOC_LITERAL(17, 231, 5) // "begin"

    },
    "AddressBookPage\0sendCoins\0\0addr\0done\0"
    "retval\0on_deleteAddress_clicked\0"
    "on_newAddress_clicked\0on_copyAddress_clicked\0"
    "onCopyLabelAction\0onEditAction\0"
    "on_exportButton_clicked\0selectionChanged\0"
    "contextualMenu\0point\0selectNewAddress\0"
    "parent\0begin"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_AddressBookPage[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
      11,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   69,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       4,    1,   72,    2, 0x0a /* Public */,
       6,    0,   75,    2, 0x08 /* Private */,
       7,    0,   76,    2, 0x08 /* Private */,
       8,    0,   77,    2, 0x08 /* Private */,
       9,    0,   78,    2, 0x08 /* Private */,
      10,    0,   79,    2, 0x08 /* Private */,
      11,    0,   80,    2, 0x08 /* Private */,
      12,    0,   81,    2, 0x08 /* Private */,
      13,    1,   82,    2, 0x08 /* Private */,
      15,    3,   85,    2, 0x08 /* Private */,

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
    QMetaType::Void, QMetaType::QPoint,   14,
    QMetaType::Void, QMetaType::QModelIndex, QMetaType::Int, QMetaType::Int,   16,   17,    2,

       0        // eod
};

void AddressBookPage::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        AddressBookPage *_t = static_cast<AddressBookPage *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->sendCoins((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 1: _t->done((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 2: _t->on_deleteAddress_clicked(); break;
        case 3: _t->on_newAddress_clicked(); break;
        case 4: _t->on_copyAddress_clicked(); break;
        case 5: _t->onCopyLabelAction(); break;
        case 6: _t->onEditAction(); break;
        case 7: _t->on_exportButton_clicked(); break;
        case 8: _t->selectionChanged(); break;
        case 9: _t->contextualMenu((*reinterpret_cast< const QPoint(*)>(_a[1]))); break;
        case 10: _t->selectNewAddress((*reinterpret_cast< const QModelIndex(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            typedef void (AddressBookPage::*_t)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&AddressBookPage::sendCoins)) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject AddressBookPage::staticMetaObject = {
    { &QDialog::staticMetaObject, qt_meta_stringdata_AddressBookPage.data,
      qt_meta_data_AddressBookPage,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *AddressBookPage::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AddressBookPage::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_AddressBookPage.stringdata0))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int AddressBookPage::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 11;
    }
    return _id;
}

// SIGNAL 0
void AddressBookPage::sendCoins(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
