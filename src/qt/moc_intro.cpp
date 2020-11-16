/****************************************************************************
** Meta object code from reading C++ file 'intro.h'
**
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "qt/intro.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'intro.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.9.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_Intro_t {
    QByteArrayData data[13];
    char stringdata0[182];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Intro_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Intro_t qt_meta_stringdata_Intro = {
    {
QT_MOC_LITERAL(0, 0, 5), // "Intro"
QT_MOC_LITERAL(1, 6, 12), // "requestCheck"
QT_MOC_LITERAL(2, 19, 0), // ""
QT_MOC_LITERAL(3, 20, 10), // "stopThread"
QT_MOC_LITERAL(4, 31, 9), // "setStatus"
QT_MOC_LITERAL(5, 41, 6), // "status"
QT_MOC_LITERAL(6, 48, 7), // "message"
QT_MOC_LITERAL(7, 56, 14), // "bytesAvailable"
QT_MOC_LITERAL(8, 71, 28), // "on_dataDirectory_textChanged"
QT_MOC_LITERAL(9, 100, 4), // "arg1"
QT_MOC_LITERAL(10, 105, 25), // "on_ellipsisButton_clicked"
QT_MOC_LITERAL(11, 131, 25), // "on_dataDirDefault_clicked"
QT_MOC_LITERAL(12, 157, 24) // "on_dataDirCustom_clicked"

    },
    "Intro\0requestCheck\0\0stopThread\0setStatus\0"
    "status\0message\0bytesAvailable\0"
    "on_dataDirectory_textChanged\0arg1\0"
    "on_ellipsisButton_clicked\0"
    "on_dataDirDefault_clicked\0"
    "on_dataDirCustom_clicked"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Intro[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   49,    2, 0x06 /* Public */,
       3,    0,   50,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       4,    3,   51,    2, 0x0a /* Public */,
       8,    1,   58,    2, 0x08 /* Private */,
      10,    0,   61,    2, 0x08 /* Private */,
      11,    0,   62,    2, 0x08 /* Private */,
      12,    0,   63,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, QMetaType::Int, QMetaType::QString, QMetaType::ULongLong,    5,    6,    7,
    QMetaType::Void, QMetaType::QString,    9,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void Intro::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Intro *_t = static_cast<Intro *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->requestCheck(); break;
        case 1: _t->stopThread(); break;
        case 2: _t->setStatus((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< quint64(*)>(_a[3]))); break;
        case 3: _t->on_dataDirectory_textChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 4: _t->on_ellipsisButton_clicked(); break;
        case 5: _t->on_dataDirDefault_clicked(); break;
        case 6: _t->on_dataDirCustom_clicked(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            typedef void (Intro::*_t)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Intro::requestCheck)) {
                *result = 0;
                return;
            }
        }
        {
            typedef void (Intro::*_t)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Intro::stopThread)) {
                *result = 1;
                return;
            }
        }
    }
}

const QMetaObject Intro::staticMetaObject = {
    { &QDialog::staticMetaObject, qt_meta_stringdata_Intro.data,
      qt_meta_data_Intro,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *Intro::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Intro::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Intro.stringdata0))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int Intro::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void Intro::requestCheck()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void Intro::stopThread()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
