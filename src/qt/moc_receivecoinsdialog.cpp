/****************************************************************************
** Meta object code from reading C++ file 'receivecoinsdialog.h'
**
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "qt/receivecoinsdialog.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'receivecoinsdialog.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.9.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_ReceiveCoinsDialog_t {
    QByteArrayData data[21];
    char stringdata0[312];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_ReceiveCoinsDialog_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_ReceiveCoinsDialog_t qt_meta_stringdata_ReceiveCoinsDialog = {
    {
QT_MOC_LITERAL(0, 0, 18), // "ReceiveCoinsDialog"
QT_MOC_LITERAL(1, 19, 5), // "clear"
QT_MOC_LITERAL(2, 25, 0), // ""
QT_MOC_LITERAL(3, 26, 6), // "reject"
QT_MOC_LITERAL(4, 33, 6), // "accept"
QT_MOC_LITERAL(5, 40, 24), // "on_receiveButton_clicked"
QT_MOC_LITERAL(6, 65, 28), // "on_showRequestButton_clicked"
QT_MOC_LITERAL(7, 94, 30), // "on_removeRequestButton_clicked"
QT_MOC_LITERAL(8, 125, 35), // "on_recentRequestsView_doubleC..."
QT_MOC_LITERAL(9, 161, 5), // "index"
QT_MOC_LITERAL(10, 167, 35), // "recentRequestsView_selectionC..."
QT_MOC_LITERAL(11, 203, 14), // "QItemSelection"
QT_MOC_LITERAL(12, 218, 8), // "selected"
QT_MOC_LITERAL(13, 227, 10), // "deselected"
QT_MOC_LITERAL(14, 238, 17), // "updateDisplayUnit"
QT_MOC_LITERAL(15, 256, 8), // "showMenu"
QT_MOC_LITERAL(16, 265, 5), // "point"
QT_MOC_LITERAL(17, 271, 7), // "copyURI"
QT_MOC_LITERAL(18, 279, 9), // "copyLabel"
QT_MOC_LITERAL(19, 289, 11), // "copyMessage"
QT_MOC_LITERAL(20, 301, 10) // "copyAmount"

    },
    "ReceiveCoinsDialog\0clear\0\0reject\0"
    "accept\0on_receiveButton_clicked\0"
    "on_showRequestButton_clicked\0"
    "on_removeRequestButton_clicked\0"
    "on_recentRequestsView_doubleClicked\0"
    "index\0recentRequestsView_selectionChanged\0"
    "QItemSelection\0selected\0deselected\0"
    "updateDisplayUnit\0showMenu\0point\0"
    "copyURI\0copyLabel\0copyMessage\0copyAmount"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_ReceiveCoinsDialog[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
      14,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,   84,    2, 0x0a /* Public */,
       3,    0,   85,    2, 0x0a /* Public */,
       4,    0,   86,    2, 0x0a /* Public */,
       5,    0,   87,    2, 0x08 /* Private */,
       6,    0,   88,    2, 0x08 /* Private */,
       7,    0,   89,    2, 0x08 /* Private */,
       8,    1,   90,    2, 0x08 /* Private */,
      10,    2,   93,    2, 0x08 /* Private */,
      14,    0,   98,    2, 0x08 /* Private */,
      15,    1,   99,    2, 0x08 /* Private */,
      17,    0,  102,    2, 0x08 /* Private */,
      18,    0,  103,    2, 0x08 /* Private */,
      19,    0,  104,    2, 0x08 /* Private */,
      20,    0,  105,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QModelIndex,    9,
    QMetaType::Void, 0x80000000 | 11, 0x80000000 | 11,   12,   13,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QPoint,   16,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void ReceiveCoinsDialog::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        ReceiveCoinsDialog *_t = static_cast<ReceiveCoinsDialog *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->clear(); break;
        case 1: _t->reject(); break;
        case 2: _t->accept(); break;
        case 3: _t->on_receiveButton_clicked(); break;
        case 4: _t->on_showRequestButton_clicked(); break;
        case 5: _t->on_removeRequestButton_clicked(); break;
        case 6: _t->on_recentRequestsView_doubleClicked((*reinterpret_cast< const QModelIndex(*)>(_a[1]))); break;
        case 7: _t->recentRequestsView_selectionChanged((*reinterpret_cast< const QItemSelection(*)>(_a[1])),(*reinterpret_cast< const QItemSelection(*)>(_a[2]))); break;
        case 8: _t->updateDisplayUnit(); break;
        case 9: _t->showMenu((*reinterpret_cast< const QPoint(*)>(_a[1]))); break;
        case 10: _t->copyURI(); break;
        case 11: _t->copyLabel(); break;
        case 12: _t->copyMessage(); break;
        case 13: _t->copyAmount(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 7:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 1:
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QItemSelection >(); break;
            }
            break;
        }
    }
}

const QMetaObject ReceiveCoinsDialog::staticMetaObject = {
    { &QDialog::staticMetaObject, qt_meta_stringdata_ReceiveCoinsDialog.data,
      qt_meta_data_ReceiveCoinsDialog,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *ReceiveCoinsDialog::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ReceiveCoinsDialog::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ReceiveCoinsDialog.stringdata0))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int ReceiveCoinsDialog::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
