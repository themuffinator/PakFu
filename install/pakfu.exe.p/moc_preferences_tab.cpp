/****************************************************************************
** Meta object code from reading C++ file 'preferences_tab.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/ui/preferences_tab.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'preferences_tab.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN14PreferencesTabE_t {};
} // unnamed namespace

template <> constexpr inline auto PreferencesTab::qt_create_metaobjectdata<qt_meta_tag_ZN14PreferencesTabE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "PreferencesTab",
        "theme_changed",
        "",
        "AppTheme",
        "theme",
        "model_texture_smoothing_changed",
        "enabled",
        "image_texture_smoothing_changed",
        "preview_fov_changed",
        "degrees",
        "pure_pak_protector_changed",
        "preview_renderer_changed",
        "PreviewRenderer",
        "renderer"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'theme_changed'
        QtMocHelpers::SignalData<void(AppTheme)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'model_texture_smoothing_changed'
        QtMocHelpers::SignalData<void(bool)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 },
        }}),
        // Signal 'image_texture_smoothing_changed'
        QtMocHelpers::SignalData<void(bool)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 },
        }}),
        // Signal 'preview_fov_changed'
        QtMocHelpers::SignalData<void(int)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 9 },
        }}),
        // Signal 'pure_pak_protector_changed'
        QtMocHelpers::SignalData<void(bool)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 },
        }}),
        // Signal 'preview_renderer_changed'
        QtMocHelpers::SignalData<void(PreviewRenderer)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 12, 13 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<PreferencesTab, qt_meta_tag_ZN14PreferencesTabE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject PreferencesTab::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14PreferencesTabE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14PreferencesTabE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14PreferencesTabE_t>.metaTypes,
    nullptr
} };

void PreferencesTab::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<PreferencesTab *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->theme_changed((*reinterpret_cast<std::add_pointer_t<AppTheme>>(_a[1]))); break;
        case 1: _t->model_texture_smoothing_changed((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->image_texture_smoothing_changed((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: _t->preview_fov_changed((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->pure_pak_protector_changed((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 5: _t->preview_renderer_changed((*reinterpret_cast<std::add_pointer_t<PreviewRenderer>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (PreferencesTab::*)(AppTheme )>(_a, &PreferencesTab::theme_changed, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (PreferencesTab::*)(bool )>(_a, &PreferencesTab::model_texture_smoothing_changed, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (PreferencesTab::*)(bool )>(_a, &PreferencesTab::image_texture_smoothing_changed, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (PreferencesTab::*)(int )>(_a, &PreferencesTab::preview_fov_changed, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (PreferencesTab::*)(bool )>(_a, &PreferencesTab::pure_pak_protector_changed, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (PreferencesTab::*)(PreviewRenderer )>(_a, &PreferencesTab::preview_renderer_changed, 5))
            return;
    }
}

const QMetaObject *PreferencesTab::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PreferencesTab::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14PreferencesTabE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int PreferencesTab::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void PreferencesTab::theme_changed(AppTheme _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void PreferencesTab::model_texture_smoothing_changed(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void PreferencesTab::image_texture_smoothing_changed(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void PreferencesTab::preview_fov_changed(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void PreferencesTab::pure_pak_protector_changed(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void PreferencesTab::preview_renderer_changed(PreviewRenderer _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}
QT_WARNING_POP
