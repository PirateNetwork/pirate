/****************************************************************************
** Meta object code from reading C++ file 'paymentserver.h'
**
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "qt/paymentserver.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'paymentserver.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.9.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_PaymentServer_t {
    QByteArrayData data[11];
    char stringdata0[123];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_PaymentServer_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_PaymentServer_t qt_meta_stringdata_PaymentServer = {
    {
QT_MOC_LITERAL(0, 0, 13), // "PaymentServer"
QT_MOC_LITERAL(1, 14, 22), // "receivedPaymentRequest"
QT_MOC_LITERAL(2, 37, 0), // ""
QT_MOC_LITERAL(3, 38, 18), // "SendCoinsRecipient"
QT_MOC_LITERAL(4, 57, 7), // "message"
QT_MOC_LITERAL(5, 65, 5), // "title"
QT_MOC_LITERAL(6, 71, 5), // "style"
QT_MOC_LITERAL(7, 77, 7), // "uiReady"
QT_MOC_LITERAL(8, 85, 15), // "handleURIOrFile"
QT_MOC_LITERAL(9, 101, 1), // "s"
QT_MOC_LITERAL(10, 103, 19) // "handleURIConnection"

    },
    "PaymentServer\0receivedPaymentRequest\0"
    "\0SendCoinsRecipient\0message\0title\0"
    "style\0uiReady\0handleURIOrFile\0s\0"
    "handleURIConnection"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_PaymentServer[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   39,    2, 0x06 /* Public */,
       4,    3,   42,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       7,    0,   49,    2, 0x0a /* Public */,
       8,    1,   50,    2, 0x0a /* Public */,
      10,    0,   53,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    2,
    QMetaType::Void, QMetaType::QString, QMetaType::QString, QMetaType::UInt,    5,    4,    6,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    9,
    QMetaType::Void,

       0        // eod
};

void PaymentServer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        PaymentServer *_t = static_cast<PaymentServer *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->receivedPaymentRequest((*reinterpret_cast< SendCoinsRecipient(*)>(_a[1]))); break;
        case 1: _t->message((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< uint(*)>(_a[3]))); break;
        case 2: _t->uiReady(); break;
        case 3: _t->handleURIOrFile((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 4: _t->handleURIConnection(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            typedef void (PaymentServer::*_t)(SendCoinsRecipient );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&PaymentServer::receivedPaymentRequest)) {
                *result = 0;
                return;
            }
        }
        {
            typedef void (PaymentServer::*_t)(const QString & , const QString & , unsigned int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&PaymentServer::message)) {
                *result = 1;
                return;
            }
        }
    }
}

const QMetaObject PaymentServer::staticMetaObject = {
    { &QObject::staticMetaObject, qt_meta_stringdata_PaymentServer.data,
      qt_meta_data_PaymentServer,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *PaymentServer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PaymentServer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_PaymentServer.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int PaymentServer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void PaymentServer::receivedPaymentRequest(SendCoinsRecipient _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void PaymentServer::message(const QString & _t1, const QString & _t2, unsigned int _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
